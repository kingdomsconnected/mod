#include "session_connected.h"
#include "states.h"

#include <logging/logger.h>

#include "core/application.h"
#include "features/player/player_service.h"

namespace KCDC::Core::States {
    const char *SessionConnectedState::GetName() const {
        return "SessionConnected";
    }

    int32_t SessionConnectedState::GetId() const {
        return StateIds::SessionConnected;
    }

    bool SessionConnectedState::OnEnter(Framework::Utils::States::Machine *machine) {
        // A disconnect can arrive after RequestNextState but before entry.
        if (!gApplication->GetSessionLoadContext().active) {
            machine->RequestNextState(StateIds::SessionDisconnection);
            return false;
        }
        // Still behind the loading screen: the game's load completion waits
        // on the session's readiness hold, which is let go only once the
        // session is open. Gameplay start follows natively, and playerReady
        // goes out from LocalEvents on the first session tick after it.
        if (!gApplication->OpenSession()) {
            gApplication->FailSessionLoading("The loaded level did not provide a local player.");
            return false;
        }
        gApplication->ReleaseSessionHold();
        const auto state = gApplication->GetCurrentState();
        gApplication->HideMultiplayerFrontend();
        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Session established with {}:{}", state.host, state.port);
        return machine->GetNextState() == nullptr;
    }

    bool SessionConnectedState::OnUpdate(Framework::Utils::States::Machine *) {
        // The order these tick in is load-bearing, and lives in
        // core/feature_registry.cpp along with the reasons for it.
        gApplication->UpdateSessionFeatures();

        // Widget callbacks run in Wrapper::Update on the game thread, before
        // the next Human::UpdateAll. Read the pose and project it there, not
        // after placing next tick's bodies against this tick's camera/pose.
        gApplication->GetImGUI()->PushWidget([]() {
            // The session opens behind the game's loading screen, which stays
            // up until the game has completed the load. Nothing drawn over
            // play belongs on it.
            if (KCDC::Core::gApplication->IsGameLoading()) {
                return;
            }
            KCDC::Core::gApplication->GetPlayerService().RenderNametags();
            KCDC::Core::gApplication->GetVoiceService().RenderTransmitIndicator();

            // The logo sits over play, not over the game's own screens.
            if (!KCDC::Core::gApplication->IsHudCovered()) {
                KCDC::Core::gApplication->GetBrandLogo().Render();
            }
        });

        if (const auto input = gApplication->GetInput(); input && input->IsKeyPressed(FW_KEY_F9) && !gApplication->HasExclusiveWebInput() && !gApplication->GetChatBox().IsInputActive()) {
            if (const auto result = gApplication->GetNetworkingEngine()->GetNetworkClient()->Disconnect(); !result) {
                Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("Disconnect failed: {}", result.GetError().message);
            }
        }

        // The transition is driven by Application::OnConnectionClosed, so a
        // server-side drop lands in the same place as a local disconnect.
        return false;
    }

    bool SessionConnectedState::OnExit(Framework::Utils::States::Machine *) {
        // Clear everything latched about the session: the next one's horses
        // are different horses, its items different items. The order is the
        // registration order in core/feature_registry.cpp, the same one the
        // tick uses.
        gApplication->ResetSessionFeatures();

        // Not a registry entry, and deliberately: the blip service also
        // ticks outside a session, from Application::PostUpdate, so a sweep
        // that reached it would reconcile its marks twice inside one. Its
        // registry is session state all the same.
        gApplication->GetMapBlipService().RemoveAll();
        return true;
    }
} // namespace KCDC::Core::States
