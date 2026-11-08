#include "stone_pile_service.h"

#include "core/server_clock.h"
#include "features/player/player_roster.h"
#include "features/siege/siege_common.h"
#include "features/siege/stone_pile_scripting.h"

#include "shared/features/player/player_entity.h"
#include "shared/pose_math.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>

namespace KCDC::Features::Siege {
    namespace {
        constexpr const char *kLogger = "StonePile";

        /**
         * A stone's report is taken this long after its grab: the grab and
         * the throw run about six seconds, and a stone rolls down a slope for
         * a while after it lands.
         */
        constexpr std::uint64_t kReportWindowMs = 15000;

        /** How far from its pile a stone may come down, in metres: heaved over a wall, it drops and rolls. */
        constexpr float kReportReach = 30.0F;

        /** How far from where its thrower stood at the grab a stone may come down, in metres: the same heave and roll, measured from the hand. */
        constexpr float kThrowReach = 30.0F;

        /** The least time between two grabs by one player: the grab and the throw run about six seconds, so a faster one is forged. */
        constexpr std::uint64_t kGrabIntervalMs = 2000;

        /** Who is told of a grab, and of an impact: anyone near enough to see it. */
        constexpr float kRelayRadius = Shared::Entities::StonePileEntity::kStreamRange;
    } // namespace

    StonePileService &StonePileService::Get() {
        static StonePileService instance;
        return instance;
    }

