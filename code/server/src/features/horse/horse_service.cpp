#include "horse_service.h"

#include "features/horse/horse_scripting.h"
#include "features/horse/horse_verbs.h"
#include "features/player/player_roster.h"

#include "shared/features/horse/horse_damage.h"
#include "shared/features/horse/horse_entity.h"
#include "shared/features/horse/horse_gear.h"
#include "shared/features/horse/horse_mount.h"
#include "shared/features/horse/horse_stats.h"
#include "shared/features/player/player_entity.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>

#include <glm/glm.hpp>

#include <cmath>
#include <cstdint>

namespace KCDC::Features::Horse {
    namespace {
        constexpr const char *kLogger = "Horse";

        // Matches the client's own comparison: two peers can differ in the last
        // bit and trade reports forever over a difference nothing can see.
        constexpr float kCapacityEpsilon = 0.01F;

        // How far from a horse a claimed hit may come from. A bow's reach with
        // room to spare: past it the claim is not about the horse this server
        // has, whatever the attacker's copy says.
        constexpr float kMaxHitDistance = 250.0F;

        Shared::Entities::HorseEntity *FindHorse(std::uint64_t networkId) {
            auto *replication = Framework::CoreModules::GetReplication();
            return replication != nullptr ? replication->GetEntity<Shared::Entities::HorseEntity>(networkId) : nullptr;
        }
    } // namespace

    void HorseService::Install() {
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr) {
            return;
        }

        // Only the simulator's copy counts: it is the one whose soul runs the horse's stats, and the one dressed longest. Any other copy may be one that has just streamed in and not yet put the saddle on, whose bare-animal answer would overwrite the saddlebags'.
        net->RegisterRPC<Shared::RPC::HorseStats>([](const Shared::RPC::HorseStats &msg, MafiaNet::Packet *packet) {
            auto *horse = FindHorse(msg.horseNetworkId);
            if (horse == nullptr || packet == nullptr || horse->ownerGUID != MafiaNet::ToPeerGuid(packet->guid) || !std::isfinite(msg.inventoryCapacity) || msg.inventoryCapacity < 0.0F) {
                return;
            }

            if (std::fabs(horse->inventoryCapacity - msg.inventoryCapacity) < kCapacityEpsilon) {
                return;
            }

            horse->inventoryCapacity = msg.inventoryCapacity;
            Framework::Logging::GetLogger(kLogger)->info("Horse {} carries {:.2f}", horse->GetNetworkID(), horse->inventoryCapacity);

            // Deliberately not forced back to the owner. It has nothing to
            // learn here: the owner is the peer that read this capacity off
            // the animal's own soul in the first place.
        });

        net->RegisterRPC<Shared::RPC::HorseMount>([](const Shared::RPC::HorseMount &msg, MafiaNet::Packet *packet) {
            auto *rider = KCDC::Features::Player::Players::FromPacket(packet);
            auto *horse = FindHorse(msg.horseNetworkId);
            if (rider == nullptr || horse == nullptr) {
                return;
            }

            const std::uint64_t riderNetworkId = rider->GetNetworkID();

            if (msg.mounted) {
                // Stepping straight from one saddle to another: the client
                // reports both edges, but a lost dismount would otherwise
                // leave the first horse owned by a rider who is not on it.
                auto *previous = Horses::UnseatPlayer(riderNetworkId);
                if (previous != nullptr && previous != horse) {
                    Scripting::Events::HorseDismounted(previous->GetNetworkID(), riderNetworkId);
                }

                // Three refusals, one answer: the player's own game has
                // already seated them, so each tells that client to get back
                // off. Somebody else's saddle is not free -- two players
                // mounting inside one tick both succeed locally. A corpse
                // carries nobody. And a script may refuse the ride.
                const char *refusal = nullptr;
                if (horse->riderNetworkId != 0 && horse->riderNetworkId != riderNetworkId) {
                    refusal = "somebody else is riding it";
                }
                else if (!horse->alive) {
                    refusal = "it is dead";
                }
                else if (!Scripting::Events::HorseMounting(horse->GetNetworkID(), riderNetworkId)) {
                    refusal = "a horseMounting handler refused it";
                }

                if (refusal != nullptr) {
                    Framework::Logging::GetLogger(kLogger)->info("Player '{}' may not ride horse {}: {}", rider->nickname, horse->GetNetworkID(), refusal);
                    Horses::OrderOff(horse, riderNetworkId);
                    return;
                }

                Horses::SetRider(horse, riderNetworkId, MafiaNet::ToPeerGuid(packet->guid), msg.instant, 0);
                Framework::Logging::GetLogger(kLogger)->info("Player '{}' mounted horse {}", rider->nickname, horse->GetNetworkID());
                Scripting::Events::HorseMounted(horse->GetNetworkID(), riderNetworkId);
                return;
            }

            // Only the rider may report their own dismount.
            if (horse->riderNetworkId != riderNetworkId) {
                return;
            }

            Horses::SetRider(horse, 0, MafiaNet::UNASSIGNED_PEER_GUID, false, msg.dismountSide);
            Framework::Logging::GetLogger(kLogger)->info("Player '{}' dismounted horse {}", rider->nickname, horse->GetNetworkID());
            Scripting::Events::HorseDismounted(horse->GetNetworkID(), riderNetworkId);
        });

