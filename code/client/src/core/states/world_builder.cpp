#include "world_builder.h"
#include "states.h"

#include <logging/logger.h>
#include <utils/states/machine.h>

#include "core/application.h"
#include "core/crash_context.h"
#include "common/module_support.h"

namespace KCDC::Core::States {
    namespace {
        // The level load blocks the game thread, and the render thread keeps
        // drawing the last frame it was handed. CryEngine's main thread waits
        // for the render thread's previous frame at the end of each of its
        // own, so two ticks after the page is hidden a frame without it has
        // been presented.
        constexpr std::uint32_t kHiddenFramesBeforeLoad = 2;

        // A safety net, not a budget: a level that has not started gameplay by
        // then is not going to.
        constexpr std::chrono::minutes kLoadTimeout {5};
    } // namespace

    const char *WorldBuilderState::GetName() const {
        return "WorldBuilder";
    }

    int32_t WorldBuilderState::GetId() const {
        return StateIds::WorldBuilder;
    }

    bool WorldBuilderState::OnEnter(Framework::Utils::States::Machine *) {
        _phase        = Phase::PreparingMods;
        _hiddenFrames = 0;
        gApplication->GetStreamedAssetService().BeginLocalMods(gApplication->GetOfflineBuilderModsFolder(), gApplication->GetOfflineBuilderMods());
        if (!gApplication->GetOfflineBuilderMods().empty()) {
            gApplication->GetMultiplayerFrontend().ShowAdmission("Preparing the client mods. The first time can take a few minutes.");
            gApplication->ShowMultiplayerFrontend();
        }
        CrashContext::SetTag("session.state", "world-builder");
        CrashContext::Breadcrumb("state", "Entered the offline World Builder");
        return true;
    }

    bool WorldBuilderState::OnUpdate(Framework::Utils::States::Machine *machine) {
        if (gApplication->IsOfflineBuilderReady()) {
            if (auto *soul = KCDC::Common::LocalSoul()) {
                // Hunger is nourishment: 100 is fed without overfeeding.
                // Use the native tick writer to avoid queuing RPG events.
                soul->SetStateWithCause(SDK::wh::rpgmodule::E_SoulState::Hunger, 100.0F);
                soul->SetStateWithCause(SDK::wh::rpgmodule::E_SoulState::Stamina, soul->GetStateMax(SDK::wh::rpgmodule::E_SoulState::Stamina));
                // Exhaust is remaining energy, so its maximum is fully rested.
                soul->SetStateWithCause(SDK::wh::rpgmodule::E_SoulState::Exhaust, soul->GetStateMax(SDK::wh::rpgmodule::E_SoulState::Exhaust));
            }
        }
        switch (_phase) {
        case Phase::PreparingMods: {
            if (gApplication->GetMultiplayerFrontend().ConsumeLeaveRequest()) {
                Leave(machine, nullptr);
                return true;
            }
            KCDC::Core::StreamedAssets::StreamedAssetService &assets = gApplication->GetStreamedAssetService();
            const KCDC::Core::StreamedAssets::StreamedAssetService::JoinStatus status = assets.PollLocalMods();
            if (status == KCDC::Core::StreamedAssets::StreamedAssetService::JoinStatus::Failed) {
                gApplication->EndAdmissionWait();
                Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("{}", assets.GetError());
                Leave(machine, "The client mods could not be loaded. Check the client log.");
                return true;
            }
            if (status == KCDC::Core::StreamedAssets::StreamedAssetService::JoinStatus::Ready) {
                gApplication->EndAdmissionWait();
                _phase = Phase::HidingMenu;
            }
            return false;
        }
        case Phase::HidingMenu: {
            if (++_hiddenFrames < kHiddenFramesBeforeLoad) {
                return false;
            }
            if (!gApplication->LoadSessionLevel(gApplication->GetOfflineBuilderLevel(), true)) {
                Leave(machine, "The game cannot start a level yet. Try again once loading finishes.");
                return true;
            }
            _phase       = Phase::Loading;
            _loadStarted = std::chrono::steady_clock::now();
            return false;
        }
        case Phase::Loading: {
            if (gApplication->IsOfflineBuilderReady()) {
                _phase = Phase::Editing;
                gApplication->GetMapEditor().SetOpen(true);
                return false;
            }
            if (std::chrono::steady_clock::now() - _loadStarted > kLoadTimeout) {
                Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("The World Builder level did not start gameplay within {} minutes", kLoadTimeout.count());
                Leave(machine, "The World Builder level did not finish loading. Check the client log.");
                return true;
            }
            return false;
        }
        case Phase::Editing: {
            if (gApplication->GetMapEditor().ConsumeExitRequest()) {
                Leave(machine, nullptr);
                return true;
            }
            return false;
        }
        }
        return false;
    }

    bool WorldBuilderState::OnExit(Framework::Utils::States::Machine *) {
        return true;
    }

    void WorldBuilderState::Leave(Framework::Utils::States::Machine *machine, const char *error) {
        gApplication->EndOfflineBuilder();
        gApplication->GetSessionLoadContext().error = error;
        machine->RequestNextState(StateIds::MainMenu);
    }
} // namespace KCDC::Core::States