    void StonePileService::Install() {
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr) {
            return;
        }
        net->RegisterRPC<Shared::RPC::StonePileUse>([this](const Shared::RPC::StonePileUse &use, MafiaNet::Packet *packet) {
            OnUse(use, packet);
        });
        net->RegisterRPC<Shared::RPC::StoneImpactReport>([this](const Shared::RPC::StoneImpactReport &report, MafiaNet::Packet *packet) {
            OnImpactReport(report, packet);
        });
    }

    void StonePileService::OnPlayerDisconnect(std::uint64_t playerNetworkId) {
        _pending.erase(playerNetworkId);
    }

    void StonePileService::PruneExpired(std::uint64_t now) {
        std::erase_if(_pending, [now](const auto &pending) {
            return now - pending.second.grabbedAtMs > kReportWindowMs;
        });
    }

    Shared::Entities::StonePileEntity *StonePileService::Spawn(const glm::vec3 &position, const glm::quat &rotation, MafiaNet::VirtualWorldId virtualWorld) {
        if (!Shared::Game::IsFinite(position) || !Shared::Game::IsUsableRotation(rotation)) {
            return nullptr;
        }
        auto *replication = Framework::CoreModules::GetReplication();
        auto *pile        = replication != nullptr ? replication->CreateEntity<Shared::Entities::StonePileEntity>() : nullptr;
        if (pile == nullptr) {
            Framework::Logging::GetLogger(kLogger)->error("Could not create a stone pile; is '{}' registered?", Shared::Entities::StonePileEntity::kTypeName);
            return nullptr;
        }
        pile->position        = position;
        pile->rotation        = glm::normalize(rotation);
        pile->streaming.range = Shared::Entities::StonePileEntity::kStreamRange;
        pile->SetVirtualWorld(virtualWorld);

        Framework::Logging::GetLogger(kLogger)->info("Spawned stone pile {} in world {} at {:.2f}, {:.2f}, {:.2f}", pile->GetNetworkID(), virtualWorld, position.x, position.y, position.z);
        Scripting::Events::StonePileSpawned(pile->GetNetworkID());
        return pile;
    }

    void StonePileService::Destroy(Shared::Entities::StonePileEntity *pile) {
        auto *replication = Framework::CoreModules::GetReplication();
        if (pile == nullptr || replication == nullptr) {
            return;
        }
        Scripting::Events::StonePileDestroyed(pile->GetNetworkID());
        // A stone already thrown still lands; only the grabs that have not are dropped.
        const std::uint64_t pileNetworkId = pile->GetNetworkID();
        std::erase_if(_pending, [pileNetworkId](const auto &pending) {
            return pending.second.pileNetworkId == pileNetworkId;
        });
        replication->DestroyEntity(pile);
    }

    std::size_t StonePileService::DestroyAll(std::optional<MafiaNet::VirtualWorldId> virtualWorld) {
        const std::vector<Shared::Entities::StonePileEntity *> doomed = Collect(virtualWorld);
        for (Shared::Entities::StonePileEntity *pile : doomed) {
            Destroy(pile);
        }
        return doomed.size();
    }

    std::vector<Shared::Entities::StonePileEntity *> StonePileService::Collect(std::optional<MafiaNet::VirtualWorldId> virtualWorld) {
        return CollectSiegeEntities<Shared::Entities::StonePileEntity>(virtualWorld);
    }

    Shared::Entities::StonePileEntity *StonePileService::Find(std::uint64_t networkId) {
        return FindSiegeEntity<Shared::Entities::StonePileEntity>(networkId);
    }

    template <typename Message>
    void StonePileService::Relay(Message message, const Shared::Entities::StonePileEntity &pile, const glm::vec3 &point, float radius, std::uint64_t exceptNetworkId) {
        auto *replication = Framework::CoreModules::GetReplication();
        auto *net         = Framework::CoreModules::GetNetworkPeer();
        if (replication == nullptr || net == nullptr) {
            return;
        }
        replication->ForEach<Shared::Entities::HumanEntity>([&](Shared::Entities::HumanEntity *viewer) {
            if (viewer->GetNetworkID() == exceptNetworkId || viewer->GetVirtualWorld() != pile.GetVirtualWorld() || !viewer->transformReady || glm::distance(viewer->position, point) > radius) {
                return;
            }
            net->SendRPC(message, MafiaNet::ToGuid(viewer->ownerGUID), MafiaNet::Priority::High, MafiaNet::Reliability::Reliable);
        });
    }

    void StonePileService::OnUse(const Shared::RPC::StonePileUse &use, MafiaNet::Packet *packet) {
        if (!use.IsSane()) {
            return;
        }
        Shared::Entities::HumanEntity *player     = packet != nullptr ? Player::Players::FromPacket(packet) : nullptr;
        Shared::Entities::StonePileEntity *pile   = Find(use.pileNetworkId);
        if (player == nullptr || pile == nullptr || player->GetVirtualWorld() != pile->GetVirtualWorld()) {
            return;
        }
        const std::uint64_t playerNetworkId = player->GetNetworkID();

        Shared::RPC::StonePileUsed used;
        used.pileNetworkId   = pile->GetNetworkID();
        used.playerNetworkId = playerNetworkId;
        used.action          = use.action;

        const std::uint64_t now = KCDC::ServerClock::NowMs();
        const auto pending      = _pending.find(playerNetworkId);

        // Only the end of a grab this player made at this pile, or of one
        // made where they could have: anything else ends nothing.
        if (use.action == Shared::RPC::StonePileAction::Leave) {
            const bool grabbed = pending != _pending.end() && pending->second.pileNetworkId == pile->GetNetworkID() && !pending->second.left;
            if (!grabbed && !(player->transformReady && glm::distance(player->position, pile->position) <= Shared::Entities::StonePileEntity::kGrabReach)) {
                return;
            }
            if (grabbed) {
                pending->second.left = true;
            }
            Relay(used, *pile, pile->position, kRelayRadius, playerNetworkId);
            return;
        }
        // The throw of the stone this player's grab put in their hands, and
        // nothing else. The impact window runs from the throw.
        if (use.action == Shared::RPC::StonePileAction::Throw) {
            if (pending == _pending.end() || pending->second.pileNetworkId != pile->GetNetworkID() || pending->second.left || pending->second.thrown) {
                return;
            }
            pending->second.thrown      = true;
            pending->second.grabbedAtMs = now;
            Relay(used, *pile, pile->position, kRelayRadius, playerNetworkId);
            return;
        }
        if (pending != _pending.end() && now - pending->second.grabbedAtMs < kGrabIntervalMs) {
            return;
        }

        // The game offers the grab only while the pile shows a stone, which it
        // does only while this count allows; a grab from afar or from an
        // empty pile is a stale or forged one.
        if (!pile->usable || !pile->HasStones() || !player->transformReady || glm::distance(player->position, pile->position) > Shared::Entities::StonePileEntity::kGrabReach) {
            return;
        }
        if (pile->stones > 0) {
            --pile->stones;
        }
        // Cheap here: grabs are rare, and this is what keeps unreported throws from piling up.
        PruneExpired(now);
        _pending[playerNetworkId] = PendingThrow {pile->GetNetworkID(), now, player->position};
        Relay(used, *pile, pile->position, kRelayRadius, playerNetworkId);
        Framework::Logging::GetLogger(kLogger)->info("Player {} threw a stone from pile {}, {} left", playerNetworkId, pile->GetNetworkID(), pile->stones);
        Scripting::Events::StonePileThrow(playerNetworkId, pile->GetNetworkID());
    }

    void StonePileService::OnImpactReport(const Shared::RPC::StoneImpactReport &report, MafiaNet::Packet *packet) {
        if (!report.IsSane()) {
            return;
        }
        Shared::Entities::HumanEntity *player = packet != nullptr ? Player::Players::FromPacket(packet) : nullptr;
        if (player == nullptr) {
            return;
        }
        const std::uint64_t playerNetworkId = player->GetNetworkID();
        const auto pending                  = _pending.find(playerNetworkId);
        if (pending == _pending.end() || pending->second.pileNetworkId != report.pileNetworkId || pending->second.reported || KCDC::ServerClock::NowMs() - pending->second.grabbedAtMs > kReportWindowMs) {
            return;
        }
        // One report per grab, whatever it says. The entry itself stays for
        // the grab's leave and its interval, and goes with its window.
        pending->second.reported = true;

        Shared::Entities::StonePileEntity *pile = Find(report.pileNetworkId);
        if (pile == nullptr || glm::distance(pile->position, report.point) > kReportReach || glm::distance(pending->second.grabbedFrom, report.point) > kThrowReach) {
            return;
        }

        Shared::RPC::StoneImpact impact;
        impact.attackerNetworkId = playerNetworkId;
        impact.point             = report.point;
        impact.damage            = pile->damage;
        impact.damageRadius      = pile->damageRadius;
        // Never the thrower: the stone leaves their hands forward and away.
        Relay(impact, *pile, report.point, kRelayRadius, playerNetworkId);

        HarmNpcsInBlast(pile->GetVirtualWorld(), report.point, pile->damage, pile->damageRadius, playerNetworkId);

        Framework::Logging::GetLogger(kLogger)->info("A stone from pile {} came down at {:.1f}, {:.1f}, {:.1f}, as player {} saw it", report.pileNetworkId, report.point.x, report.point.y, report.point.z, playerNetworkId);
        Scripting::Events::StoneImpact(report.pileNetworkId, playerNetworkId, report.point);
    }
} // namespace KCDC::Features::Siege