        // An attacker's own game hit its copy of a horse. The attacker is the
        // sending connection, never the message; the claim is checked against
        // where this server has both of them before it goes anywhere.
        net->RegisterRPC<Shared::RPC::HorseHit>([](const Shared::RPC::HorseHit &msg, MafiaNet::Packet *packet) {
            auto *attacker = KCDC::Features::Player::Players::FromPacket(packet);
            auto *horse    = FindHorse(msg.horseNetworkId);
            if (!msg.IsSane() || attacker == nullptr || horse == nullptr || !horse->alive) {
                return;
            }
            if (!MafiaNet::VirtualWorldsCanSee(horse->GetVirtualWorld(), attacker->GetVirtualWorld()) || glm::distance(horse->position, attacker->position) > kMaxHitDistance) {
                return;
            }
            // The simulator's own player hits its own copy, which is the one
            // that counts; a report from there is a client out of step.
            if (horse->ownerGUID == MafiaNet::ToPeerGuid(packet->guid)) {
                return;
            }

            Horses::Hit(horse, attacker->GetNetworkID(), msg.amount, Shared::Game::ToDamageReason(msg.reason));
        });

        // What the simulator's soul took. Only the simulator may say, and the
        // attacker it names has to be a player this server knows.
        net->RegisterRPC<Shared::RPC::HorseDamaged>([](const Shared::RPC::HorseDamaged &msg, MafiaNet::Packet *packet) {
            auto *horse = FindHorse(msg.horseNetworkId);
            if (!msg.IsSane() || horse == nullptr || packet == nullptr || horse->ownerGUID != MafiaNet::ToPeerGuid(packet->guid)) {
                return;
            }

            const std::uint64_t attackerNetworkId = KCDC::Features::Player::Players::Find(msg.attackerNetworkId) != nullptr ? msg.attackerNetworkId : 0;
            Scripting::Events::HorseDamaged(horse->GetNetworkID(), attackerNetworkId, msg.amount, Shared::Game::ToDamageReason(msg.reason));
        });

        // What the rider's copy of the horse now wears. The rider is the one player the game lets at a horse's inventory; anybody else's copy only ever differs because its engine did something, and a word from it would strip the horse everywhere. Checked by the seat rather than by who simulates: the report can beat the pin that makes the rider the simulator by a round trip.
        net->RegisterRPC<Shared::RPC::HorseGearReport>([](const Shared::RPC::HorseGearReport &msg, MafiaNet::Packet *packet) {
            auto *horse    = FindHorse(msg.horseNetworkId);
            auto *reporter = KCDC::Features::Player::Players::FromPacket(packet);
            if (!msg.IsSane() || horse == nullptr || reporter == nullptr || horse->riderNetworkId == 0 || horse->riderNetworkId != reporter->GetNetworkID()) {
                return;
            }

            Horses::ReportGear(horse, reporter->GetNetworkID(), msg.gear, msg.basedOnRevision);
        });

        // The simulator's soul died. The same rule as its damage: only the
        // simulator may say, and only a known player may be named.
        net->RegisterRPC<Shared::RPC::HorseDied>([](const Shared::RPC::HorseDied &msg, MafiaNet::Packet *packet) {
            auto *horse = FindHorse(msg.horseNetworkId);
            if (!msg.IsSane() || horse == nullptr || packet == nullptr || horse->ownerGUID != MafiaNet::ToPeerGuid(packet->guid)) {
                return;
            }
            const std::uint64_t killerNetworkId = KCDC::Features::Player::Players::Find(msg.killerNetworkId) != nullptr ? msg.killerNetworkId : 0;
            Horses::AnnounceDeath(horse, killerNetworkId, Shared::Game::ToDamageReason(msg.reason));
        });
    }
} // namespace KCDC::Features::Horse
