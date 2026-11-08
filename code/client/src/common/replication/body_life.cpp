#include <utils/safe_win32.h>

#include "body_life.h"

#include "features/npc/npc.h"
#include "features/npc/npc_service.h"
#include "features/player/player.h"
#include "common/module_support.h"

#include "sdk/cryengine/action/i_game_object.h"
#include "sdk/wh/entitymodule/c_body_part_data.h"
#include "sdk/wh/entitymodule/c_ragdoll_manager.h"
#include "sdk/wh/rpgmodule/c_combat_soul.h"
#include "sdk/wh/rpgmodule/c_health_value_effect.h"

#include "shared/features/player/player_damage.h"

#include <logging/logger.h>

#include <algorithm>
#include <iterator>

namespace KCDC::Common::Replication {
    namespace {
        constexpr const char *kLogger = "BodyLife";

        // `C_RagdollManager`'s physics profiles, as the engine numbers them.
        constexpr std::uint8_t kPhysicsProfileNotPhysicalized = 0;
        constexpr std::uint8_t kPhysicsProfileAlive           = 1;

        // How many times a soul's maximum health a lethal blow deals: enough
        // that no armour `C_HitCalculation` applies leaves anything standing.
        constexpr float kLethalBlowMultiplier = 10.0F;
    } // namespace

    BodyLife &BodyLife::Get() {
        static BodyLife instance;
        return instance;
    }

    // --- Registration ---------------------------------------------------------

    void BodyLife::Bind(const SDK::wh::rpgmodule::C_Soul *soul, IReplicatedLife *body) {
        if (soul == nullptr || body == nullptr) {
            return;
        }
        const auto found = _bodies.find(soul);
        if (found != _bodies.end() && found->second == body) {
            return;
        }
        Unbind(body);
        _bodies[soul] = body;
    }

    void BodyLife::Unbind(const IReplicatedLife *body) {
        for (auto it = _bodies.begin(); it != _bodies.end();) {
            if (it->second != body) {
                ++it;
                continue;
            }
            _owedRagdolls.erase(it->first);
            it = _bodies.erase(it);
        }
    }

    IReplicatedLife *BodyLife::Find(const SDK::wh::rpgmodule::C_Soul *soul) const {
        const auto found = soul != nullptr ? _bodies.find(soul) : _bodies.end();
        return found != _bodies.end() ? found->second : nullptr;
    }

    // --- Health writes --------------------------------------------------------

    std::uint64_t BodyLife::LocalPlayerNetworkId() {
        auto *human = KCDC::Features::Player::Human::FindLocal();
        return human != nullptr ? human->GetNetworkID() : 0;
    }

    std::uint64_t BodyLife::PlayerNetworkIdOfSoul(std::uint64_t soulWuid, std::uint64_t localPlayerNetworkId) {
        if (soulWuid == 0) {
            return 0;
        }
        auto *local     = KCDC::Common::LocalHumanActor();
        auto *localSoul = local != nullptr ? local->GetSoul() : nullptr;
        if (localSoul != nullptr && localSoul->GetWuid() == soulWuid) {
            return localPlayerNetworkId;
        }
        auto *remote = KCDC::Features::Player::Human::FindRemoteSoulWuid(soulWuid);
        return remote != nullptr ? remote->GetNetworkID() : 0;
    }

