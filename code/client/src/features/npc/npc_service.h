#pragma once

#include "core/feature_registry.h"
#include "features/player/player_duel_roster.h"

#include "sdk/types.h"
#include "sdk/wh/rpgmodule/c_soul.h"

#include "shared/features/npc/npc_rpc.h"
#include "shared/game/animation_tags.h"

#include <mafianet/get_time.h>
#include <networking/replication/replication_manager.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <vector>

namespace KCDC::Features::Npc {
    class Npc;

    /**
     * Client owner of every server-owned NPC on this machine.
     *
     * One tick drives every NPC replica, in the two roles `Npc` describes. What
     * belongs here rather than on a replica is what spans more than one of them:
     * the tick, the interaction key, and the one-shot RPCs, which land on the
     * network thread and have to be carried out on the game thread.
     *
     * The per-client simulation budget is the server's: the framework's
     * `DelegationManager` refuses to hand this client more than it was told to
     * take, so nothing here has to count.
     */
    class NpcService final: public KCDC::Core::IFeatureService {
      public:
        static NpcService &Get();
        void SetPreviewBody(SDK::EntityId entity, const void *mind) {
            _previewEntity.store(entity);
            _previewMind.store(mind);
        }

        /** Registers the one-shot RPC. Call once at startup. */
        static void Install();

        void Update() override;
        void Reset() override;

        /**
         * The level changed under us: every body is gone, and the replicas have
         * to forget theirs without touching it.
         */
        void NativeContextChanged() override;

        // --- The replicas ------------------------------------------------------

        /**
         * The name the NPC with this NetworkID is called, or null.
         *
         * Answered for the localization detour, which is how a name written on
         * the server reaches the game's own HUD: the soul carries a key rather
         * than text, and the key carries the NetworkID, so this resolves the
         * live replica rather than anything cached. Null lets the engine answer
         * instead of painting a key over what it would have said.
         */
        static const char *ResolveName(std::uint64_t networkId);

        /**
         * Whether this entity is the body of a server-owned NPC.
         *
         * Asked from the combat-entry detour, which is handed a fighter and has
         * to decide whether its reactions belong to it or to the server. Walks
         * the replicas rather than a table of its own, for the reason the absent
         * table below gives.
         */
        static bool IsServerOwnedBody(SDK::EntityId entityId);

        /**
         * One NPC body as of the last tick: what the combat, missile and damage
         * detours need to know about it without walking the replicas. Built on
         * the game thread and published whole, because those detours run on
         * the engine's worker threads as well.
         */
        struct CombatBody {
            SDK::EntityId entityId                 = 0;
            std::uint64_t networkId                = 0;
            const SDK::wh::rpgmodule::I_Soul *soul = nullptr;
            std::uint64_t soulWuid                 = 0;

            /** The attack's target body; 0 when the NPC is not attacking. */
            SDK::EntityId targetEntityId = 0;

            /** Whether this client is the body's simulator. */
            bool simulating = false;

            /** Whether this client runs a bow attack on it. */
            bool shooting = false;
        };

        /** The body with this entity, this soul or this soul's WUID, as of the last tick. */
        static std::optional<CombatBody> FindCombatBody(SDK::EntityId entityId);
        static std::optional<CombatBody> FindCombatBodyOfSoul(const SDK::wh::rpgmodule::I_Soul *soul);
        static std::optional<CombatBody> FindCombatBodyOfSoulWuid(std::uint64_t soulWuid);

        /** Whether this body may enter combat mode: it attacks, or it is attacked by one that does. */
        static bool IsCombatEnabled(SDK::EntityId entityId);

        /** Whether these two bodies are an attack and its target, in either order. */
        static bool IsCombatPair(SDK::EntityId first, SDK::EntityId second);

        /**
         * Whether this entity is the body of a server-owned NPC players may
         * talk to.
         *
         * Asked from the native interaction menu's detours: the game offers
         * its own Talk entry on these bodies and no Rob entry, whatever its
         * dialogue graph and pickpocket state would say.
         */
        static bool IsInteractableBody(SDK::EntityId entityId);

        /**
         * Whether the brain of this AI-side record is held: it belongs to a
         * server-owned NPC, and this client has not released it to a native
         * flee (`Npc::IsBrainReleased`).
         *
         * Asked from the brain detour, which is handed a brain and can reach only
         * its `C_NPC`. Compared on the pointer because that is all a brain knows.
         */
        static bool IsHeldBrainOwner(const void *npcRecord);

        /** Network thread: queues a one-shot for the next tick. */
        void Receive(const Shared::RPC::NpcAction &action);

        /**
         * The NPC the local player is looking at and close enough to talk to, or
         * null. Public because the nametag pass and the action-button prompt want
         * the same answer this does.
         */
        Npc *FindInteractionTarget() const;

        /**
         * The player chose the game's own Talk entry on this body. Sends the
         * same interaction the mod's key does, so the server cannot tell the
         * two apart. Game thread only.
         */
        void InteractWith(SDK::EntityId entityId);

      private:
        std::atomic<SDK::EntityId> _previewEntity {0};
        std::atomic<const void *> _previewMind {nullptr};
        /** The replica whose body this is, if players may talk to it. */
        static Npc *FindInteractableBody(SDK::EntityId entityId);

        /** Watches the mod's interaction key and sends a press at whoever it faces. */
        void PollInteraction();

        /** Sends one interaction to the server, at most as often as it can act on one. */
        void SendInteraction(Npc *target);

        // Deliberately no table of replicas. Construction and teardown are
        // raised from the peer pump and the tick runs on the game thread, so a
        // vector maintained from both would be mutated under the walk -- which
        // is undefined, not merely racy. Replication's own iteration is the
        // shared structure, and every other feature here walks it the same way.

        /** Rebuilds the combat bodies from the replicas and publishes them. */
        void PublishCombatBodies(Framework::Networking::Replication::ReplicationManager &replication);

        // The tick's combat bodies: a table rebuilt whole on the game thread
        // and swapped in, never edited in place, which is what the detours
        // can read while the next one is filled.
        struct CombatBodies {
            std::vector<CombatBody> bodies;
        };
        KCDC::Features::Player::RosterRing<CombatBodies, 4> _combatBodies;

        // The one-shots that arrived since the last tick. The RPC lands on the
        // network thread, where neither the entity system nor Mannequin is safe
        // to touch.
        std::mutex _inboxMutex;
        std::vector<Shared::RPC::NpcAction> _inbox;

        // Rising-edge detection for the interact key, and a floor on how often a
        // press may be sent.
        bool _interactHeld              = false;
        MafiaNet::Time _nextInteraction = 0;
    };
} // namespace KCDC::Features::Npc
