#include "social_scoreboard_service.h"

#include "core/server.h"

#include "shared/features/player/player_entity.h"

#include <core_modules.h>
#include <mafianet/get_time.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>

#include <algorithm>
#include <cstddef>
#include <limits>
#include <string>

namespace KCDC::Features::Social {
    namespace {
        /** A board refreshes every two seconds; anything asking faster than this is not a board. */
        constexpr std::uint64_t kMinAnswerIntervalMs = 400;

        /**
         * Cut at a character boundary, not a byte count: a name cut through
         * the middle of a UTF-8 sequence draws as a replacement glyph.
         */
        std::string BoundedName(const std::string &name) {
            if (name.size() <= KCDC::Shared::RPC::kMaxScoreboardName) {
                return name;
            }
            std::size_t end = KCDC::Shared::RPC::kMaxScoreboardName;
            while (end > 0 && (static_cast<unsigned char>(name[end]) & 0xC0U) == 0x80U) {
                --end;
            }
            return name.substr(0, end);
        }
    } // namespace

    void ScoreboardService::Install(bool enabled) {
        _enabled = enabled;
        Framework::CoreModules::GetNetworkPeer()->RegisterRPC<KCDC::Shared::RPC::ScoreboardRequest>([this](const KCDC::Shared::RPC::ScoreboardRequest &request, MafiaNet::Packet *packet) {
            if (!_enabled || !request.IsSane() || packet == nullptr) {
                return;
            }
            Answer(request, MafiaNet::ToPeerGuid(packet->guid));
        });
    }

    void ScoreboardService::OnRosterChanged() {
        // Wraps past zero onto one, since zero is the ask of a client holding nothing.
        _revision = _revision == std::numeric_limits<std::uint32_t>::max() ? 1 : _revision + 1;
    }

    void ScoreboardService::OnPlayerDisconnect(MafiaNet::PeerGuid guid) {
        _answeredAt.erase(guid);
        OnRosterChanged();
    }

    void ScoreboardService::Answer(const KCDC::Shared::RPC::ScoreboardRequest &request, MafiaNet::PeerGuid guid) {
        auto *replication = Framework::CoreModules::GetReplication();
        auto *net         = Framework::CoreModules::GetNetworkPeer();
        auto *asker       = replication != nullptr ? replication->GetViewerAs<KCDC::Shared::Entities::HumanEntity>(guid) : nullptr;
        if (asker == nullptr || net == nullptr) {
            return;
        }

        // The first ask of a connection is always answered, whatever the clock says.
        const std::uint64_t now = MafiaNet::GetTime();
        if (const auto found = _answeredAt.find(guid); found != _answeredAt.end() && now - found->second < kMinAnswerIntervalMs) {
            return;
        }
        _answeredAt[guid] = now;

        KCDC::Shared::RPC::Scoreboard answer;
        answer.revision   = _revision;
        answer.localSlot  = asker->playerIndex;
        answer.maxPlayers = static_cast<std::uint16_t>(std::clamp<std::int32_t>(KCDC::Server::Get()->GetOptions().maxPlayers, 0, std::numeric_limits<std::uint16_t>::max()));
        answer.names      = request.knownRevision != _revision;

        replication->ForEach<KCDC::Shared::Entities::HumanEntity>([&](KCDC::Shared::Entities::HumanEntity *human) {
            // Only connections: a body with no slot is nobody's avatar.
            if (human->playerIndex == MafiaNet::UNASSIGNED_PLAYER_INDEX || answer.rows.size() >= KCDC::Shared::RPC::kMaxScoreboardRows) {
                return;
            }
            KCDC::Shared::RPC::ScoreboardRow row;
            row.slot = human->playerIndex;

            // -1 until the connection has a measured round trip.
            row.ping = static_cast<std::uint16_t>(std::clamp(net->GetPing(MafiaNet::ToGuid(human->ownerGUID)), 0, static_cast<int>(std::numeric_limits<std::uint16_t>::max())));
            if (answer.names) {
                row.name = BoundedName(human->nickname);
            }
            answer.rows.push_back(std::move(row));
        });

        net->SendRPC(answer, MafiaNet::ToGuid(guid), MafiaNet::Priority::Medium, MafiaNet::Reliability::ReliableOrdered);
    }
} // namespace KCDC::Features::Social