    bool BodyLife::FilterHealthWrite(SDK::wh::rpgmodule::C_SoulStateStorage18070F3E8 *storage, SDK::wh::rpgmodule::E_SoulState state, float value, const SDK::wh::rpgmodule::C_SoulStateEffect *cause) {
        if (state != SDK::wh::rpgmodule::E_SoulState::Health || storage == nullptr) {
            return false;
        }

        // The backup storage shares its owner with the active one; only the
        // active one is the body's health.
        const SDK::wh::rpgmodule::C_Soul *soul = storage->GetSoul();
        IReplicatedLife *body                  = Find(soul);
        if (body == nullptr || storage != soul->GetActiveStateStorage()) {
            return false;
        }

        // A verdict this layer is writing lands on any copy and says nothing:
        // whoever decided it already knows.
        if (_writingVerdict) {
            return false;
        }

        const bool decides = body->DecidesOwnLife();
        const float taken  = storage->GetState(state) - std::max(value, 0.0F);
        if (decides && taken <= 0.0F) {
            return false;
        }
        if (decides && _writingBlow) {
            body->OnOwnDamage(_blowAttacker, taken, _blowReason);
            return false;
        }
        // Healing and regeneration on a mirror: nothing to hand on, and
        // nothing this copy may decide.
        if (taken <= 0.0F) {
            return true;
        }

        const SDK::wh::rpgmodule::C_HealthValueEffect *damage = SDK::wh::rpgmodule::C_HealthValueEffect::FromCause(state, cause);
        const std::uint64_t localPlayer                       = LocalPlayerNetworkId();
        const std::uint64_t attacker                          = damage != nullptr ? PlayerNetworkIdOfSoul(damage->GetAttackerSoulWuid(), localPlayer) : 0;
        const auto reason                                     = static_cast<std::uint8_t>(Shared::Game::ToDamageReason(damage != nullptr ? damage->GetDeathReason() : 0));

        if (damage != nullptr && KCDC::Features::Npc::NpcService::FindCombatBodyOfSoulWuid(damage->GetAttackerSoulWuid())) {
            // Native melee is withheld by the duel capture and settled by
            // the referee. A health write outside that path is stale.
            return true;
        }

        if (!decides) {
            // Not ours to decide. A blow the local player dealt still has to
            // land, so it goes to whoever does.
            if (attacker != 0 && attacker == localPlayer) {
                body->OnLocalBlow(taken, reason);
            }
            return true;
        }

        // A remote player's replayed swing or arrow hitting this copy: the
        // same blow already came, or is coming, from that player's own client.
        if (attacker != 0 && attacker != localPlayer) {
            return true;
        }

        // Drains -- bleeding, a fall with no cause -- carry no damage effect
        // and raise nothing, as for players; the death they cause still shows
        // through the replicated life.
        if (damage != nullptr) {
            body->OnOwnDamage(attacker, taken, reason);
        }
        return false;
    }

    void BodyLife::NotifySoulDied(const SDK::wh::rpgmodule::C_Soul *soul) {
        IReplicatedLife *body = Find(soul);
        if (body == nullptr) {
            return;
        }
        Framework::Logging::GetLogger(kLogger)->info("Body {}: the engine declared its death ({})", body->LifeNetworkId(), body->DecidesOwnLife() ? "decides" : "mirror");
        body->OnSoulDied();
    }

    void BodyLife::NotifyDeadActionEntered(SDK::wh::entitymodule::C_Actor *actor) {
        auto *soul = actor != nullptr ? actor->GetSoul() : nullptr;
        if (soul == nullptr || _owedRagdolls.erase(soul) == 0) {
            return;
        }
        if (auto *ragdoll = actor->GetRagdollManager(); ragdoll != nullptr && !ragdoll->IsRagdoll()) {
            ragdoll->RequestRagdoll(true);
        }
    }

    // --- Verdicts -------------------------------------------------------------

    void BodyLife::WriteHealth(SDK::wh::rpgmodule::C_Soul *soul, float health) {
        // `C_Soul::SetState` (0x1809DC648) opens with `if (state || |value| >
        // 1e-5)`, so a Health write of exactly zero is dropped. Anything below
        // zero passes and is clamped to zero inside.
        soul->SetState(SDK::wh::rpgmodule::E_SoulState::Health, health > 0.0F ? health : -1.0F);
    }

