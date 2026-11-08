#include "horse_verbs.h"
#include "horse_navigation.h"

#include "features/horse/horse_scripting.h"
#include "features/player/player_roster.h"

#include "shared/features/horse/horse_action.h"
#include "shared/features/horse/horse_buffs.h"
#include "shared/features/horse/horse_damage.h"
#include "shared/features/horse/horse_ride_order.h"
#include "shared/game/horse_catalog.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace KCDC::Features::Horse {
    namespace {
        constexpr const char *kLogger = "KCDCServer";

        // Broadcasts one horse action for every client to perform locally.
        void BroadcastAction(std::uint8_t type, std::uint64_t horseNetworkId, std::uint64_t attackerNetworkId) {
            auto *net = Framework::CoreModules::GetNetworkPeer();
            if (net == nullptr) {
                return;
            }

            Shared::RPC::HorseAction action;
            action.type              = type;
            action.horseNetworkId    = horseNetworkId;
            action.attackerNetworkId = attackerNetworkId;
            net->BroadcastRPC(action);
        }

        // Sends one saddle order to the client of the player it is about.
        bool SendRideOrder(std::uint64_t horseNetworkId, std::uint64_t playerNetworkId, bool mount, bool instant) {
            auto *net    = Framework::CoreModules::GetNetworkPeer();
            auto *player = KCDC::Features::Player::Players::Find(playerNetworkId);
            if (net == nullptr || player == nullptr || player->ownerGUID == MafiaNet::UNASSIGNED_PEER_GUID) {
                return false;
            }

            Shared::RPC::HorseRideOrder order;
            order.horseNetworkId = horseNetworkId;
            order.mount          = mount;
            order.instant        = instant;
            net->SendRPC(order, MafiaNet::ToGuid(player->ownerGUID));
            return true;
        }
    } // namespace

    Shared::Entities::HorseEntity *Horses::Spawn(const std::string &soul, const glm::vec3 &position, const glm::quat &rotation, const std::string &name, const Shared::Game::HorseGear *gear) {
        auto *replication = Framework::CoreModules::GetReplication();
        if (replication == nullptr) {
            return nullptr;
        }

        auto *horse = replication->CreateEntity<Shared::Entities::HorseEntity>();
        if (horse == nullptr) {
            return nullptr;
        }

        // A breed name is resolved here, once, so the wire only ever carries a soul GUID; anything else is taken as one.
        const Shared::Game::HorseBreedEntry *breed = Shared::Game::FindHorseBreed(soul);
        horse->soulGuid                            = breed != nullptr ? std::string(breed->soulGuid) : soul.empty() ? Shared::Entities::HorseEntity::kDefaultSoulGuid : soul;
        if (breed == nullptr) {
            for (const Shared::Game::HorseBreedEntry &candidate : Shared::Game::g_HorseBreeds) {
                if (candidate.soulGuid == horse->soulGuid) {
                    breed = &candidate;
                    break;
                }
            }
        }

        // Before the entity replicates, so the name rides the construction snapshot rather than arriving a tick after the body.
        horse->name     = name;
        horse->position = position;

        // Dressed the way the game dresses its own horses unless the caller says otherwise: a spawned soul brings no tack of its own. On the construction snapshot, so every copy is born wearing it.
        const Shared::Game::HorseGear dressed = gear != nullptr ? *gear : DefaultGear(breed != nullptr ? breed->soulName : std::string_view(), horse->GetNetworkID());
        if (Shared::Game::ValidateHorseGear(dressed) == Shared::Game::HorseGearError::None) {
            horse->gear         = Shared::Game::PackHorseGear(dressed);
            horse->gearRevision = 1;
        }
        horse->rotation = rotation;
        horse->SetVirtualWorld(MafiaNet::VIRTUAL_WORLD_GLOBAL);

        // Range culling stays on, unlike a player's body: a horse is placed at a real pose the moment it is created, so the interest grid has something true to cull against from the start.

        Framework::Logging::GetLogger(kLogger)->info("Spawned horse {} (soul '{}', name '{}') at {:.2f}, {:.2f}, {:.2f}", horse->GetNetworkID(), horse->soulGuid, horse->name, position.x, position.y, position.z);

        Scripting::Events::HorseSpawned(horse->GetNetworkID());
        return horse;
    }

    // --- Gear -------------------------------------------------------------------

    Shared::Game::HorseGear Horses::DefaultGear(std::string_view soulName, std::uint64_t networkId) {
        // A named horse's own tack shares its soul's name; any other horse draws one child of equipment_horse_common, as the game's OneChild preset does, picked from the NetworkID so every copy agrees.
        const Shared::Game::HorseGearPresetEntry *preset = soulName.empty() ? nullptr : Shared::Game::FindHorseGearPreset(soulName);
        if (preset == nullptr && !Shared::Game::g_CommonHorseGearPresets.empty()) {
            preset = Shared::Game::FindHorseGearPreset(Shared::Game::g_CommonHorseGearPresets[networkId % Shared::Game::g_CommonHorseGearPresets.size()]);
        }

        Shared::Game::HorseGear gear;
        if (preset == nullptr || Shared::Game::HorseGearFromPreset(*preset, gear) != Shared::Game::HorseGearError::None) {
            return {};
        }
        return gear;
    }

    Shared::Game::HorseGear Horses::GearOf(const Shared::Entities::HorseEntity *horse) {
        Shared::Game::HorseGear gear;
        if (horse != nullptr) {
            // Only ever written through PackHorseGear from a validated set, so this cannot fail; a record that somehow did reads as bare.
            Shared::Game::UnpackHorseGear(horse->gear, gear);
        }
        return gear;
    }

    bool Horses::SetGear(Shared::Entities::HorseEntity *horse, const Shared::Game::HorseGear &gear, std::uint64_t playerNetworkId) {
        if (horse == nullptr || Shared::Game::ValidateHorseGear(gear) != Shared::Game::HorseGearError::None) {
            return false;
        }

        // Every copy already wears it; a revision would only make each one check.
        const std::string packed = Shared::Game::PackHorseGear(gear);
        if (packed == horse->gear) {
            return true;
        }

        horse->gear = packed;
        PublishGear(horse);
        Framework::Logging::GetLogger(kLogger)->info("Horse {} now wears {} piece(s) of gear", horse->GetNetworkID(), packed.size() / Shared::Game::kPackedHorseGearWidth);
        Scripting::Events::HorseGearChanged(horse->GetNetworkID(), playerNetworkId);
        return true;
    }

    void Horses::ReportGear(Shared::Entities::HorseEntity *horse, std::uint64_t riderNetworkId, const std::string &packed, std::uint32_t basedOnRevision) {
        // Made before the copy saw the server's latest write: that write is already on its way to it and will dress it over whatever it has, so there is nothing to answer.
        if (horse == nullptr || basedOnRevision != horse->gearRevision || packed == horse->gear) {
            return;
        }

        Shared::Game::HorseGear gear;
        const Shared::Game::HorseGearError error = Shared::Game::UnpackHorseGear(packed, gear);

        std::string refusal;
        if (error != Shared::Game::HorseGearError::None) {
            refusal = Shared::Game::HorseGearErrorText(error);
        }
        else if (horse->gearLocked) {
            refusal = "its gear is locked";
        }
        else if (!Scripting::Events::HorseGearChanging(horse->GetNetworkID(), riderNetworkId, gear)) {
            refusal = "a horseGearChanging handler refused it";
        }

        if (!refusal.empty()) {
            Framework::Logging::GetLogger(kLogger)->info("Refused a gear change on horse {} from player {}: {}", horse->GetNetworkID(), riderNetworkId, refusal);

            // The record stands, and is published again so the copy that changed is dressed back in it.
            PublishGear(horse);
            return;
        }

        SetGear(horse, gear, riderNetworkId);
    }

    void Horses::PublishGear(Shared::Entities::HorseEntity *horse) {
        horse->gearRevision = Shared::Entities::HorseEntity::AdvanceRevision(horse->gearRevision);
        horse->ForceState();
    }

    // --- Buffs ----------------------------------------------------------------

    std::vector<Shared::RPC::BuffId> Horses::BuffsOf(const Shared::Entities::HorseEntity *horse) {
        std::vector<Shared::RPC::BuffId> buffs;
        if (horse != nullptr) {
            Shared::Game::UnpackHorseBuffs(horse->buffs, buffs);
        }
        return buffs;
    }

    bool Horses::AddBuff(Shared::Entities::HorseEntity *horse, const Shared::Game::BuffEntry &buff) {
        if (horse == nullptr) {
            return false;
        }
        std::vector<Shared::RPC::BuffId> buffs = BuffsOf(horse);
        const Shared::RPC::BuffId id           = Shared::Game::BuffIdOf(buff);
        if (std::find(buffs.begin(), buffs.end(), id) != buffs.end()) {
            return true;
        }
        if (buffs.size() >= Shared::Entities::HorseEntity::kMaxBuffs) {
            Framework::Logging::GetLogger(kLogger)->warn("Horse {} already carries {} buffs; '{}' refused", horse->GetNetworkID(), buffs.size(), buff.name);
            return false;
        }
        buffs.push_back(id);
        PublishBuffs(horse, buffs);
        Framework::Logging::GetLogger(kLogger)->info("Horse {} given buff '{}' ({} on it)", horse->GetNetworkID(), buff.name, buffs.size());
        return true;
    }

    bool Horses::RemoveBuff(Shared::Entities::HorseEntity *horse, const Shared::Game::BuffEntry &buff) {
        if (horse == nullptr) {
            return false;
        }
        std::vector<Shared::RPC::BuffId> buffs = BuffsOf(horse);
        const auto erased                      = std::erase(buffs, Shared::Game::BuffIdOf(buff));
        if (erased == 0) {
            return false;
        }
        PublishBuffs(horse, buffs);
        Framework::Logging::GetLogger(kLogger)->info("Horse {} relieved of buff '{}' ({} on it)", horse->GetNetworkID(), buff.name, buffs.size());
        return true;
    }

    bool Horses::HasBuff(const Shared::Entities::HorseEntity *horse, const Shared::Game::BuffEntry &buff) {
        const auto buffs = BuffsOf(horse);
        return std::find(buffs.begin(), buffs.end(), Shared::Game::BuffIdOf(buff)) != buffs.end();
    }

    bool Horses::ClearBuffs(Shared::Entities::HorseEntity *horse) {
        if (horse == nullptr || horse->buffs.empty()) {
            return false;
        }
        PublishBuffs(horse, {});
        Framework::Logging::GetLogger(kLogger)->info("Horse {} relieved of every server buff", horse->GetNetworkID());
        return true;
    }

    void Horses::PublishBuffs(Shared::Entities::HorseEntity *horse, const std::vector<Shared::RPC::BuffId> &buffs) {
        horse->buffs        = Shared::Game::PackHorseBuffs(buffs);
        horse->buffRevision = Shared::Entities::HorseEntity::AdvanceRevision(horse->buffRevision);
        horse->ForceState();
    }

    void Horses::Destroy(Shared::Entities::HorseEntity *horse) {
        if (horse == nullptr) {
            return;
        }

        // Nobody is ordered off: the rider's client abandons the ride itself when the body despawns, and an order would name a horse that no longer exists by the time it lands.
        TakeRiderOff(horse, false);

        // While the entity is still here, so a handler can still read it.
        Scripting::Events::HorseDestroyed(horse->GetNetworkID());
        // A final script callback can issue another order while the handle
        // still resolves. Retire navigation after all such callbacks.
        HorseNavigation::Get().Forget(horse->GetNetworkID());

        if (auto *replication = Framework::CoreModules::GetReplication()) {
            replication->DestroyEntity(horse);
        }
    }

    bool Horses::SetOwner(Shared::Entities::HorseEntity *horse, std::uint64_t playerNetworkId) {
        if (horse == nullptr || (playerNetworkId != 0 && KCDC::Features::Player::Players::Find(playerNetworkId) == nullptr)) {
            return false;
        }
        if (horse->ownerNetworkId == playerNetworkId) {
            return true;
        }

        const std::uint64_t previous = horse->ownerNetworkId;
        horse->ownerNetworkId        = playerNetworkId;

        // A server field, so the field channel tells every copy but the simulator's, and the push tells that one.
        horse->ForceState();

        Framework::Logging::GetLogger(kLogger)->info("Horse {} handed from player {} to player {}", horse->GetNetworkID(), previous, playerNetworkId);
        Scripting::Events::HorseOwnerChanged(horse->GetNetworkID(), playerNetworkId);
        return true;
    }

    void Horses::OwnerLeft(std::uint64_t playerNetworkId) {
        auto *replication = Framework::CoreModules::GetReplication();
        if (replication == nullptr || playerNetworkId == 0) {
            return;
        }

        // Collected first: destroying an entity from inside the walk would pull it out from under the iteration.
        std::vector<Shared::Entities::HorseEntity *> owned;
        replication->ForEach<Shared::Entities::HorseEntity>([&owned, playerNetworkId](Shared::Entities::HorseEntity *horse) {
            if (horse->ownerNetworkId == playerNetworkId) {
                owned.push_back(horse);
            }
        });

        for (Shared::Entities::HorseEntity *horse : owned) {
            if (horse->destroyWithOwner) {
                Destroy(horse);
            }
            else {
                SetOwner(horse, 0);
            }
        }
    }

    void Horses::Rename(Shared::Entities::HorseEntity *horse, const std::string &name) {
        if (horse == nullptr || horse->name == name) {
            return;
        }

        horse->name = name;

        // The key on the animal's soul is built from its NetworkID, so it is already right, and the localization hook reads this field back every time the engine resolves that key. The name is a server field, so the field channel carries it to every copy but the simulator's, and
        // the push carries it there.
        horse->ForceState();
        Framework::Logging::GetLogger(kLogger)->info("Horse {} renamed to '{}'", horse->GetNetworkID(), name);
    }

    void Horses::SetRider(Shared::Entities::HorseEntity *horse, std::uint64_t playerNetworkId, MafiaNet::PeerGuid riderGuid, bool seatedInstantly, std::uint8_t dismountSide) {
        auto *replication = Framework::CoreModules::GetReplication();
        if (horse == nullptr || replication == nullptr) {
            return;
        }

        if (playerNetworkId != 0) {
            HorseNavigation::Get().Hold(horse);
        }
        horse->riderNetworkId  = playerNetworkId;
        horse->seatedInstantly = playerNetworkId != 0 && seatedInstantly;
        horse->dismountSide    = playerNetworkId == 0 && dismountSide <= 2 ? dismountSide : 0;

        if (playerNetworkId == 0) {
            // The rider keeps simulating it until the next election, which usually leaves it with them: they are standing beside it. The push is what tells them the saddle is empty: the seat is a server field and they own the horse, so the field channel never would.
            replication->Delegation().Unpin(horse);
            horse->ForceState();
            return;
        }

        // Pinned rather than handed over by hand, or the next election would take the horse off its own rider. The push is unconditional: a pin that changed the owner has already sent one, but a rider who was already simulating the horse is owed the new record too, and a second
        // push only costs a message.
        replication->Delegation().Pin(horse, riderGuid);
        horse->ForceState();
    }

    Shared::Entities::HorseEntity *Horses::RiddenBy(std::uint64_t playerNetworkId) {
        auto *replication = Framework::CoreModules::GetReplication();
        if (replication == nullptr || playerNetworkId == 0) {
            return nullptr;
        }

        Shared::Entities::HorseEntity *ridden = nullptr;
        replication->ForEach<Shared::Entities::HorseEntity>([&ridden, playerNetworkId](Shared::Entities::HorseEntity *horse) {
            if (horse->riderNetworkId == playerNetworkId) {
                ridden = horse;
            }
        });
        return ridden;
    }

    Shared::Entities::HorseEntity *Horses::UnseatPlayer(std::uint64_t playerNetworkId) {
        auto *previous = RiddenBy(playerNetworkId);
        if (previous != nullptr) {
            SetRider(previous, 0, MafiaNet::UNASSIGNED_PEER_GUID, false, 0);
        }
        return previous;
    }

    std::uint64_t Horses::TakeRiderOff(Shared::Entities::HorseEntity *horse, bool orderClient) {
        if (horse == nullptr || horse->riderNetworkId == 0) {
            return 0;
        }

        const std::uint64_t riderNetworkId = horse->riderNetworkId;
        SetRider(horse, 0, MafiaNet::UNASSIGNED_PEER_GUID, false, 0);
        if (orderClient) {
            OrderOff(horse, riderNetworkId);
        }
        Scripting::Events::HorseDismounted(horse->GetNetworkID(), riderNetworkId);
        return riderNetworkId;
    }

    bool Horses::Mount(Shared::Entities::HorseEntity *horse, std::uint64_t playerNetworkId, bool instant) {
        if (horse == nullptr || !horse->alive || playerNetworkId == 0 || (horse->riderNetworkId != 0 && horse->riderNetworkId != playerNetworkId)) {
            return false;
        }
        return SendRideOrder(horse->GetNetworkID(), playerNetworkId, true, instant);
    }

    void Horses::OrderOff(Shared::Entities::HorseEntity *horse, std::uint64_t playerNetworkId) {
        if (horse != nullptr) {
            SendRideOrder(horse->GetNetworkID(), playerNetworkId, false, false);
        }
    }

    bool Horses::RearAndThrowDown(Shared::Entities::HorseEntity *horse) {
        if (horse == nullptr || horse->riderNetworkId == 0) {
            return false;
        }

        BroadcastAction(Shared::RPC::HorseAction::RearAndThrowDown, horse->GetNetworkID(), 0);
        return true;
    }

    bool Horses::PullDownRider(Shared::Entities::HorseEntity *horse, std::uint64_t attackerNetworkId) {
        if (horse == nullptr || horse->riderNetworkId == 0 || attackerNetworkId == 0) {
            return false;
        }

        BroadcastAction(Shared::RPC::HorseAction::PullDownRider, horse->GetNetworkID(), attackerNetworkId);
        return true;
    }

    // --- Life ---------------------------------------------------------------

    void Horses::PublishVitals(Shared::Entities::HorseEntity *horse) {
        horse->vitalsRevision = Shared::Entities::HorseEntity::AdvanceRevision(horse->vitalsRevision);
        horse->ForceState();
    }

    void Horses::Hit(Shared::Entities::HorseEntity *horse, std::uint64_t attackerNetworkId, float amount, Shared::Game::DamageReason reason) {
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (horse == nullptr || net == nullptr || !horse->alive || !std::isfinite(amount) || amount <= 0.0F) {
            return;
        }

        // Somebody is running the horse: their soul decides what the blow does, and their report raises the events.
        if (horse->ownerGUID != MafiaNet::UNASSIGNED_PEER_GUID) {
            Shared::RPC::HorseHit hit;
            hit.horseNetworkId    = horse->GetNetworkID();
            hit.attackerNetworkId = attackerNetworkId;
            hit.amount            = amount;
            hit.reason            = static_cast<std::uint8_t>(reason);
            net->SendRPC(hit, MafiaNet::ToGuid(horse->ownerGUID));
            return;
        }

        // Nobody near enough to simulate it, and no simulator ever reported what it can take: there is no ledger to write into.
        if (horse->maxHealth <= 0.0F) {
            return;
        }

        const float before = horse->health;
        horse->health      = std::max(0.0F, before - amount);
        Scripting::Events::HorseDamaged(horse->GetNetworkID(), attackerNetworkId, before - horse->health, reason);

        // The next simulator applies this to its own soul when it takes the horse on.
        if (horse->health <= 0.0F) {
            AnnounceDeath(horse, attackerNetworkId, reason);
            return;
        }
        horse->vitalsRevision = Shared::Entities::HorseEntity::AdvanceRevision(horse->vitalsRevision);
    }

    bool Horses::SetHealth(Shared::Entities::HorseEntity *horse, float value) {
        if (horse == nullptr || !horse->alive || horse->maxHealth <= 0.0F || !std::isfinite(value)) {
            return false;
        }
        if (value <= 0.0F) {
            return Kill(horse);
        }

        horse->health = std::min(value, horse->maxHealth);
        PublishVitals(horse);
        return true;
    }

    bool Horses::Kill(Shared::Entities::HorseEntity *horse) {
        if (horse == nullptr || !horse->alive) {
            return false;
        }

        Framework::Logging::GetLogger(kLogger)->info("Horse {} killed by the server", horse->GetNetworkID());
        AnnounceDeath(horse, 0, Shared::Game::DamageReason::Scripted);
        return true;
    }

    bool Horses::Revive(Shared::Entities::HorseEntity *horse) {
        if (horse == nullptr || horse->alive) {
            return false;
        }

        horse->alive           = true;
        horse->health          = horse->maxHealth;
        horse->killerNetworkId = 0;
        horse->deathReason     = 0;
        horse->deathAnnounced  = false;
        PublishVitals(horse);
        Framework::Logging::GetLogger(kLogger)->info("Horse {} revived", horse->GetNetworkID());
        return true;
    }

    bool Horses::Wake(Shared::Entities::HorseEntity *horse) {
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (horse == nullptr || net == nullptr || !horse->alive || !horse->unconscious || horse->ownerGUID == MafiaNet::UNASSIGNED_PEER_GUID) {
            return false;
        }

        // A request, not a verdict: the knockout is the simulator's soul's, as a blow is, so it goes where `Hit` sends one.
        Shared::RPC::HorseAction action;
        action.type           = Shared::RPC::HorseAction::Wake;
        action.horseNetworkId = horse->GetNetworkID();
        net->SendRPC(action, MafiaNet::ToGuid(horse->ownerGUID));
        Framework::Logging::GetLogger(kLogger)->info("Horse {} woken by the server", horse->GetNetworkID());
        return true;
    }

    void Horses::AnnounceDeath(Shared::Entities::HorseEntity *horse, std::uint64_t killerNetworkId, Shared::Game::DamageReason reason) {
        if (horse == nullptr || horse->deathAnnounced) {
            return;
        }
        horse->deathAnnounced  = true;
        horse->alive           = false;
        horse->health          = 0.0F;
        horse->killerNetworkId = killerNetworkId;
        horse->deathReason     = static_cast<std::uint8_t>(reason);

        // The verdict is the server's, so it is pushed with a revision of its own: the simulator kills its soul on it when the death was not its own, and a push made for anything else in the meantime -- the rider's dismount arriving first -- is superseded rather than left
        // standing.
        PublishVitals(horse);

        // A dead horse carries nobody: the rider's client ends the ride from its own death edge, and this makes the server's record agree.
        TakeRiderOff(horse, true);

        Framework::Logging::GetLogger(kLogger)->info("Horse {} died (killer {}, reason {})", horse->GetNetworkID(), killerNetworkId, static_cast<int>(reason));
        Scripting::Events::HorseDied(horse->GetNetworkID(), killerNetworkId, reason);
    }
} // namespace KCDC::Features::Horse
