#pragma once

#include "features/npc/npc_movement_progress.h"
#include "features/player/player_nametag_policy.h"

#include "shared/features/npc/npc_entity.h"
#include "shared/features/npc/npc_inventory_message.h"
#include "shared/features/npc/npc_rpc.h"

#include "features/items/inventory_reconciliation.h"
#include "features/items/native_inventory_projection.h"
#include "common/replication/body_life.h"
#include "common/replication/pose_snapshot.h"
#include "common/spawn/actor_spawner.h"
#include "sdk/cryengine/animation/i_character_instance.h"
#include "sdk/types.h"
#include "sdk/wh/entitymodule/c_actor.h"
#include "sdk/wh/rpgmodule/c_soul.h"
#include "sdk/wh/xgenaimodule/c_npc_manager.h"

#include <mafianet/get_time.h>
#include <mafianet/replica_manager3.h>

#include <atomic>
#include <cstdint>
#include <optional>

namespace KCDC::Features::Npc {
    /**
     * Client binding for a server-owned NPC.
     *
     * The same replica runs in one of two roles, decided by nothing more than
     * `IsSimulating()`:
     *
     * - **Observer.** The body reproduces the wire: the pose it was reported at
     *   and the Mannequin tags the simulating engine chose, stated through the
     *   locomotion override (NpcLocomotionService). It decides nothing.
     *   This is the role most bodies are in most of the time, and the cheap one.
     *
     * - **Simulator.** This client was elected to run the body. It carries out
     *   the server's intent with the real engine, publishes the pose it ends up
     *   in, and reports how the order is going. Exactly one client is ever in
     *   this role for a given NPC, and the framework's `DelegationManager` is
     *   what guarantees it.
     *
     * `NpcService` owns the tick, the level change and the one-shot RPCs.
     */
    class Npc final: public Shared::Entities::NpcEntity, public KCDC::Common::Replication::IReplicatedLife {
      public:
        explicit Npc(bool localPreview = false): _localPreview(localPreview) {}
        ~Npc() override;

        bool IsSimulating() const {
            return _localPreview || Shared::Entities::NpcEntity::IsSimulating();
        }
        void StopLocalPreview(bool worldGone);
        bool PreviewSpawnFailed() const {
            return _spawnRefused;
        }
        bool PreviewReady() const {
            return IsBodyReady();
        }

        /** Whether the body is dressed, awake and animated: what anything that plays actions on it waits for. */
        bool IsBodyReady() const {
            return _bodyReady && IsPresentable(ResolveActor());
        }

        // --- Replica3 ---------------------------------------------------------

        void OnConstructed() override;
        void OnDeserialized(bool transformUpdated) override;
        void OnStateForced() override;
        void DeallocReplica(MafiaNet::Connection_RM3 *sourceConnection) override;

        // --- This body --------------------------------------------------------

        /** Runs whichever role this client is in. Game thread only. */
        void Frame();

        /** Forgets the body without touching it, for a world that has gone. */
        void Unbind();

        /** This body's AI-side record, or null before the spawn sink has run. */
        const void *GetNpcRecord() const {
            return _npcRecord;
        }

        SDK::EntityId GetEntityId() const {
            return _entityId;
        }

        /** The live actor behind this replica, or null before the body exists. */
        SDK::wh::entitymodule::C_Actor *ResolveActor() const;

        bool AcceptInventory(const nlohmann::json &snapshot);
        bool ApplyInventory();
        void NotifyNativeInventoryChange() {
            _stockApplied = false;
            _stockRetry.Reset();
        }
        bool InventoryReady() const {
            return _stockReady && _stockApplied;
        }
        bool StockKnown() const {
            return _stockReady;
        }
        std::uint64_t InventoryToken() const {
            return _stockInteraction.token;
        }
        bool InventoryBusyFor(std::uint64_t ownToken) const {
            return _stockInteraction.BusyFor(ownToken);
        }
        bool CanBeHarvested() const {
            return _stockReady && _harvestable && !_stockInteraction.harvested;
        }
        std::uint64_t InventoryRevision() const {
            return _stockRevision;
        }
        SDK::wh::entitymodule::C_Inventory *ProjectedInventory() const;
        const KCDC::Features::Items::NativeInventoryProjection &InventoryProjection() const {
            return _inventoryProjection;
        }
        bool ReadInitialInventory(nlohmann::json &rows) const;