    SDK::wh::rpgmodule::I_Soul *BodyLife::SoulOfPlayer(std::uint64_t playerNetworkId) {
        if (playerNetworkId == 0) {
            return nullptr;
        }
        auto *human = KCDC::Common::FindReplicaById<KCDC::Features::Player::Human>(playerNetworkId);
        if (human == nullptr) {
            auto *npc   = KCDC::Common::FindReplicaById<KCDC::Features::Npc::Npc>(playerNetworkId);
            auto *actor = npc != nullptr ? npc->ResolveActor() : nullptr;
            return actor != nullptr ? actor->GetSoul() : nullptr;
        }
        auto *actor = human->IsOwner() ? static_cast<SDK::wh::entitymodule::C_Actor *>(KCDC::Common::LocalHumanActor()) : human->ResolveActor();
        return actor != nullptr ? actor->GetSoul() : nullptr;
    }

    void BodyLife::DealLethalBlow(SDK::wh::rpgmodule::C_Soul *soul, std::uint64_t killerNetworkId) {
        auto *combat = soul->GetCombatSoul();
        if (combat == nullptr) {
            return;
        }
        const float health = std::max(soul->GetState(SDK::wh::rpgmodule::E_SoulState::Health), soul->GetStateMax(SDK::wh::rpgmodule::E_SoulState::Health));

        // Stamina first: the binding's own descriptor at 0x183BE6768 reads
        // "stamina,health". The flag is left false, as in the game's battle
        // trees; the body part uses the binding's default when none is named.
        const SDK::wh::entitymodule::C_BodyPartData bodyPart;
        combat->TakeDamage(0.0F, health * kLethalBlowMultiplier, SoulOfPlayer(killerNetworkId), false, bodyPart);
    }

    void BodyLife::Kill(SDK::wh::entitymodule::C_Actor *actor) {
        auto *soul = actor != nullptr ? actor->GetSoul() : nullptr;
        if (soul == nullptr) {
            return;
        }

        // A soul whose storage already reads dead cannot be declared dead
        // again -- the storage writer declares only an alive-to-dead edge --
        // so the body is told directly, and owed its ragdoll.
        if (soul->IsDead()) {
            _owedRagdolls.insert(soul);
            actor->HandleDeath(true);
            return;
        }

        // A body still in the reaction to an earlier blow -- on a simulator,
        // its own native hit, a moment before the verdict on that blow -- is
        // given only its dead action for the lethal one: with no reaction of
        // its own, `ProcessActorHitRecord` asks for the kill's ragdoll only
        // while no reaction runs (0x180B24725). It is owed that ragdoll, the
        // one a copy with no reaction running falls into.
        if (actor->GetHitDeathReactions()->IsReacting()) {
            _owedRagdolls.insert(soul);
        }
        _writingVerdict = true;
        DealLethalBlow(soul, 0);
        _writingVerdict = false;
    }

    void BodyLife::Revive(SDK::wh::entitymodule::C_Actor *actor, float health) {
        auto *soul = actor != nullptr ? actor->GetSoul() : nullptr;
        if (soul == nullptr || !(health > 0.0F)) {
            return;
        }

        // A dead soul refuses its own setter, so this goes the way the
        // player's respawn does: the storage writer, which has the
        // dead-to-alive branch, then the actor's in-place resurrect.
        _writingVerdict = true;
        soul->GetActiveStateStorage()->SetState(SDK::wh::rpgmodule::E_SoulState::Health, health, nullptr);
        _writingVerdict = false;
        actor->Revive(SDK::wh::entitymodule::C_Actor::eReviveReason_Script);

        // `C_Actor::Revive` resets the ragdoll manager, and `Reset` only
        // records the profile the body is in: a body that died as a ragdoll
        // comes back animated with its articulated physics still standing in
        // for a collider, so nothing can hit it again. Profile 1 is the way
        // out the engine itself takes -- `SetPhysicsProfile(Alive)` blends out
        // of the ragdoll, restores the animated character and rebuilds the
        // collider -- as CryEngine's `CActor::Revive` asks for `eAP_Alive`.
        if (auto *object = actor->GetGameObject()) {
            object->SetAspectProfile(SDK::EEntityAspects::Physics, kPhysicsProfileAlive, true);
        }
    }

