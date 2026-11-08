#include "features/player/player_ledge_module.h"

#include <core_modules.h>
#include <mafianet/get_time.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>

namespace KCDC::Features::Player {
    std::size_t Ledge::Relay(const KCDC::Shared::RPC::PlayerLedgeGrab &grab, KCDC::Shared::Entities::HumanEntity &actor) {
        auto *net         = Framework::CoreModules::GetNetworkPeer();
        auto *replication = Framework::CoreModules::GetReplication();
        if (net == nullptr || replication == nullptr) {
            return 0;
        }

        KCDC::Shared::RPC::PlayerLedgeGrab relay = grab;
        relay.climberNetworkId                    = actor.GetNetworkID();
        relay.relayedAt                           = MafiaNet::GetTime();
        std::size_t recipients                    = 0;
        // Only the peers that have the climber's body can play its grab.
        replication->ForEachStreamingPeer(&actor, [&](MafiaNet::PeerGuid peer) {
            if (peer != actor.ownerGUID) {
                net->SendRPC(relay, MafiaNet::ToGuid(peer), MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered, Framework::Networking::Channel::Presentation);
                ++recipients;
            }
        });
        return recipients;
    }
} // namespace KCDC::Features::Player
