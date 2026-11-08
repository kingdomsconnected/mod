#pragma once

#include <core_modules.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>

#include <cstddef>

namespace KCDC {
    /**
     * Sends `message` to every peer streaming `actor`, except the actor's
     * own owner, who produced it. Returns how many peers it reached.
     */
    template <typename Message, typename Actor> std::size_t RelayToStreamingPeers(Message &message, Actor &actor, Framework::Networking::Channel channel = Framework::Networking::Channel::Events) {
        auto *replication = Framework::CoreModules::GetReplication();
        auto *net         = Framework::CoreModules::GetNetworkPeer();
        if (replication == nullptr || net == nullptr) {
            return 0;
        }
        std::size_t recipients = 0;
        replication->ForEachStreamingPeer(&actor, [&](MafiaNet::PeerGuid peer) {
            if (peer != actor.ownerGUID) {
                net->SendRPC(message, MafiaNet::ToGuid(peer), MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered, channel);
                ++recipients;
            }
        });
        return recipients;
    }
} // namespace KCDC
