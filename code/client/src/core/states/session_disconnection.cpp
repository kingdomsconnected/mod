#include "session_disconnection.h"
#include "states.h"

#include <logging/logger.h>
#include <utils/states/machine.h>

#include "core/application.h"

namespace KCDC::Core::States {
    namespace {
        // Replicas are gone with the connection, and each Human removes its
        // body in DeallocReplica. The level is left where it is; the next
        // session's StartNewGame replaces it.
        void ReturnToMainMenu(Framework::Utils::States::Machine *machine) {
            // The game's session starts a second or two after gameplay start,
            // from C_NewGameHelper's readiness wait. Left running behind the
            // menu, its quest graph keeps profile handles the next load's
            // save read frees, and that load crashes in
            // C_GameProfileManager::ReleaseProfile.
            const auto &load = gApplication->GetSessionLoadContext();
            if (load.levelRequested && !load.gameSessionStarted) {
                gApplication->EndGameSessionWhenItStarts();
            }
            gApplication->ReturnToMenu();
            machine->RequestNextState(StateIds::MainMenu);
        }
    } // namespace

    const char *SessionDisconnectionState::GetName() const {
        return "SessionDisconnection";
    }

    int32_t SessionDisconnectionState::GetId() const {
        return StateIds::SessionDisconnection;
    }

    bool SessionDisconnectionState::OnEnter(Framework::Utils::States::Machine *machine) {
        // Lets go of the readiness hold: a level the game finished loading
        // completes the way the game completes any load.
        gApplication->CancelSessionLoading();
        gApplication->GetStreamedAssetService().EndSession();
        // Shown by MainMenu, over the transport's reason: refusing a join
        // closes the connection, which then reports itself as the cause.
        if (const char *error = gApplication->GetSessionLoadContext().error) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("{}", error);
        }
        auto *client = gApplication->GetNetworkingEngine()->GetNetworkClient();
        if (client->GetConnectionState() != Framework::Networking::PeerState::DISCONNECTED) {
            if (const auto result = client->Disconnect(); !result) {
                Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("Disconnect failed: {}", result.GetError().message);
            }
        }

        // That completion takes the loading screen down and starts gameplay,
        // and the menu comes back on the same edge, the order the game's own
        // failed loads use. A load the session asked for always ends in one --
        // even when the connection dropped mid-load -- and re-entering the
        // menu state ahead of it leaves the loading screen up under the menu.
        const auto &load = gApplication->GetSessionLoadContext();
        if (load.levelRequested && !load.gameplayStarted) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Waiting for the game to finish the abandoned load before returning to the menu");
            return true;
        }
        ReturnToMainMenu(machine);
        return false;
    }

    bool SessionDisconnectionState::OnUpdate(Framework::Utils::States::Machine *machine) {
        if (!gApplication->GetSessionLoadContext().gameplayStarted) {
            return false;
        }
        ReturnToMainMenu(machine);
        return true;
    }

    bool SessionDisconnectionState::OnExit(Framework::Utils::States::Machine *) {
        return true;
    }
} // namespace KCDC::Core::States
