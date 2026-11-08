#pragma once

#include "core/feature_registry.h"

#include "features/ground_item/ground_item.h"
#include "features/player/player.h"
#include "features/player/player_nametags.h"

#include "sdk/types.h"
#include "sdk/wh/combatmodule/c_combat_actor.h"
#include "sdk/wh/entitymodule/c_actor.h"
#include "sdk/wh/entitymodule/c_actor_action_carry_corpse.h"
#include "sdk/wh/entitymodule/c_actor_action_carry_item.h"
#include "sdk/wh/entitymodule/c_actor_action_ladder.h"
#include "sdk/wh/entitymodule/c_player_input.h"
#include "sdk/wh/entitymodule/c_rider_player_input.h"
#include "sdk/wh/rpgmodule/c_soul.h"

#include "shared/features/player/item_carry_policy.h"
#include "shared/features/player/player_combat_attack.h"
#include "shared/features/player/player_combat_reaction.h"
#include "shared/features/player/player_combat_hit_report.h"
#include "shared/features/player/player_spawn.h"
#include "shared/game/appearance_catalog.h"
#include "shared/item_stack.h"

#include <mafianet/get_time.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace KCDC::Features::Player {
    /**
     * Everything about players that is not about one player.
     *
     * The engine's combat hooks fire for every fighter in the world and long
     * before this client has a replica of its own body; what they saw about
     * the local player is latched here until the next tick captures it. The
     * tick itself, the RPC registration and the nametag pass live here for the
     * same reason: they are about the population, not about a member of it.
     */
    class PlayerService final: public KCDC::Core::IFeatureService {
      public:
        /** Registers peer-lifetime RPC handlers before a session can start. */
        static void Install();

        /** Game thread: drives Frame() for every live human. */
        void Update() override;

        /**
         * Session ended: drops what the hooks latched about the local player,
         * the attack generation counter and the swing edge still waiting on
         * the wire.
         *
         * The queued body despawns are deliberately kept. They are how the
         * remote bodies leave the world, and PostUpdate drains them whether or
         * not a session is up; dropping them would leave the actors behind.
         * The soul property subscription is kept for the same kind of reason:
         * it is a registration on a native soul rather than a latch, and
         * forgetting which soul it is on re-registers it on the next session.
         */
        void Reset() override;

        /**
         * Holds a replica's native body until a network pump has returned to
         * an ordinary game tick, which is the only place it may be removed.
         */
        void QueueBodyDespawn(std::uint64_t networkId, SDK::EntityId entityId);

        /** Game thread: settles queued replica teardown against the engine. */
        void DrainBodyDespawns();

        /**
         * Whether walking is the pace the local body keeps coming back to.
         *
         * A preference, not a rule: the body starts each life walking and the
         * player's own toggle still works. What it has to survive is the
         * sprint, which the engine deliberately ends in a jog.
         */
        void SetWalkByDefault(bool walkByDefault);
        bool IsWalkByDefault() const {
            return _walkByDefault;
        }

        /** Puts the default pace on `input`, unless a sprint is running. */
        void ApplyWalkDefault(SDK::wh::entitymodule::C_PlayerInput *input);

        /**
         * `OnSprint` has just forced jog; `wasWalking` is the pace it took.
         * A sprint that started keeps the jog until it ends, one that was
         * refused has the pace back before the frame can read it.
         */
        void NotifySprintForcedJog(SDK::wh::entitymodule::C_PlayerInput *input, bool wasWalking);

        /** A sprint has ended: a pace it took goes back. */
        void NotifySprintEnded(SDK::wh::entitymodule::C_PlayerInput *input);

        /**
         * Whether the local body may not run or sprint at all.
         *
         * A rule, so it is held rather than set: the permissions the body had
         * are saved when the hold is taken and written back on release, the
         * way the game's own carry-a-body action does it. Nothing re-states
         * it -- the hold is kept by answering the one native that writes the
         * pair.
         */
        void SetWalkEnforced(bool enforced);
        bool IsWalkEnforced() const {
            return _walkEnforced;
        }

        /**
         * Overrules a `SetMovementRestriction` call on the body under the
         * rule, keeping what it asked for. Any other body is left alone.
         */
        void InterceptMovementRestriction(const SDK::wh::entitymodule::C_Actor *actor, bool &allowRun, bool &allowSprint);

        /** Game-thread ImGui callback. */
        void RenderNametags() {
            _nametags.Render();
        }

        // --- Where the local body stands ---

        /** Asks where the local body goes. False while it has not replicated yet. */
        static bool RequestSpawn();

        // --- The local player's body ---

        /** Re-reads the body the combat hooks name their events against. */
        void RefreshLocalActor();

        /**
         * The local soul now wears `gender`; its body follows it onto that
         * gender's skeleton at the next animation sync.
         *
         * The level builds the local player as `Player` on `male.cdf` before
         * its owner's appearance has replicated, and the engine only ever
         * picks the player's class at that point
         * (`C_SoulList::GetPlayerActorClassName`). So the body is rebuilt
         * in place, the way `C_Actor::Revive` rebuilds one.
         */
        void RequestLocalBody(Shared::Game::AppearanceGender gender);

        /**
         * Main thread, from the `CharacterManager::SyncAllAnimations` hook,
         * after every animation job was joined: the one point in a frame
         * where the old character can be released under a live body. Runs a
         * pending rebuild once the body is at rest.
         */
        void OnAnimationsSynced();

        /**
         * The local player's fighting, as the combat hooks last left it.
         *
         * Held here rather than written straight onto the replica because the
         * hooks run inside the engine's own guard and zone calls, which fire
         * several times between two ticks and long before this client has a
         * replica of its own body at all.
         */
        struct LocalCombat {
            SDK::EntityId actorEntityId  = 0;
            std::uint32_t combatRevision = 1;
            bool fistsUp                 = false;
            std::uint8_t guardRequest    = 0;
            std::int32_t combatZone      = -1;
        };

        const LocalCombat &Combat() const {
            return _localCombat;
        }

        // --- The local player's soul ---

        /**
         * Subscribes to the local soul's resource changes, once per soul.
         *
         * Keyed by address *and* WUID: the allocator hands the same address
         * back for a later soul, and a stale subscription would then report
         * another body's resources as the local player's.
         */
        void ListenForLocalSoulChanges(SDK::wh::rpgmodule::C_Soul *nativeSoul);

        /** Any thread: marks the local soul snapshot stale. */
        void MarkLocalSoulDirty();

        /** The soul the last accepted local capture snapshotted. */
        const SDK::wh::rpgmodule::I_Soul *CapturedLocalSoul() const;

        /** Records the soul a local capture is snapshotting. */
        void PublishCapturedLocalSoul(const SDK::wh::rpgmodule::I_Soul *nativeSoul);

        /** Takes the stale flag, clearing it. */
        bool ConsumeLocalSoulDirty();

        // --- Engine edges, raised by this feature's own hooks ---

        // Raised for every fighter in the world. Requested state, guard and
        // zone narrow to the local body; stance loss clears either the local
        // authoritative latch or a remote copy's presentation latch.
        void NotifyCombatStance(SDK::EntityId actorEntityId, bool fistsUp);
        void NotifyCombatStanceLost(SDK::EntityId actorEntityId);
        void NotifyCombatGuard(SDK::EntityId actorEntityId, std::uint8_t guardRequest);
        void NotifyCombatZone(SDK::EntityId actorEntityId, std::int32_t zoneId);

        /** An accepted native Press began a new held attack generation. */
        void NotifyCombatAttackBegan(std::int32_t inputClassId, std::int32_t zoneId);

        /** The exact attack action fields accepted by the local fighter's queue. */
        void NotifyCombatAttackCommitted(const SDK::wh::combatmodule::S_CombatActionAttackDataDBData *attackData, std::int32_t zoneId, std::uint8_t candidateFlag0, std::uint8_t candidateFlag1, std::uint8_t contextFlag0, std::uint8_t contextFlag1, std::uint32_t stateMode,
            float attackSpeed);

        /** Native input canceled the current held attack without a Commit. */
        void NotifyCombatAttackCanceled();

        /** Drops what the hooks latched about the local player's fighting. */
        void ForgetLocalCombat();

        // Counts accepted jumps for the local player only.
        void NotifyJumpStarted(SDK::EntityId actorEntityId);

        /** How many jumps the local player's engine has accepted, wrapping; the pose carries it. */
        std::uint8_t LocalJumpCount() const {
            return _localJumpCount.load(std::memory_order_relaxed);
        }

        // Counts the local player's `horse_slowdown` presses in the saddle.
        void NotifySlowdownPressed(const SDK::wh::entitymodule::C_RiderPlayerInput *riderInput);

        /** How many times the local player braked their horse, wrapping; the pose carries it. */
        std::uint8_t LocalSlowdownCount() const {
            return _localSlowdownCount.load(std::memory_order_relaxed);
        }

        // Reports accepted vaults for the local player only.
        void NotifyLedgeGrabStarted(SDK::EntityId actorEntityId, const SDK::wh::entitymodule::S_LedgeGrabInput &input);

        // Raised for every body entering or leaving the ladder action.
        void NotifyLadderEntered(SDK::wh::entitymodule::C_ActorActionLadder *action);
        void NotifyLadderExited(SDK::wh::entitymodule::C_ActorActionLadder *action);

        /** The ladder action this body is holding, or null. */
        SDK::wh::entitymodule::C_ActorActionLadder *LadderActionFor(SDK::EntityId actorEntityId) const;

        /**
         * Whether another body on the same ladder is within reach of
         * `climber` further along `direction` (up when positive): within the
         * rungs the ladder's traffic check gives a climber (its rung to six or
         * seven above).
         */
        bool IsClimberAhead(SDK::EntityId climber, float direction) const;

        void ForgetClimbs();

        // --- Carrying ------------------------------------------------------

        // Raised for every body entering or leaving the carry action. The
        // local player's own edges are also reported to the server.
        void NotifyCarryEntered(SDK::wh::entitymodule::C_ActorActionCarryCorpse *action);
        void NotifyCarryExited(SDK::wh::entitymodule::C_ActorActionCarryCorpse *action);

        /**
         * Lets go of `targetEntityId` on every shoulder on this machine, now
         * rather than a queue update later: for a body about to be destroyed
         * or revived, which the carry must not outlive.
         */
        void ReleaseCarriesOf(SDK::EntityId targetEntityId);

        /** The same for what one carrier holds, before that carrier goes. */
        void ReleaseCarryBy(SDK::EntityId carrierEntityId);

        /** Plays the local player's put-down, as the server ordered. */
        void PutDownLocalCarry(bool immediate);

        void ForgetCarries();

        // --- Carrying an item ----------------------------------------------

        /**
         * Raised for every carry-item action about to enter. A carry
         * StartItemCarry asked for is put past its pick-up here, so the item
         * goes straight into the hand.
         *
         * The action counts as running from here on: an instant pick-up
         * transfers the item inside Enter itself -- and so inside the
         * `RequestCarryItem` that queued it, since the queue inserts
         * synchronously -- which is before NotifyItemCarryEntered could
         * register it.
         */
        void PrepareItemCarryEnter(SDK::wh::entitymodule::C_ActorActionCarryItem *action);

        // Raised for every carry-item action once its Enter returned, and for
        // every one leaving. The local player's own edges are also reported
        // to the server.
        void NotifyItemCarryEntered(SDK::wh::entitymodule::C_ActorActionCarryItem *action, bool entered);
        void NotifyItemCarryExited(SDK::wh::entitymodule::C_ActorActionCarryItem *action);

        /** A carry's put-down clip started; the local player's is reported so the copies play theirs alongside. */
        void NotifyItemPutDownStarted(SDK::wh::entitymodule::C_ActorActionCarryItem *action);

        /** The carry-item action this player's body is running, or null. NPCs' carries are not kept. */
        SDK::wh::entitymodule::C_ActorActionCarryItem *ItemCarryActionFor(SDK::EntityId carrierEntityId) const;

        /** The class of the item that carry is for, as it was when the carry entered; invalid for none. */
        Shared::RPC::ItemClassId ItemCarryClassFor(SDK::EntityId carrierEntityId) const;

        /** What `ItemCarryRefusal` judges the local player by, from this machine's own view of the body. */
        Shared::Game::ItemCarryState LocalItemCarryState() const;

        /**
         * Whether `holder` is a running item carry's own holder: the
         * destination its pick-up moves the item into, which is never an
         * inventory.
         */
        bool IsItemCarryHolder(const void *holder) const;

        /** Whether `itemWuid` is the item of a running item carry, on any body. */
        bool IsCarriedItem(std::uint64_t itemWuid) const;

        /** The class of the item a carry is for, or an invalid one when it no longer resolves. */
        static Shared::RPC::ItemClassId CarriedItemClassOf(const SDK::wh::entitymodule::C_ActorActionCarryItem *action);

        /**
         * Makes an item of `itemClass` and puts it straight into `actor`'s
         * hands through the game's own carry, without the pick-up. The item is
         * this machine's alone: put down, it is handed to the prop the server
         * makes of it, and otherwise taken back out of the world. False when
         * the item could not be made or the actor's queue refused the carry.
         * `groundItemNetworkId` is the prop this carry stands in for, when the
         * prop itself could not be taken up.
         */
        bool StartItemCarry(SDK::wh::entitymodule::C_Actor *actor, const Shared::RPC::ItemClassId &itemClass, std::uint64_t groundItemNetworkId);

        /**
         * Has `actor` take up a carryable prop with the game's own pick-up:
         * the bend-down, reaching for the prop's own item where it lies.
         * False when the prop has no item to take up here or the queue refused
         * the carry -- the prop keeps its item then.
         */
        bool StartGroundItemCarry(SDK::wh::entitymodule::C_Actor *actor, KCDC::Features::GroundItem::GroundItem *prop);

        /**
         * The item a carry by `carrierEntityId` dropped to the ground, for the
         * prop the server makes of it to take over, or null. Taken: it is the
         * prop's from here on.
         */
        SDK::wh::entitymodule::C_Item *TakeDroppedCarryItem(SDK::EntityId carrierEntityId, const Shared::RPC::ItemClassId &itemClass);

        /** Whether `carrierEntityId` is carrying an item of `itemClass` right now. */
        bool IsCarryingItem(SDK::EntityId carrierEntityId, const Shared::RPC::ItemClassId &itemClass) const;

        /**
         * Plays the local player's item put-down, or lets go at once, as the
         * server ordered. A `refused` carry gives an item taken up from a prop
         * back to it.
         */
        void PutDownLocalItemCarry(bool immediate, bool refused);

        /** Ends what `carrierEntityId` carries now, before that carrier goes. */
        void ReleaseItemCarryBy(SDK::EntityId carrierEntityId);

        void ForgetItemCarries();

        /**
         * The engine has just declared a soul dead -- before HandleDeath, and
         * before it looks at whether that soul is allowed a game-over.
         *
         * Raised for every soul in the world, so the local body's death is
         * picked out by pointer. This, not the game-over, is the death: the
         * game-over is gated on the dying soul's player flag and latches once
         * per session, so a report that waited for it would be a report that
         * one engine flag can silently cancel.
         */
        void NotifySoulDied(SDK::wh::rpgmodule::C_Soul *soul);

        /** Returns true when multiplayer consumes the native death game-over. */
        bool NotifyGameOverRaised();
        void NotifyRevived(SDK::wh::entitymodule::C_Actor *actor);

        /**
         * Some body equipped or unequipped something.
         *
         * Raised by the two native calls every equip path funnels through --
         * including the player-gated wrappers, which reach them through a
         * `std::function` the call graph cannot be read across. Says only that
         * equipment moved somewhere; the owner confirms whether it was its own.
         */
        void NotifyEquipmentChanged();

        /** Takes the equipment-changed flag, clearing it. */
        bool ConsumeEquipmentChanged();

        /** Publishes the class the local hand holds, onto the owning replica. */
        static bool PublishLocalHandItemClass(std::uint32_t hand, const Shared::RPC::ItemClassId &itemClass);

        /** Speaks a chat line over its sender, if we have a body for it. */
        static void TalkFromChat(std::uint64_t speakerNetworkId, const std::string &text);

        /**
         * Marks the exact local controller call in which a release may queue
         * an accepted attack. The RunAction hook uses this narrow scope to
         * distinguish the player's committed action from AI and copy work.
         */
        class LocalAttackCommitCapture final {
          public:
            LocalAttackCommitCapture(SDK::wh::combatmodule::C_CombatActor *combatActor, std::int32_t combatZone);
            ~LocalAttackCommitCapture();
            LocalAttackCommitCapture(const LocalAttackCommitCapture &)            = delete;
            LocalAttackCommitCapture &operator=(const LocalAttackCommitCapture &) = delete;

          private:
            PlayerService *_service                               = nullptr;
            SDK::wh::combatmodule::C_CombatActor *_previousFighter = nullptr;
            std::int32_t _previousCombatZone                       = -1;
        };

        /** Whether RunAction is inside the local controller's PerformAttack. */
        bool IsCapturingLocalAttackCommit(const SDK::wh::combatmodule::C_CombatActor *combatActor) const;

        /** Stamps a combat reaction with the local body's identity fence, as a swing is. */
        bool CompleteCombatReactionFence(Shared::RPC::CombatReaction &reaction) const {
            return CaptureLocalActorFence(reaction.incarnation, reaction.epoch, reaction.combatRevision);
        }

        /** Stamps a blow report with the life of the body it landed on: the local player's, as it is now. */
        bool CompleteCombatHitReportFence(Shared::RPC::CombatHitReport &report) const {
            std::uint32_t combatRevision = 0;
            return CaptureLocalActorFence(report.incarnation, report.epoch, combatRevision);
        }

        /**
         * A fresh attack generation for a swing the engine started by itself
         * -- a riposte, a combo -- from the same count the input's swings take
         * theirs from, so no two swings of this player share a name. Game
         * thread.
         */
        std::uint32_t TakeCombatAttackGeneration() {
            const std::uint32_t generation = _nextCombatAttackGeneration;
            AdvanceCombatAttackGeneration();
            return generation;
        }

        /** The aim sampled at the exact local PerformAttack entry. */
        std::int32_t CapturedLocalAttackCombatZone() const {
            return _localAttackCommitCombatZone;
        }

      private:
        // A local body rebuild waiting for the next animation sync and for
        // the body to be at rest, and the body it was asked for: a level
        // change in between leaves that entity behind.
        bool _bodySwapPending                          = false;
        SDK::EntityId _bodySwapEntityId                = 0;
        Shared::Game::AppearanceGender _bodySwapGender = Shared::Game::AppearanceGender::Male;

        struct PendingBodyDespawn {
            std::uint64_t networkId = 0;
            SDK::EntityId entityId  = 0;
        };

        // Where the local death path last stopped. Every one of these used to
        // be a silent return, which is why a body that died and told nobody
        // looked exactly like one the server knew about and no script revived.
        enum class DeathStep : std::uint8_t {
            Silent,
            NoLocalBody,
            NotDead,
            NoOwnerReplica,
            ReportRefused,
            Reported,
        };

        /**
         * Tells the server the local body died, once per life.
         *
         * Returns whether the death being handled is this peer's own connected
         * body -- which is what decides whether the native game-over sequence
         * is consumed, independently of whether a report could be captured.
         */
        bool ReportLocalDeath();

        /** Says where the death path stopped, once per distinct reason. */
        void SayDeathStep(DeathStep step, const char *reason);

        /** Advances the accepted local combat edge, reserving zero. */
        void AdvanceLocalCombatRevision();

        /**
         * Returns the owned body's server identity when a local soul has been
         * captured. Shared by the attack edges and the two traversal edges,
         * which is what keeps combat and traversal on one service.
         *
         * DoorService has a fence capture of its own and keeps it: that one
         * also demands the replica and the latch agree, and optionally that
         * combat be neutral, neither of which an attack may require.
         */
        bool CaptureLocalActorFence(std::uint32_t &incarnation, std::uint8_t &epoch, std::uint32_t &combatRevision) const;

        /** Adds the owning Human's server-authored identity fences. */
        bool CompleteCombatAttackFence(Shared::RPC::CombatAttack &attack) const;

        /** Advances the next accepted local swing generation, reserving zero. */
        void AdvanceCombatAttackGeneration();

        /** Retries the one accepted Commit/Cancel which has not entered the wire yet. */
        void FlushCombatAttackTerminal();

        /** Holds a terminal edge that could not be queued, under a deadline. */
        void HoldCombatAttackTerminal(const Shared::RPC::CombatAttack &attack);

        void ClearActiveCombatAttack();

        /** The owner of the queue a ladder action was posted to. */
        static SDK::EntityId ClimberOf(const SDK::wh::entitymodule::C_ActorActionLadder *action);

        /** The owner of the queue a carry action was posted to. */
        static SDK::wh::entitymodule::C_Actor *CarrierOf(const SDK::wh::entitymodule::C_ActorActionCarryCorpse *action);

        /** Unbinds whatever `carrier` holds and stops the action holding it. */
        static void ReleaseCarry(SDK::wh::entitymodule::C_Actor *carrier);

        /** The owner of the queue a carry-item action was posted to. */
        static SDK::wh::entitymodule::C_Actor *CarrierOf(const SDK::wh::entitymodule::C_ActorActionCarryItem *action);

        /** Takes the carries' items nobody took over back out of the world once they are due. */
        void SweepCarryItems();

        /**
         * An item a carry on this machine holds or held: one StartItemCarry
         * made, one a prop gave up, or one the local player took from a pile.
         * Kept until the carry is over and the item is either a prop's, a
         * pile's, or due to be taken back out of the world.
         */
        struct CarryItemRecord {
            std::uint64_t itemWuid = 0;
            Shared::RPC::ItemClassId itemClass;
            SDK::EntityId carrierEntityId     = 0;
            std::uint64_t groundItemNetworkId = 0;
            // Into the hands without the pick-up: an item made for the carry.
            bool instant = false;
            bool carried = false;
            // Lying where the carry dropped it, for its prop to take over.
            bool dropped = false;
            // The server refused the carry: the item goes back to its prop.
            bool refused            = false;
            MafiaNet::Time removeAt = 0;
        };

        CarryItemRecord *FindCarryItemRecord(std::uint64_t itemWuid);

        /** Queues the game's carry of one item on `queue`; see C_ActionActor::RequestCarryItem. */
        static bool RequestCarry(SDK::wh::entitymodule::C_ActionActor *queue, std::uint64_t itemWuid);

        LocalCombat _localCombat;

        NametagPass _nametags;

        // What this peer has already told the server about its own swings: the
        // generation counter that orders them, the Press still held open, and
        // the one Commit or Cancel that has not reached the wire yet.
        std::uint32_t _nextCombatAttackGeneration   = 1;
        std::uint32_t _activeCombatAttackGeneration = 0;
        std::int32_t _activeCombatAttackInputClass  = -1;
        // Expire unacknowledged terminal edges so later attacks are not
        // blocked indefinitely.
        std::uint64_t _pendingCombatAttackDeadline = 0;
        std::optional<Shared::RPC::CombatAttack> _pendingCombatAttackTerminal;

        DeathStep _deathStep = DeathStep::Silent;

        // Whether this peer has already told the server the local body died.
        // The native game-over is raised on every frame the body stays dead,
        // so the report goes out once and the revive edge takes it back.
        bool _reportedGameOver = false;

        // The soul the last accepted local capture snapshotted, and whether a
        // resource has changed since. Set from the soul's own property
        // listener, on whichever thread the engine changed it on, and read on
        // the game thread by the owner's capture.
        std::atomic<const SDK::wh::rpgmodule::I_Soul *> _localSoul {nullptr};
        std::atomic<bool> _localSoulDirty {true};

        // Written from the jump hook, which the movement pass may run off the
        // game thread; read by the capture tick.
        std::atomic<std::uint8_t> _localJumpCount {0};
        std::atomic<std::uint8_t> _localSlowdownCount {0};

        // The soul the property listener is registered on, keyed by address
        // and WUID so allocator reuse is detected. Game thread only.
        const SDK::wh::rpgmodule::I_Soul *_listeningSoul = nullptr;
        std::uint64_t _listeningSoulWuid                 = 0;

        // Every body currently in the ladder action, by entity id.
        std::unordered_map<SDK::EntityId, SDK::wh::entitymodule::C_ActorActionLadder *> _ladderActions;

        // Every body currently in the carry action, by the carrier's entity id.
        std::unordered_map<SDK::EntityId, SDK::wh::entitymodule::C_ActorActionCarryCorpse *> _carryActions;

        /** A running item carry, and the class of its item as it entered. */
        struct ItemCarry {
            SDK::wh::entitymodule::C_ActorActionCarryItem *action = nullptr;
            Shared::RPC::ItemClassId itemClass;
        };

        // Every player's body currently in the carry-item action, by entity
        // id. Players only: an NPC's carry is the game's business, and a body
        // nothing here tracks cannot leave a stale action behind.
        std::unordered_map<SDK::EntityId, ItemCarry> _itemCarryActions;

        std::vector<CarryItemRecord> _carryItems;

        // The carry-item action inside its Enter, between PrepareItemCarryEnter
        // and NotifyItemCarryEntered.
        SDK::wh::entitymodule::C_ActorActionCarryItem *_enteringItemCarry = nullptr;

        // Set from the equip detours, which run on whichever thread the engine
        // called them on; read and cleared by the owner's own capture.
        std::atomic<bool> _equipmentChanged {true};


        // The local fighter whose exact accepted action is being queued.
        SDK::wh::combatmodule::C_CombatActor *_localAttackCommitFighter = nullptr;
        std::int32_t _localAttackCommitCombatZone                       = -1;

        // Replica destruction can arrive from the loading-time network pump,
        // where touching the half-built entity system is unsafe. The replica
        // itself is gone after that callback, so only stable identities cross
        // into the next ordinary game tick.
        std::mutex _pendingBodyDespawnsMutex;
        std::vector<PendingBodyDespawn> _pendingBodyDespawns;

        /** Takes or releases the permission hold against the body it is on. */
        void SettleWalkHold();

        bool _walkByDefault = false;

        // Latched when a sprint begins from a walk, so only a pace the
        // sprint took is handed back and a player who chose to jog keeps it.
        bool _sprintTookWalkMode = false;

        // The walk rule, and the permissions to hand back when it is lifted:
        // the newest intent seen, not the one from when the hold was taken.
        bool _walkEnforced       = false;
        bool _walkHoldActive     = false;
        bool _heldAllowRunWas    = true;
        bool _heldAllowSprintWas = true;

        // Set across the hold's own write, so the interception does not read
        // it back as somebody's intent.
        bool _walkHoldWriting = false;
    };
} // namespace KCDC::Features::Player