        /** The live body `intent.targetNetworkId` names -- an attack's target, a flee's threat -- resolved afresh for body rebuilds. */
        SDK::wh::entitymodule::C_Actor *TargetActor() const;

        /** Whether the replica carries a live, unfrozen attack order, on whichever peer. */
        bool IsAttacking() const;

        /** Whether this client runs that attack: `IsAttacking` on its simulator. */
        bool HasAttackOrder() const;

        /** Whether this client hands the flee to the animal's own brain: a live, unfrozen animal's flee, on its simulator. */
        bool FleesNatively() const;

        /** Whether this client released the brain to a native flee and lets it think; what the brain gate asks. */
        bool IsBrainReleased() const {
            return _escapeRevision != 0;
        }

        /** The time observers render this body's pose at, which its combat replay waits for. */
        MafiaNet::Time CombatRenderTime();

        /** Plays a one-shot the server broadcast. */
        void PerformAction(const Shared::RPC::NpcAction &action);

        /**
         * The localization key this NPC's name is answered under.
         *
         * One word, because the native UI '@'-prefixes each whitespace-separated
         * word of what it is given and resolves them separately -- a key with a
         * space in it fails as two keys.
         */
        std::string LocalizationKey();

        // --- Life (IReplicatedLife) --------------------------------------------
        //
        // The server's ledger decides an NPC's life, so every copy -- the
        // simulator's included -- only mirrors it.

        std::uint64_t LifeNetworkId() const override {
            return const_cast<Npc *>(this)->GetNetworkID();
        }

        bool DecidesOwnLife() const override {
            return false;
        }

        /** Sent as `NpcDamage`; the server takes the attacker from the connection. */
        void OnLocalBlow(float amount, std::uint8_t reason) override;

        void OnOwnDamage(std::uint64_t, float, std::uint8_t) override {}

        /** Whether this NPC can be spoken to, for the interaction pass. */
        bool IsInteractable() const {
            return _entityId != 0 && alive && Shared::Game::HasFlag(flags, Shared::Game::NpcFlags::Interactable);
        }

      private:
        bool _localPreview = false;
        // --- Body lifecycle ---------------------------------------------------

        void RequestSpawn();
        void OnSpawned(KCDC::Common::Spawn::SpawnTicket &ticket, bool spawned);
        void ReleaseSpawnTicket();
        void Despawn();

        /** Binds an NPC that adopts one of the level's own bodies. */
        bool BindLevelBody();

        /** How far ApplyLook got, and whether asking again could do better. */
        enum class LookResult : std::uint8_t {
            Applied,
            /** Not this frame, but nothing says it cannot be the next one. */
            Retry,
            /** The engine refused; retrying would fail the same way. */
            Refused,
        };

        /** Souls, dresses and wakes the body. The order here is load-bearing. */
        LookResult ApplyLook();
        void ApplyHarvestedAppearance(SDK::wh::entitymodule::C_Actor *actor);

        /** Writes the replicated appearance into the body's look-as layer. */
        bool ApplyAppearance(SDK::wh::entitymodule::C_Actor *actor);

        /** Re-dresses the body when the wire says it wears something else. */
        void ApplyOutfit(SDK::wh::entitymodule::C_Actor *actor);

        /**
         * Points the soul's UI name at this NPC's key, so the game names the
         * body what the server called it.
         *
         * `C_Soul::GetUIName` is what every native path that names an actor
         * reads, and it holds a **localization key**, not text -- so the key
         * goes on the soul once and `NpcService::ResolveName` answers it out of
         * the replica at the localization detour. A rename therefore needs no
         * write at all: the key does not change and the detour reads the new
         * name. Re-stated each frame only so a rebuilt soul gets it back, which
         * is a string compare and nothing more.
         */
        void ReconcileName(SDK::wh::rpgmodule::C_Soul *soul);

        /**
         * Points Mannequin at this body's character instance.
         *
         * Answers **false** when the body is being rebuilt instead, in which
         * case the caller must stop touching it this frame: the entity is gone.
         *
         * The first binding happens in the body's own spawn tick, where nothing
         * can be animating it. A *later* change to the instance means something
         * re-dressed a live body -- a server-driven appearance or equipment
         * change is the only thing that can -- and `RefreshCharacterBindings`
         * from `PostUpdate` is then exactly the race that aborts the client:
         * the animation job installs the next sequencer segment against the
         * instance that was swapped out and virtual-calls a dead vptr.
         *
         * This is the one place that can see that happen, because it is the one
         * place that compares the instance against what is bound. A spawned body
         * is rebuilt rather than rebound, which puts the dressing back into a
         * spawn tick where it is safe. Only an adopted level body has to take
         * the risk, having no spawn tick of its own to go back to.
         */
        bool RefreshAnimationBinding(SDK::wh::entitymodule::C_Actor *actor);

