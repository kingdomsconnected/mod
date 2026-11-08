#include "features/player/player_rock_module.h"

#include "core/peer_relay.h"
#include "shared/features/player/player_entity.h"

#include <core_modules.h>
#include <mafianet/get_time.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>

namespace KCDC::Features::Player {
    void Rock::Install() {
        auto *net = Framework::CoreModules::GetNetworkPeer();
        net->RegisterRPC<KCDC::Shared::RPC::RockLaunched>([this](const auto &message, MafiaNet::Packet *packet) { OnLaunched(message, packet); });
    }

    void Rock::OnLaunched(const KCDC::Shared::RPC::RockLaunched &message, MafiaNet::Packet *packet) {
        if (packet == nullptr || !message.IsSane()) {
            return;
        }
        auto *replication = Framework::CoreModules::GetReplication();
        auto *thrower     = replication->GetViewerAs<KCDC::Shared::Entities::HumanEntity>(MafiaNet::ToPeerGuid(packet->guid));
        if (thrower == nullptr || !thrower->transformReady || !thrower->soul.CanAct() || thrower->soul.incarnation != message.incarnation || thrower->stateEpoch != message.epoch) {
            return;
        }
        if (!_players[thrower->GetNetworkID()].Accept(message, thrower->position, MafiaNet::GetTime())) {
            return;
        }
        auto relay             = message;
        relay.throwerNetworkId = thrower->GetNetworkID();
        KCDC::RelayToStreamingPeers(relay, *thrower);
    }

    void Rock::Update() {
        auto *replication = Framework::CoreModules::GetReplication();
        std::erase_if(_players, [replication](const auto &entry) { return replication->GetEntity<KCDC::Shared::Entities::HumanEntity>(entry.first) == nullptr; });
    }
} // namespace KCDC::Features::Player
