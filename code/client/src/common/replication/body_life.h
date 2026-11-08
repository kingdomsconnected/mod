#pragma once

#include "sdk/cryengine/entitysystem/i_entity.h"
#include "sdk/wh/entitymodule/c_actor.h"
#include "sdk/wh/rpgmodule/c_soul.h"
#include "sdk/wh/rpgmodule/c_soul_state_effect.h"
#include "sdk/wh/rpgmodule/c_soul_state_storage.h"
#include "sdk/wh/rpgmodule/e_soul_state.h"

#include <cstdint>
#include <unordered_map>
#include <unordered_set>

namespace KCDC::Common::Replication {
    /**
     * What a replicated body tells the shared life layer about itself.
     *
     * Implemented by the replica that owns the body -- a player, an NPC, a
     * horse -- because it already knows who decides this copy's life and
     * where a blow has to go.
     */
    class IReplicatedLife {
      public:
        virtual ~IReplicatedLife() = default;

        /** Which replica this is, for the log. */
        virtual std::uint64_t LifeNetworkId() const = 0;

        /**
         * Whether this copy's own engine decides its life: the owner of a
         * player's body, the client simulating a horse. Everywhere else the
         * copy only mirrors a verdict made somewhere else -- another client's
         * engine, or the server's ledger.
         */
        virtual bool DecidesOwnLife() const = 0;

        /**
         * A blow the local player dealt this copy while it only mirrors. The
         * engine resolved it here, against this copy's armour and soul, so the
         * amount is real -- but this soul is not the one that counts, and the
         * blow has to reach whoever decides.
         */
        virtual void OnLocalBlow(float amount, std::uint8_t reason) = 0;

        /** What this copy's soul took while it decides its own life, and from whom (0 for nobody). */
        virtual void OnOwnDamage(std::uint64_t attackerNetworkId, float amount, std::uint8_t reason) = 0;

        /**
         * The engine declared this copy's soul dead: its own death dispatch,
         * the one edge every declared death passes through. The place to learn
         * of a death -- never a poll of `IsDead`.
         */
        virtual void OnSoulDied() {}
    };

    /**
     * The one place a replicated body's life meets the engine.
     *
     * Every kind of remote body used to reach the soul its own way: a player
     * copy replayed its owner's numbers into storage and called
     * `HandleDeath` itself, an NPC wrote a health cache the soul never reads,
     * a horse filtered writes and killed its soul. Three paths, none of which
     * let the engine play a death end to end on every copy. This is the one
     * path they all take now:
     *
     * - **Health writes** are filtered at the soul-state storage writer. A
     *   copy that decides its own life keeps them and reports what it took. A
     *   mirror keeps none -- two souls deciding one body's life would kill it
     *   on two schedules -- and hands the local player's blows to whoever
     *   decides.
     * - **A kill is a blow.** The dead action plays nothing -- slot 31 only
     *   sets the dying/dead actor state -- and a body falls because the blow
     *   that killed it is reacted to: the death fragment and the `Ragdoll`
     *   clip it carries. So a verdict kills through `I_CombatSoul::TakeDamage`
     *   (0x180EE031C), the game's own damage primitive and what Lua's
     *   `soul:DealDamage` reaches: it runs `C_HitCalculation`, submits the
     *   event that declares the death, and hands an `S_CombatHitRecord` to
     *   the game rules for the reaction, matching the path taken by the copy
     *   that resolved the real blow.
     * - **Only a death with no blow is played by hand.** A player copy's
     *   storage already holds its owner's zero, so no blow can declare it; it
     *   gets `HandleDeath`, and -- having no hit to pick a death fragment from
     *   -- the ragdoll that fragment would have carried, asked for when its
     *   dead action enters. So does a body the lethal blow finds still in the
     *   reaction to an earlier one, which the engine gives no ragdoll.
     * - **Removal** of a corpse stands it back to profile 1 first. Removing
     *   an actor that is still a ragdoll faults in `CEntitySystem::RemoveEntity`.
     *
     * Game thread only.
     */
    class BodyLife final {
      public:
        static BodyLife &Get();

        // --- Registration ---

        /** States that `soul` is `body`'s. Restated every frame; a soul that changed replaces the old entry. */
        void Bind(const SDK::wh::rpgmodule::C_Soul *soul, IReplicatedLife *body);

        /** Forgets `body`, for a replica losing its native body or going away. */
        void Unbind(const IReplicatedLife *body);