        /** Marks the clothes and the face for re-applying when the replicated ones differ from what the body wears. */
        void NoteLookChanges();

        /**
         * Whether this body may be handed to the locomotion override at all.
         *
         * The override is read from inside the engine's animation update, on a
         * job thread, and `CAnimActionAIMovement::SelectState` is made to answer
         * the state it names -- a fragment-table index. A body whose Mannequin
         * side is not up yet has no table to index: the scope resolves a
         * sequence that is not there and the job dies on a null slot, with no
         * mod frame anywhere on the stack.
         */
        bool IsPresentable(SDK::wh::entitymodule::C_Actor *actor) const;

        /** Applies the ledger to the soul: health, and the death or revival that follows it. */
        void ApplyVitals(SDK::wh::entitymodule::C_Actor *actor);

        // --- Observer ---------------------------------------------------------

        void PresentRemote(SDK::wh::entitymodule::C_Actor *actor, SDK::IEntity *entity);
        /**
         * States the tags the simulator's engine settled on, on an observer.
         *
         * Nothing is decided here: the pace, direction and stance ride the wire
         * as the ids the simulating engine chose, and "moving" is read off the
         * pace tag rather than measured.
         */
        void PublishLocomotion(const KCDC::Common::Replication::PoseSnapshot &pose);

        /** Takes this body's override back off, for a peer that has become its simulator. */
        void WithdrawLocomotion();

        // --- Simulator --------------------------------------------------------

        void Simulate(SDK::wh::entitymodule::C_Actor *actor, SDK::IEntity *entity);

        /** Stands the body down from an attack: automation tokens back, target and stance off. */
        void StopCombat(SDK::wh::entitymodule::C_Actor *actor);

        /** Runs one frame of an engaged attack; false when the body cannot fight it. */
        bool Fight(SDK::wh::entitymodule::C_Actor *actor);

        /** The bow half of `Fight`: aims, draws, holds and looses. */
        bool Shoot(SDK::wh::entitymodule::C_Actor *actor, SDK::wh::entitymodule::C_Actor *target);

        /** Runs one frame of a native flee: reports the body, lets the brain think, offers it the escape until it takes it, then reports the order's progress. */
        void Escape(SDK::wh::entitymodule::C_Actor *actor, SDK::IEntity *entity);

        /** Takes a native flee back: the brain is held again, and a living body's move that its escape started is stopped. */
        void StopEscape();

        /** Advances the body towards the intent by moving it ourselves. */
        void StepKinematic(SDK::wh::entitymodule::C_Actor *actor, SDK::IEntity *entity, const glm::vec3 &target, float dt);

        /** Hands the destination to the engine's own movement controller. */
        void SteerNative(SDK::wh::entitymodule::C_Actor *actor, const glm::vec3 &target);

        /** Stops whatever the last frame asked of the movement controller. */
        void ReleaseSteering(SDK::wh::entitymodule::C_Actor *actor);

        /** Where the current intent wants the body, and whether it wants it anywhere. */
        bool ResolveIntentTarget(glm::vec3 &target) const;

        /** Turns the body towards a point, at a rate rather than instantly. */
        void FaceTowards(const glm::vec3 &target, float dt);

        /** Publishes pose, velocity and tags for everyone else to draw. */
        void ReportPose(SDK::IEntity *entity);

        /** Publishes what has become of the order, once per edge. */
        void ReportStatus(Shared::Game::NpcIntentStatus status);

        /** Tells the server the body is in something a handover cannot continue. */
        void ReportBusy(SDK::wh::entitymodule::C_Actor *actor);

        /** The ground under a point, or the point itself when nothing is under it. */
        static bool GroundAt(const glm::vec3 &point, float &outZ);

        // --- State ------------------------------------------------------------

        /** This body's AI-side record, which is how its brain is recognised. */
        SDK::wh::xgenaimodule::C_NPC *_npcRecord = nullptr;

        SDK::EntityId _entityId = 0;

        KCDC::Common::Spawn::SpawnTicket *_spawnTicket = nullptr;
        bool _spawnRefused                           = false;
        MafiaNet::Time _nextSpawnAttempt             = 0;

