#include "social_emote_service.h"

#include "shared/features/player/player_entity.h"
#include <core_modules.h>
#include <mafianet/get_time.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>

namespace KCDC::Features::Social {
    void EmoteService::Install() {
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr) {
            return;
        }
        net->RegisterRPC<Shared::RPC::Emote>([this](const Shared::RPC::Emote &event, MafiaNet::Packet *packet) {
            auto *replication = Framework::CoreModules::GetReplication();
            auto *peer        = Framework::CoreModules::GetNetworkPeer();
            if (!event.IsSane() || packet == nullptr || replication == nullptr || peer == nullptr) {
                return;
            }
            const auto guid = MafiaNet::ToPeerGuid(packet->guid);
            auto *actor     = replication->GetViewerAs<Shared::Entities::HumanEntity>(guid);
            if (actor == nullptr || !_senders[guid].Accept(event, MafiaNet::GetTime(), true)) {
                return;
            }
            // Resolve sender identity from the connection.
            auto relay           = event;
            relay.actorNetworkId = actor->GetNetworkID();
            peer->BroadcastRPC(relay);
        });
    }

    void EmoteService::OnPlayerDisconnect(MafiaNet::PeerGuid guid) {
        _senders.erase(guid);
    }
} // namespace KCDC::Features::Social
