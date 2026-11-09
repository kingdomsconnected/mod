#include "main_menu.h"
#include "states.h"

#include <logging/logger.h>
#include <utils/states/machine.h>

#include <utility>

#include "core/application.h"
#include "core/crash_context.h"
#if defined(KCDC_AGENT_TESTING)
#include "core/boot/agent_testing.h"
#endif
#include "core/dlc.h"
#include "features/prop/prop_editor_document.h"
#include "shared/game/dlc_catalog.h"
#include "shared/game/levels.h"

#include "sdk/cryengine/system/i_system.h"

namespace KCDC::Core::States {
    const char *MainMenuState::GetName() const {
        return "MainMenu";
    }

    int32_t MainMenuState::GetId() const {
        return StateIds::MainMenu;
    }

    bool MainMenuState::OnEnter(Framework::Utils::States::Machine *) {
        _shouldProceedConnection = false;
        CrashContext::SetTag("session.state", "menu");
        CrashContext::Breadcrumb("state", "Entered main menu");

        // A kcdc:// launch skips the page and connects straight away. Only the
        // first entry has one: taking it clears it, so a disconnect lands on
        // the menu instead of reconnecting in a loop.
        if (auto link = gApplication->TakeLaunchConnection()) {
            CrashContext::Breadcrumb("state", "Connecting from a launch link");
            ApplyConnectRequest(std::move(*link));
            return true;
        }

        // Why the last join was refused outranks how its connection ended.
        if (const char *error = gApplication->GetSessionLoadContext().error) {
            gApplication->GetMultiplayerFrontend().ShowError(error);
            gApplication->GetSessionLoadContext().error = nullptr;
        }
        else if (gApplication->ConsumeOfflineBuilderEnded()) {
            // Back from the World Builder: how an earlier connection ended is
            // no longer news.
            gApplication->GetMultiplayerFrontend().ShowMenu();
        }
        else {
            gApplication->GetMultiplayerFrontend().ShowMenu(gApplication->GetLastDisconnectionReason());
        }
        gApplication->ShowMultiplayerFrontend();
        return true;
    }

    bool MainMenuState::OnUpdate(Framework::Utils::States::Machine *machine) {
#if defined(KCDC_AGENT_TESTING)
        if (auto level = Boot::AgentTesting::Get().TakeWorldBuilderLevel()) {
            if (_shouldProceedConnection) {
                Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Agent testing: ignored World Builder request because a launch connection is pending");
            }
            else {
                WorldBuilderRequest request;
                request.level = std::move(*level);
                if (StartWorldBuilder(request)) {
                    Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Agent testing: starting World Builder in '{}' from KCDC_TEST_WORLD_BUILDER", request.level);
                    machine->RequestNextState(StateIds::WorldBuilder);
                    return true;
                }
                Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("Agent testing: World Builder startup refused for '{}'", request.level);
            }
        }
        if (auto link = Boot::AgentTesting::Get().TakeConnectLink()) {
            if (auto request = ParseLaunchLink(*link)) {
                Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Agent testing: connecting from KCDC_TEST_CONNECT_FILE");
                ApplyConnectRequest(std::move(*request));
            }
            else {
                Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("Agent testing: ignored a malformed link in KCDC_TEST_CONNECT_FILE");
            }
        }
#endif
        if (const std::optional<WorldBuilderRequest> request = gApplication->GetMultiplayerFrontend().ConsumeBuilderRequest()) {
            if (StartWorldBuilder(*request)) {
                machine->RequestNextState(StateIds::WorldBuilder);
                return true;
            }
            return false;
        }
        if (gApplication->GetMultiplayerFrontend().ConsumeQuitRequest()) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Quit asked for from the menu");
            CrashContext::Breadcrumb("state", "Quit from main menu");
            // The mod's own teardown is the detour on this in core/boot/quit.cpp, so there is
            // nothing to do here first; the call does not come back.
            SDK::ISystem::GetInstance()->Quit("Kingdoms Connected menu");
            return true;
        }

        if (auto request = gApplication->GetMultiplayerFrontend().ConsumeConnectRequest()) {
            ApplyConnectRequest(std::move(*request));
        }

        if (!_shouldProceedConnection) {
            return false;
        }

        machine->RequestNextState(StateIds::SessionConnection);
        return true;
    }

    bool MainMenuState::StartWorldBuilder(const WorldBuilderRequest &request) {
        KCDC::Core::MultiplayerFrontend &frontend = gApplication->GetMultiplayerFrontend();
        std::string level                         = request.level;
        std::string project;
        std::string error;
        if (request.action == WorldBuilderRequest::Action::OpenProject) {
            project = KCDC::Features::Prop::MapFiles::ChoosePath(false, false, "", error);
            if (project.empty()) {
                if (!error.empty()) {
                    frontend.ShowError(error);
                }
                return false;
            }
        }
        else if (request.action == WorldBuilderRequest::Action::LastProject) {
            const auto recent = KCDC::Features::Prop::MapFiles::RecentProjects();
            if (recent.empty()) {
                frontend.ShowError("No previous project was found. Use Open project, or create and save a new project.");
                return false;
            }
            project = recent.front();
        }
        if (!project.empty()) {
            std::string text;
            KCDC::Features::Prop::MapDocument document;
            if (!KCDC::Features::Prop::MapFiles::ReadPath(project, text, error) || !KCDC::Features::Prop::ParseProject(text, document, error)) {
                frontend.ShowError(request.action == WorldBuilderRequest::Action::LastProject ? "Could not load the last project. Use Open project to choose it again.\n" + error : error);
                return false;
            }
            level = document.level;
        }

        const Shared::Game::PlayableLevel *playable = Shared::Game::FindPlayableLevel(level);
        if (playable == nullptr) {
            frontend.ShowError("This project names an unsupported level.");
            return false;
        }
        if (!playable->dlc.empty()) {
            const Shared::Game::DLCEntry *dlc = Shared::Game::FindDLCByName(playable->dlc);
            if (dlc != nullptr && Core::MissingDLC(Shared::Game::DLCBit(dlc->id))) {
                frontend.ShowError("This level requires the " + std::string(dlc->displayName) + " DLC.");
                return false;
            }
        }

        if (project.empty()) {
            gApplication->GetMapEditor().RequestNewProject();
        }
        else {
            gApplication->GetMapEditor().RequestProject(project);
        }
        gApplication->SetOfflineBuilderLevel(level);
        gApplication->HideMultiplayerFrontend();
        CrashContext::Breadcrumb("state", "World Builder asked for from the menu");
        return true;
    }

    void MainMenuState::ApplyConnectRequest(ConnectRequest request) {
#if defined(KCDC_AGENT_TESTING)
        Boot::AgentTesting::Get().SetNickname(request.nickname);
#endif
        auto state     = gApplication->GetCurrentState();
        state.host     = std::move(request.host);
        state.port     = request.port;
        state.password = std::move(request.password);
        state.nickname = std::move(request.nickname);
        state.ticket   = std::move(request.ticket);
        gApplication->SetCurrentState(std::move(state));

        _shouldProceedConnection = true;
    }

    // The page is taken down by whoever takes the view: SessionConnection for
    // the game's loading screen, PreShutdown for the mod going away.
    bool MainMenuState::OnExit(Framework::Utils::States::Machine *) {
        return true;
    }
} // namespace KCDC::Core::States
