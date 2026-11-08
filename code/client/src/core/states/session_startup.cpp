#include "session_startup.h"
#include "states.h"

#include "core/application.h"
#include "features/player/player_service.h"

#include <mafianet/get_time.h>
#include <utils/states/machine.h>

namespace KCDC::Core::States {
    namespace {
        constexpr const char *kScriptsFailed = "Client scripts could not start. Check the client log for details.";
    } // namespace

    bool SessionStartupState::Enter(Framework::Utils::States::Machine *machine) {
        _machine = machine;
        // The previous transition may already have been queued when a
        // disconnect arrived. Every destination checks cancellation on entry.
        if (!gApplication->GetSessionLoadContext().active) {
            machine->RequestNextState(StateIds::SessionDisconnection);
            return false;
        }
        gApplication->SetSessionStartupState(this);
        _enteredAtMs = MafiaNet::GetTime();
        return true;
    }

    bool SessionStartupState::OnUpdate(Framework::Utils::States::Machine *) {
        // A deadline only fails the join. It never stands in for an answer.
        if (MafiaNet::GetTime() - _enteredAtMs >= kServerAnswerTimeoutMs) {
            gApplication->FailSessionLoading(kServerSilent);
        }
        return false;
    }

    bool SessionStartupState::OnExit(Framework::Utils::States::Machine *) {
        gApplication->SetSessionStartupState(nullptr);
        return true;
    }

    int32_t SessionResourcesState::GetId() const {
        return StateIds::SessionResources;
    }

    bool SessionResourcesState::OnEnter(Framework::Utils::States::Machine *machine) {
        if (!Enter(machine)) {
            return false;
        }
        // Synchronous, resourceStart promises included, and on a normal tick
        // after the level loaded: the game is parked behind the session's hold.
        const bool started = gApplication->CompleteDeferredInitialAssetProcessing(gApplication->GetAssetProcessingGeneration(), true);
        auto &load         = gApplication->GetSessionLoadContext();
        if (started) {
            if (load.active) {
                machine->RequestNextState(StateIds::SessionReplication);
            }
        }
        else if (load.active) {
            gApplication->FailSessionLoading(kScriptsFailed);
        }
        else {
            // The Framework refuses the join by disconnecting, which can close
            // the session before this returns. The reason still belongs on the
            // menu, rather than the transport's.
            load.error = kScriptsFailed;
        }
        return machine->GetNextState() == nullptr;
    }

    int32_t SessionReplicationState::GetId() const {
        return StateIds::SessionReplication;
    }

    bool SessionReplicationState::OnEnter(Framework::Utils::States::Machine *machine) {
        if (!Enter(machine)) {
            return false;
        }
        // Catch an edge that arrived between states. Subsequent completion
        // comes through OnReplicationReady, never an OnUpdate query.
        if (gApplication->GetNetworkingEngine()->GetNetworkClient()->IsInitialReplicationDownloadComplete()) {
            OnReplicationReady();
        }
        return machine->GetNextState() == nullptr;
    }

    void SessionReplicationState::OnReplicationReady() {
        _machine->RequestNextState(StateIds::SessionSpawn);
    }

    int32_t SessionSpawnState::GetId() const {
        return StateIds::SessionSpawn;
    }

    bool SessionSpawnState::OnEnter(Framework::Utils::States::Machine *machine) {
        if (!Enter(machine)) {
            return false;
        }
        _answered  = false;
        _settled   = false;
        _placement = 0;
        if (!KCDC::Features::Player::PlayerService::RequestSpawn()) {
            gApplication->FailSessionLoading("The server did not provide this connection's player.");
        }
        return machine->GetNextState() == nullptr;
    }

    void SessionSpawnState::OnSpawnAnswered(std::uint64_t placement) {
        if (_answered) {
            return;
        }
        _answered  = true;
        _placement = placement;
        if (placement == 0) {
            OnPlacementSettled(0, true);
        }
    }

    void SessionSpawnState::OnPlacementSettled(std::uint64_t placement, bool success) {
        if (!_answered || _settled || placement != _placement) {
            return;
        }
        _settled = true;
        if (success) {
            _machine->RequestNextState(StateIds::SessionFinalize);
        }
        else {
            gApplication->FailSessionLoading("The game could not place the player at the spawn point.");
        }
    }

    int32_t SessionFinalizeState::GetId() const {
        return StateIds::SessionFinalize;
    }

    bool SessionFinalizeState::OnEnter(Framework::Utils::States::Machine *machine) {
        if (!Enter(machine)) {
            return false;
        }
        gApplication->SignalConnectionSpawnReady();
        if (gApplication->GetConnectionPhase() == Framework::Integrations::Client::ConnectionPhase::InGame) {
            OnConnectionReady();
        }
        return machine->GetNextState() == nullptr;
    }

    void SessionFinalizeState::OnConnectionReady() {
        _machine->RequestNextState(StateIds::SessionConnected);
    }
} // namespace KCDC::Core::States