        /** The body `soul` belongs to, or null. */
        IReplicatedLife *Find(const SDK::wh::rpgmodule::C_Soul *soul) const;

        // --- The engine's edges, from the hooks ---

        /**
         * A health write reached the soul-state storage writer. True drops
         * it. See the class comment for who keeps what.
         */
        bool FilterHealthWrite(SDK::wh::rpgmodule::C_SoulStateStorage18070F3E8 *storage, SDK::wh::rpgmodule::E_SoulState state, float value, const SDK::wh::rpgmodule::C_SoulStateEffect *cause);

        /** The engine's death dispatch reached `soul`; its body is told. */
        void NotifySoulDied(const SDK::wh::rpgmodule::C_Soul *soul);

        /** The engine's dead action entered on `actor`; a body owed its ragdoll gets it. */
        void NotifyDeadActionEntered(SDK::wh::entitymodule::C_Actor *actor);

        // --- Verdicts ---

        /**
         * Kills `actor` with a lethal blow from nobody, or plays `HandleDeath`
         * on a soul whose storage already says dead. Nobody, because a verdict
         * is not a blow this machine saw: naming a player would run the game's
         * attacker-side bookkeeping for a strike that happened elsewhere.
         */
        void Kill(SDK::wh::entitymodule::C_Actor *actor);

        /** Brings a dead body back to `health`, in place. */
        void Revive(SDK::wh::entitymodule::C_Actor *actor, float health);

        /** Moves a living soul's health to `health` through its own setter. */
        void SetHealth(SDK::wh::entitymodule::C_Actor *actor, float health);

        /**
         * A blow resolved on another client, applied to the copy that decides
         * and reported with the attacker and reason that came with it. A
         * lethal one is a `TakeDamage` blow, so the death is reacted to like
         * any other; a lesser one is written exactly, because it was already
         * resolved against this body's armour on the attacker's machine and
         * `TakeDamage` would take it off a second time.
         */
        void ApplyBlow(SDK::wh::entitymodule::C_Actor *actor, std::uint64_t attackerNetworkId, float amount, std::uint8_t reason);

        // --- The body ---

        /**
         * What a mod-spawned actor needs and does not get by itself: a
         * pre-physics pass that does not wait on an AI activation, and a
         * collider. True once both hold; asked every frame until then.
         */
        static bool BringUp(SDK::wh::entitymodule::C_Actor *actor, SDK::IEntity *entity);

        /** Stands a corpse back up before its entity is removed. */
        static void PrepareForRemoval(SDK::wh::entitymodule::C_Actor *actor);

      private:
        /** Health through `C_Soul::SetState`, which drops a write of exactly zero. */
        static void WriteHealth(SDK::wh::rpgmodule::C_Soul *soul, float health);

        /** A blow no armour survives, from `killerNetworkId`'s soul, through the game's own damage primitive. */
        static void DealLethalBlow(SDK::wh::rpgmodule::C_Soul *soul, std::uint64_t killerNetworkId);

        /** A replicated player's soul by NetworkID, the local player's included; null for 0 or nobody. */
        static SDK::wh::rpgmodule::I_Soul *SoulOfPlayer(std::uint64_t playerNetworkId);

        /** The local player's NetworkID, or 0. */
        static std::uint64_t LocalPlayerNetworkId();

        /** A replicated player's NetworkID from their soul's WUID, `localPlayerNetworkId` for the local soul. */
        static std::uint64_t PlayerNetworkIdOfSoul(std::uint64_t soulWuid, std::uint64_t localPlayerNetworkId);

        std::unordered_map<const SDK::wh::rpgmodule::C_Soul *, IReplicatedLife *> _bodies;

        // Souls owed the ragdoll their death would have carried: told
        // `HandleDeath` with no blow behind it, or dealt the lethal blow while
        // still in the reaction to an earlier one.
        std::unordered_set<const SDK::wh::rpgmodule::C_Soul *> _owedRagdolls;

        // Set while this layer writes on a verdict's behalf, so its own writes
        // land on any copy and report nothing -- or, for a forwarded blow,
        // report the attacker that came with it.
        bool _writingVerdict           = false;
        bool _writingBlow              = false;
        std::uint64_t _blowAttacker    = 0;
        std::uint8_t _blowReason       = 0;
    };
} // namespace KCDC::Common::Replication