        /** Which `spawnEpoch` the body standing in the world was made for. */
        std::uint32_t _bodyEpoch = 0;

        /** Whether the body is dressed, souled and awake. */
        bool _bodyReady = false;

        /**
         * What the nametag pass remembers about this body between frames.
         *
         * Public and mutable for the same reason a player's is: the pass owns
         * the smoothing, the body owns the place to keep it.
         */
      public:
        KCDC::Features::Player::NametagVisibility nametagVisibility {};

      private:
        bool _broughtUp     = false;

        /** Whether an override is currently published for this body. */
        bool _locomotionPublished = false;

        SDK::ICharacterInstance *_boundCharacter = nullptr;

        bool _equipmentDirty = true;
        KCDC::Features::Items::NativeInventoryProjection _inventoryProjection;
        std::map<std::string, nlohmann::json> _stockRows;
        std::map<std::string, std::uint32_t> _stockEquipped;
        KCDC::Shared::NpcInventory::InteractionState _stockInteraction;
        std::optional<bool> _appliedHarvested;
        bool _harvestable            = false;
        std::uint64_t _stockRevision = 0;
        std::uint64_t _stockState    = 0;
        bool _stockReady             = false;
        bool _nativeStockKnown       = false;
        bool _stockApplied           = false;
        KCDC::Features::Items::InventoryProjectionRetry _stockRetry;
        std::string _seenInventoryState;
        std::string _appliedEquipment;

        Shared::Game::CharacterAppearance _appliedAppearance;
        bool _appearanceDirty   = true;
        bool _appearanceRefused = false;

        /** What the ledger said when it was last applied to the soul. */
        float _appliedHealth = -1.0F;

        // --- Simulator state --------------------------------------------------

        /** Which intent revision this client is running, and what it has said about it. */
        std::uint32_t _runningRevision                  = 0;
        Shared::Game::NpcIntentStatus _reportedStatus   = Shared::Game::NpcIntentStatus::Idle;

        MafiaNet::Time _lastStep = 0;

        MovementProgress _movementProgress;

        /** Whether a native steering request is currently on the body. */
        bool _steering = false;

        /** Whether this simulator holds the body's animated movement off, as `Frozen` asks. */
        bool _heldInPlace = false;

        /** The body the running attack is engaged with, or 0 while it is not fighting. */
        SDK::EntityId _combatTarget = 0;

        /** What `C_CombatAutomation` handed back for each control this body enabled; 0 owns nothing. */
        std::int32_t _offenseToken = 0;
        std::int32_t _defenseToken = 0;
        std::int32_t _guardToken   = 0;
        MafiaNet::Time _nextBiteAt = 0;

        /** When the held draw is loosed, and by when a freshly drawn bow must be ready. */
        MafiaNet::Time _bowReleaseAt = 0;
        MafiaNet::Time _bowReadyBy   = 0;

        /** The intent revision this simulator released the animal's brain for; 0 while the brain is held. */
        std::uint32_t _escapeRevision = 0;

        /** By when the released brain must take that revision's escape; 0 once it has, and while the brain is held. */
        MafiaNet::Time _escapeDueBy = 0;

        /**
         * The server moved this body while this client was simulating it.
         *
         * A scripted teleport reaches the simulator on the forced-state channel
         * and nowhere else, so it is taken as a warp on the next step rather
         * than raced against the poses this client is publishing.
         */
        bool _forcedPosePending = false;

        /** The `teleportSequence` this body was last placed for; a forced state carrying another is a scripted move. */
        std::uint32_t _appliedTeleportSequence = 0;

        /** Where the body was when the simulator last published a pose. */
        glm::vec3 _lastReported    = glm::vec3(0.0F);
        MafiaNet::Time _lastReportAt = 0;
        bool _reportedOnce         = false;

        // Locomotion tags this simulator publishes, derived from its own motion.
        std::uint8_t _publishedSpeedTag = Shared::Entities::NpcEntity::kNoTag;

        // --- Observer state ---------------------------------------------------

        bool _headLookApplied     = false;

        /** Buffered poses, interpolated exactly as a player's are. */
        Framework::Utils::SnapshotBuffer<KCDC::Common::Replication::PoseSnapshot, KCDC::Common::Replication::PoseSnapshotPolicy> _snapshots {&KCDC::Common::Replication::PoseBufferConfig()};

        // Set from the peer pump, read on the game thread.
        std::atomic<bool> _dirty {true};
    };
} // namespace KCDC::Features::Npc