    void BodyLife::SetHealth(SDK::wh::entitymodule::C_Actor *actor, float health) {
        auto *soul = actor != nullptr ? actor->GetSoul() : nullptr;
        if (soul == nullptr || soul->IsDead()) {
            return;
        }
        _writingVerdict = true;
        WriteHealth(soul, health);
        _writingVerdict = false;
    }

    void BodyLife::ApplyBlow(SDK::wh::entitymodule::C_Actor *actor, std::uint64_t attackerNetworkId, float amount, std::uint8_t reason) {
        auto *soul = actor != nullptr ? actor->GetSoul() : nullptr;
        if (soul == nullptr || soul->IsDead() || !(amount > 0.0F)) {
            return;
        }

        _writingBlow  = true;
        _blowAttacker = attackerNetworkId;
        _blowReason   = reason;
        if (amount >= soul->GetState(SDK::wh::rpgmodule::E_SoulState::Health)) {
            DealLethalBlow(soul, attackerNetworkId);
        }
        else {
            WriteHealth(soul, soul->GetState(SDK::wh::rpgmodule::E_SoulState::Health) - amount);
        }
        _writingBlow = false;
    }

    // --- The body -------------------------------------------------------------

    bool BodyLife::BringUp(SDK::wh::entitymodule::C_Actor *actor, SDK::IEntity *entity) {
        auto *object = actor != nullptr ? actor->GetGameObject() : nullptr;
        if (object == nullptr || entity == nullptr || !actor->IsSettled()) {
            return false;
        }

        // An actor drives its movement controller and its actions from its
        // pre-physics pass, which `CGameObject::EvaluateUpdateActivation`
        // (0x1807FF6A0) grants on this rule. `WhenAIActivated`, what a
        // single-player build asks for, defers to `ShouldUpdateAI`; a remote
        // body has to keep running whether or not the AI LOD is looking at it.
        object->EnablePrePhysicsUpdate(SDK::EPrePhysicsUpdate::Always);

        // The collider, and only the collider. The update-slot aspect is left
        // alone: nothing in the shipped game sets it, and forcing it crashes.
        //
        // Stood down before being asked for Alive, because a fresh body reports
        // Alive while having no physical entity and the setter early-outs on a
        // matching profile. Guarded on the physical entity, not the profile.
        if (entity->GetPhysics() == nullptr) {
            object->SetAspectProfile(SDK::EEntityAspects::Physics, kPhysicsProfileNotPhysicalized, true);
            object->SetAspectProfile(SDK::EEntityAspects::Physics, kPhysicsProfileAlive, true);
        }
        return entity->GetPhysics() != nullptr;
    }

    void BodyLife::PrepareForRemoval(SDK::wh::entitymodule::C_Actor *actor) {
        // An actor removed while it is still a ragdoll faults inside
        // `CEntitySystem::RemoveEntity`: profiles 2 and 3 leave an articulated
        // physics body with the animated character stood down behind it and
        // the dead action holding listeners. Profile 1 blends out of the
        // ragdoll and restores the animated character.
        auto *soul    = actor != nullptr ? actor->GetSoul() : nullptr;
        auto *ragdoll = actor != nullptr ? actor->GetRagdollManager() : nullptr;
        const bool corpse = (soul != nullptr && soul->IsDead()) || (ragdoll != nullptr && ragdoll->IsRagdoll());
        if (!corpse) {
            return;
        }
        if (auto *object = actor->GetGameObject()) {
            object->SetAspectProfile(SDK::EEntityAspects::Physics, kPhysicsProfileAlive, true);
        }
    }
} // namespace KCDC::Common::Replication
