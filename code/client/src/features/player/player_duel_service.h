#pragma once

#include "core/feature_registry.h"
#include "features/player/player_duel_roster.h"

#include "shared/features/player/player_combat_cancel.h"
#include "shared/features/player/player_combat_claim.h"
#include "shared/features/player/player_combat_hit_report.h"
#include "shared/features/player/player_combat_outcome.h"
#include "shared/features/player/player_combat_reaction.h"
#include "shared/features/player/player_combat_swing_ledger.h"

#include "sdk/types.h"
#include "sdk/wh/combatmodule/c_combat_actor.h"
#include "sdk/wh/combatmodule/c_combat_actor_defense_actions.h"
#include "sdk/wh/combatmodule/c_combat_actor_time_cop.h"
#include "sdk/wh/combatmodule/c_combat_opponent_candidates.h"
#include "sdk/wh/combatmodule/c_combat_player_controller.h"
#include "sdk/wh/combatmodule/c_combat_rpg.h"
#include "sdk/wh/combatmodule/s_combat_hit_record.h"
#include "sdk/wh/entitymodule/c_actor.h"
#include "sdk/wh/rpgmodule/c_soul.h"
#include "sdk/wh/rpgmodule/c_soul_state_effect.h"
#include "sdk/wh/rpgmodule/c_soul_state_storage.h"
#include "sdk/wh/rpgmodule/s_combat_hit_data.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <unordered_map>
#include <utility>
#include <vector>

namespace KCDC::Features::Player {
    /**
     * Player-versus-player melee, run through the engine's own fight and
     * ruled on by the server.
     *
     * Four rules make it one fight on every machine rather than one per
     * machine.
     *
     * **Pairing is the engine's.** The local player's opponent picker is let
     * see remote players -- it normally skips anything IsPlayerControlled, and
     * nobody is hostile to a fellow player -- and locks on exactly as it does
     * to an NPC. Whom each player's fighter is locked on to is replicated, and
     * every copy is handed the same lock through the same `SetTarget`
     * call, so the engine promotes it to the opponent at the copy's next
     * rest on every machine. Swings, windows, recoils and sync partners all
     * key on that opponent.
     *
     * **A blow is resolved once, where the victim is real.** A player's
     * contact on a copy is dropped before it produces anything: the machine
     * that owns the victim's body resolves it against a guard that has no
     * latency in it. What its engine ran on the victim -- the hit reaction,
     * the perfect blocks -- goes out by the exact rows it built them from, as
     * any action on one's own body does.
     *
     * **The server rules.** The health that resolution would take is withheld
     * at the soul's storage writer and reported, with how the blow was taken
     * and the recoil the engine ran on the attacker's copy, to the server.
     * Only its ruling takes the health -- on the victim's machine, through the
     * game's own damage primitive, so a killing blow dies the native way --
     * and only its ruling puts the recoil on the attacker's real body and the
     * impact on every other machine's copies. Arrows take the same road: the
     * hit the server already accepted is applied here, and its health
     * withheld and reported the same way.
     *
     * **Time is shared.** A replayed swing runs at the speed the sender's
     * engine gave it rather than the one this engine would, and the local
     * player's swing at a remote player is held back by a round trip through
     * the engine's own slot delay, so the defender's answer is here when the
     * blow lands.
     *
     * **Threads.** KCD2 ticks combat groups in parallel (a fighter and its
     * opponent share one group and its lock), and entity and AI updates may
     * run as jobs, so most of the hooks below can be entered off the main
     * thread. Who is who is therefore published once a tick as an immutable
     * roster the hooks only read; a scope that describes one native call is
     * thread-local; and what a worker thread has to publish or consume goes
     * through a lock and is acted on from Update.
     */
    class PlayerDuelService final: public KCDC::Core::IFeatureService {
      public:
        /** Registers the referee's ruling. Call once at startup. */
        static void Install();

        /** Game thread: refreshes who is who, publishes the local lock, hands copies theirs, sends what the hooks queued and plays the rulings. */
        void Update() override;

        /** Session ended: forgets every fighter and every pending value. */
        void Reset() override;

        /** Rejects presentations from before the server cancelled this pair; `relayedAt` is on this machine's clock. */
        bool WasCancelled(std::uint64_t first, std::uint64_t second, std::uint64_t relayedAt) const;

        // --- Who is fighting whom ------------------------------------------

        /** Whether either side of this fight is a replicated player's body. */
        bool InvolvesRemotePlayer(const SDK::wh::combatmodule::C_CombatActor *fighter) const;

        /** Whether `fighter` is a replicated player's body on this machine. */
        bool IsRemoteFighter(const SDK::wh::combatmodule::C_CombatActor *fighter) const;

        /** Whether `fighter` is the local player's own. */
        bool IsLocalFighter(const SDK::wh::combatmodule::C_CombatActor *fighter) const;

        /** Only the automatic empty-queue exit may yield to the owner's stance. */
        class CombatPostUpdateScope final {
          public:
            explicit CombatPostUpdateScope(const SDK::wh::combatmodule::C_CombatActor *fighter): _previous(_postUpdatingFighter) { _postUpdatingFighter = fighter; }
            ~CombatPostUpdateScope() { _postUpdatingFighter = _previous; }
            CombatPostUpdateScope(const CombatPostUpdateScope &) = delete;
            CombatPostUpdateScope &operator=(const CombatPostUpdateScope &) = delete;

          private:
            const SDK::wh::combatmodule::C_CombatActor *_previous;
        };

        bool KeepRemoteStance(const SDK::wh::combatmodule::C_CombatActor *fighter) const;
        bool IsPlayerPair(const SDK::wh::combatmodule::C_CombatActor *fighter) const;

        /** The player the local fighter is locked on to, as a network id, or 0. */
        std::uint64_t LocalTargetNetworkId() const;

        /**
         * The player the local fighter is fighting, as a network id, or 0: its
         * opponent when that is a remote player, else its lock-on, else the
         * remote player it is plainly swinging at -- the nearest within
         * `kSoftTargetRange` and `kSoftTargetConeDegrees` of where it faces.
         * The engine fights without either whenever the player strikes on the
         * move. What a local swing names as the body it is aimed at.
         */
        std::uint64_t LocalOpponentNetworkId() const;

        /** Game thread: the local player exchanged a blow or a lock with `networkId`, which makes it the one they are fighting. */
        void Engage(std::uint64_t networkId);

        /**
         * The local player's per-frame target pick, inside a fight with a
         * replicated player: the fight's body stays the target. False when
         * there is no fight, and the engine picks as it always does.
         */
        bool KeepEngagedTarget(SDK::wh::combatmodule::C_CombatPlayerController *controller) const;

        /**
         * A copy's per-frame target pick, while it holds its owner's own lock
         * with its fists up in the free guard: the lock stays. False for any
         * other body, and for any other copy, which the engine picks for as
         * it always does.
         */
        bool KeepOwnerLock(SDK::wh::combatmodule::C_CombatPlayerController *controller) const;

        /** How far a swing with no opponent is taken to reach for one, in metres. */
        static constexpr float kSoftTargetRange = 3.5F;

        /** How far off the fighter's facing that body may stand, in degrees either side. */
        static constexpr float kSoftTargetConeDegrees = 60.0F;

        /**
         * Game thread: squares a replicated fighter up against the body its
         * owner's swing is aimed at, before the swing is played -- the engine
         * raises no contact for a fighter with no opponent. It stays squared
         * up against that body until its owner swings at another or locks on.
         */
        void SquareUp(SDK::wh::combatmodule::C_CombatActor *fighter, std::uint64_t opponentNetworkId);

        // --- The local player's opponent picker ----------------------------

        /** Offers every remote body the picker may lock on to, after its own refresh. */
        void OfferRemoteCandidates(SDK::wh::combatmodule::C_CombatOpponentCandidates *candidates) const;

        /** Whether the picker may treat `candidate` as hostile to the local fighter: a remote body it may weigh, or a server's NPC the two of them fight. */
        bool IsHostileCandidate(const SDK::wh::combatmodule::C_CombatRPG *asker, const SDK::wh::combatmodule::C_CombatActor *candidate) const;

        /** Whether a remote body is one the picker should see; the scope below applies only to these. */
        bool IsPickableRemoteBody(const SDK::wh::entitymodule::C_Actor *actor) const;

        /**
         * Whether the local picker may offer this remote player's body at all,
         * as a client script decides -- someone lying wounded, say. Local to
         * this machine; cleared with the session.
         */
        void SetTargetable(std::uint64_t networkId, bool targetable);
        bool IsTargetable(std::uint64_t networkId) const;

        /** Whether `actor` is the remote body the picker is weighing on this thread right now. */
        static bool IsOfferingRemoteCandidate(const SDK::wh::entitymodule::C_Actor *actor) {
            return actor != nullptr && actor == _offeredCandidate;
        }

        /**
         * Holds the actor the refusal test is weighing for one call, so the
         * IsPlayerControlled detour can answer for it as for an NPC and every
         * other test of the refusal still runs.
         */
        class CandidateScope final {
          public:
            explicit CandidateScope(const SDK::wh::entitymodule::C_Actor *actor);
            ~CandidateScope();
            CandidateScope(const CandidateScope &)            = delete;
            CandidateScope &operator=(const CandidateScope &) = delete;

          private:
            const SDK::wh::entitymodule::C_Actor *_previous;
        };

        // --- The engine's choreography --------------------------------------

        /**
         * Whether the planner of `attacker` may post the windows it scheduled.
         * Yes onto the local body, whose windows only its own input fires; no
         * onto a replicated body, whose replayed guard press would turn into
         * a perfect block inside the window and decide the remote defender's
         * move on a machine that does not own it.
         */
        bool MayDispatchSlots(const SDK::wh::combatmodule::C_CombatActor *attacker) const;

        /** Whether the lethal variant may be chosen against this fighter's opponent. */
        bool MayWeighLethalVariant(const SDK::wh::combatmodule::C_CombatRPG *rpg) const;

        /** Whether the windows `attacker`'s planner posts are cut to the plain block (`DuelRoster::BlocksOnly`). */
        bool PostsBlocksOnly(const SDK::wh::combatmodule::C_CombatActor *attacker) const;

        /**
         * Whether `action` is a copy's own block of a swing a block already
         * showed here: its owner's, replayed before the copy's trigger fired.
         * The replayed one stands; a second would start the block over.
         */
        bool BlocksShownSwing(const SDK::wh::combatmodule::C_CombatActor *fighter, const SDK::wh::combatmodule::I_CombatActorAction *action) const;

        /**
         * Marks one planner dispatch onto a replicated body: every window
         * but the plain block is looked up as absent, so it is never opened
         * there. Suspended while a window is being opened, whose own
         * activation looks windows up again.
         */
        class BlockSlotsScope final {
          public:
            explicit BlockSlotsScope(bool active);
            ~BlockSlotsScope();
            BlockSlotsScope(const BlockSlotsScope &)            = delete;
            BlockSlotsScope &operator=(const BlockSlotsScope &) = delete;

          private:
            bool _previous;
        };

        /** Whether a lookup of window `kind` on this thread must answer none. */
        static bool HidesSlot(std::uint32_t kind);

        // --- Blows -----------------------------------------------------------

        /**
         * Whether a contact is the local player's blow on a remote player's
         * body: resolved here on the copy, so the attacker sees its blow land
         * at once, and claimed for the referee. A blow from anyone else -- an
         * NPC -- is resolved wherever it lands, as it always was.
         */
        bool IsClaimedContact(const SDK::wh::combatmodule::C_CombatActor *attacker, const SDK::wh::combatmodule::C_CombatActor *victim) const;

        /**
         * Whether a contact is between two replicated players' bodies: their
         * machines resolve it, and the ruling plays it here. Resolved here as
         * well, it would play a second reaction from copies' positions.
         */
        bool IsBystanderContact(const SDK::wh::combatmodule::C_CombatActor *attacker, const SDK::wh::combatmodule::C_CombatActor *victim) const;

        /**
         * Whether `action` is the reaction a claimed contact raises on a copy
         * whose owner's own reaction to that swing already played here: the
         * owner's stands, and the contact's would show the blow twice.
         */
        bool HitsShownBlow(const SDK::wh::combatmodule::C_CombatActor *fighter, const SDK::wh::combatmodule::I_CombatActorAction *action) const;

        /** The local player's swing now in the air, by generation; what a contact it makes is claimed under. Any thread. */
        void NoteLocalSwing(std::uint32_t generation) {
            if (generation != 0) {
                _localSwingGeneration.store(generation);
            }
        }

        /**
         * The remote player the local swing now starting is aimed at, or 0
         * (`LocalOpponentNetworkId`, sampled on the game thread at the
         * swing's press and commit). A swing aimed at one is held back by
         * the round trip whether the engine names them its opponent or not.
         */
        void NoteLocalSwingTarget(std::uint64_t networkId) {
            _localSwingTarget.store(networkId);
            Engage(networkId);
        }

        /**
         * One remote player's blow on the local body, as this thread's engine
         * resolves it: the report the server will rule on, filled in as the
         * engine runs, and the reactions it ran on the local body, held so
         * they go out with it.
         */
        struct BlowCapture {
            /** The local body's fighter, for a swing; null for an arrow. */
            SDK::wh::combatmodule::C_CombatActor *victim = nullptr;

            /** The attacker's fighter here, for a swing. */
            SDK::wh::combatmodule::C_CombatActor *attacker = nullptr;

            /** The soul whose writes are the blow's to withhold. */
            const SDK::wh::rpgmodule::C_Soul *victimSoul = nullptr;

            Shared::RPC::CombatHitReport report;

            /** Server ledger units per native health point; an NPC's ledger is not its soul's. */
            float healthScale = 1.0F;

            /** Names an NPC victim's simulated body as the reporter, fenced to its life and order. */
            void FenceNpcVictim(std::uint64_t networkId, std::uint32_t incarnation, std::uint8_t epoch, std::uint32_t revision, float maximumHealth);

            /** Fences an NPC attacker's blow to the life and order that swung it. */
            void FenceNpcAttacker(std::uint32_t incarnation, std::uint32_t revision) {
                report.attackerIncarnation = incarnation;
                report.attackerRevision    = revision;
            }

            /** Whether the engine's own hit data has been read. */
            bool hitData = false;

            std::vector<Shared::RPC::CombatReaction> reactions;
        };

        /**
         * Marks a contact this thread resolves on the local body against a
         * remote attacker. Every action the engine queues inside it is one of
         * its consequences; the health it would take is withheld; and on the
         * way out it is reported to the server.
         */
        class ResolutionScope final {
          public:
            ResolutionScope(PlayerDuelService &service, SDK::wh::combatmodule::C_CombatActor *attacker, SDK::wh::combatmodule::C_CombatActor *victim);
            ~ResolutionScope();
            ResolutionScope(const ResolutionScope &)            = delete;
            ResolutionScope &operator=(const ResolutionScope &) = delete;

          private:
            PlayerDuelService &_service;
            SDK::wh::combatmodule::C_CombatActor *_previous;
            BlowCapture *_previousCapture;
            BlowCapture _capture;
        };

        /**
         * Marks the local player's blow on a remote player's body as this
         * thread's engine resolves it on the copy: what it would take, how
         * the copy's guard took it, the reaction it ran on the copy and the
         * recoil on the local body go into the claim; the copy's own soul
         * keeps its owner's numbers as always.
         */
        class ClaimScope final {
          public:
            ClaimScope(PlayerDuelService &service, SDK::wh::combatmodule::C_CombatActor *attacker, SDK::wh::combatmodule::C_CombatActor *victim);
            ~ClaimScope();
            ClaimScope(const ClaimScope &)            = delete;
            ClaimScope &operator=(const ClaimScope &) = delete;

          private:
            PlayerDuelService &_service;
            BlowCapture *_previousCapture;
            BlowCapture _capture;
        };

        /**
         * Marks the local body taking a remote player's arrow, which the
         * server already accepted and this machine applies: the health it
         * would take is withheld and reported the same way a blade's is.
         */
        class MissileHitScope final {
          public:
            MissileHitScope(PlayerDuelService &service, const SDK::wh::rpgmodule::C_Soul *victimSoul, std::uint64_t shooterNetworkId, std::uint64_t shotSequence);
            ~MissileHitScope();
            MissileHitScope(const MissileHitScope &)            = delete;
            MissileHitScope &operator=(const MissileHitScope &) = delete;

          private:
            PlayerDuelService &_service;
            BlowCapture *_previousCapture;
            BlowCapture _capture;
        };

        /** The RPG side's hit data for a blow; inside a resolution, how the local body took it. */
        void OnCombatHitData(const SDK::wh::rpgmodule::S_CombatHitData *data);

        /**
         * A write reached the soul-state storage writer. Inside a blow between
         * two players, what it takes is noted for the referee. True withholds
         * it: the health a remote player's blow takes off the local body,
         * which only the server's ruling may take, and any damage a collision
         * with a remote player's copy deals. Stamina is noted and kept.
         */
        bool ScreenSoulWrite(const SDK::wh::rpgmodule::C_SoulStateStorage18070F3E8 *storage, SDK::wh::rpgmodule::E_SoulState state, float value, const SDK::wh::rpgmodule::C_SoulStateEffect *cause);

        /**
         * The record the engine queues for the victim of a contact. Inside a
         * resolution, its impact is kept for the report.
         */
        void OnHitRecordQueued(const SDK::wh::combatmodule::S_CombatHitRecord *record);

        /**
         * One combat action the engine accepted onto `fighter`'s queue.
         * Queues it for publication when it is this machine's to publish.
         */
        void OnActionRun(SDK::wh::combatmodule::C_CombatActor *fighter, SDK::wh::combatmodule::I_CombatActorAction *action);

        /**
         * Marks one RunAction call on `fighter`: a swing timeline begun inside
         * it is the timeline of the action RunAction lets in, which the commit
         * capture or OnActionRun publishes.
         */
        class RunActionScope final {
          public:
            explicit RunActionScope(const SDK::wh::combatmodule::C_CombatActor *fighter): _previous(_runningActionFighter) {
                _runningActionFighter = fighter;
            }
            ~RunActionScope() {
                _runningActionFighter = _previous;
            }
            RunActionScope(const RunActionScope &)            = delete;
            RunActionScope &operator=(const RunActionScope &) = delete;

          private:
            const SDK::wh::combatmodule::C_CombatActor *_previous;
        };

        /**
         * A combo's next strike handed to `running` (QueueNextAttack), before
         * the engine stores it. The helper's combo handler builds it through
         * CreateAttackAction and hands it to the running attack rather than to
         * RunAction; it starts when that attack ends, past every RunAction.
         * Kept for the local body's attack, if it has no next strike yet.
         */
        void OnComboStrikeQueued(const SDK::wh::combatmodule::C_CombatActorActionAttack *running, const SDK::wh::combatmodule::I_CombatActorAction *next);

        /**
         * Right after a timeline began: a timeline the local body begins
         * outside RunAction is the combo's next strike it was handed, which is
         * published here as a swing of its own (CombatReaction::Attack).
         */
        void PublishComboStrike(SDK::wh::combatmodule::C_CombatActorTimeCop *timeCop);

        // --- Timing ------------------------------------------------------------

        /** A swing timeline's time scale, as its fighter's engine is about to use it. */
        float FilterAttackSpeed(const SDK::wh::combatmodule::C_CombatRPG *rpg, float nativeSpeed);

        /** The time scale this thread's local swing was just given, or 0; clears it. */
        static float TakeLocalAttackSpeed();

        /** Hands the next swing timeline of `fighter` the sender's time scale. */
        void ForceNextAttackSpeed(const SDK::wh::combatmodule::C_CombatActor *fighter, float attackSpeed);

        /** Drops a forced time scale a refused replay never used. */
        void ForgetForcedAttackSpeed(const SDK::wh::combatmodule::C_CombatActor *fighter);

        /** Right after a timeline began: holds the local swing at a remote player back by a round trip. */
        void HoldLocalSwing(SDK::wh::combatmodule::C_CombatActorTimeCop *timeCop) const;

        // --- Replay --------------------------------------------------------------

        /** A remote player's swing, `generation`, is now what their body here is playing; blows it lands are reported under it. */
        void NoteReplayedSwing(const SDK::wh::combatmodule::C_CombatActor *fighter, std::uint32_t generation, std::uint32_t windowMs);

        /** The swing `attacker` is playing on this machine, by generation, or 0. Any thread. */
        std::uint32_t SwingOf(const SDK::wh::combatmodule::C_CombatActor *attacker) const;

        /** The server asks how this body defended against a swing its copy of the attacker never landed here. */
        void AnswerClaim(const Shared::RPC::CombatClaimQuery &query);

        /** An authoritative fighter raised a counter against a replicated opponent. */
        void OnRiposteRequested(SDK::wh::combatmodule::C_CombatActor *fighter);

        enum class ReactionResult : std::uint8_t {
            Applied,
            Deferred,
            Obsolete,
        };

        /** Game thread: builds another machine's own-body action from its rows on the body it names here. */
        ReactionResult ApplyRemoteReaction(const Shared::RPC::CombatReaction &reaction);

        /** Why the last ApplyRemoteReaction deferred, for the log that gives up on it. */
        const char *LastDeferral() const {
            return _lastDeferral;
        }

        /**
         * Whether this thread is queueing a replay of another machine's
         * decision: `C_CombatActorAction::CanBeReplacedBy` (0x1814962EC)
         * lets it in over whatever the copy is busy with in that slot.
         */
        static bool IsGivingWay() {
            return _givingWay;
        }

        /** Anything past this many metres is not a candidate for the local picker at all. */
        static constexpr float kCandidateRange = 12.0F;

        /**
         * What a relay takes beyond the round trip itself: the server's
         * relay tick, the defender's frame, the presentation clock both ends
         * play the stream on, and their jitter. Measured at 120 ms of round
         * trip, 50 ms left about half of the defender's perfect blocks
         * arriving after the attacker's own blade had met the copy. The
         * engine caps the whole hold at `wh_cs_TimeAlignment_MaxAnimAddition`
         * (0.5 s by default, 0x18409A210) regardless.
         */
        static constexpr float kSwingHoldMarginSeconds = 0.15F;

        /**
         * How long a ruling's recoil is retried against a body that will not
         * take it yet: a copy whose swing has not reached its contact here
         * still runs the attack at a standing no recoil outranks, and drops
         * it once the blade has passed. Past this the swing is long over.
         */
        static constexpr std::uint64_t kRecoilRetryBudgetMs = 600;

        /** What a blow that cannot kill leaves its victim with, as the engine's own clamp does (`HealthToKnockOut`). */
        static constexpr float kHealthLeftByNonLethalBlow = 1.0F;

      private:
        /** Who is who on this machine, as of one tick. */
        using Roster = DuelRoster<SDK::wh::combatmodule::C_CombatActor, SDK::wh::entitymodule::C_Actor>;

        /**
         * How many rosters rotate. A hook holds a roster only for the one
         * native call it runs inside, and a slot is refilled this many ticks
         * after it was published, far past any such call.
         */
        static constexpr std::size_t kRosterSlots = 4;

        const Roster &Current() const {
            return _rosters.Current();
        }

        /** Fills and publishes this tick's roster, and hands every copy its owner's lock. */
        void PublishRoster(std::uint64_t now);

        /** The fighter a replicated body stands for here, or null. */
        SDK::wh::combatmodule::C_CombatActor *FighterFor(const Roster &roster, std::uint64_t networkId, bool build) const;

        // --- Who is fighting whom ----------------------------------------------

        /** How far apart a fight may drift before it is over. */
        static constexpr float kEngageRange = 5.0F;

        RosterRing<Roster, kRosterSlots> _rosters;
        DuelEngagement _engagement;

        // The body each remote fighter last swung at, by NetworkID: what it is
        // held squared up against while its owner holds no lock-on. Game
        // thread only.
        std::unordered_map<const SDK::wh::combatmodule::C_CombatActor *, std::uint64_t> _swingOpponents;

        // --- Publication -------------------------------------------------------

        /** Queues one action this machine resolved; Update sends it. */
        void Publish(const Shared::RPC::CombatReaction &reaction);

        /** Queues several, in order. */
        void Publish(const std::vector<Shared::RPC::CombatReaction> &reactions);

        /** Queues one blow for the referee, with the reactions it ran; Update sends both. */
        void Report(BlowCapture &capture);

        /** Gives an authoritative NPC action its identity and swing before publishing. */
        void PublishActionReaction(const SDK::wh::combatmodule::C_CombatActor *fighter, Shared::RPC::CombatReaction reaction);

        /** Sends what the hooks queued since the last tick, from the game thread. */
        void FlushPublications();

        /** Replays the NPC actions that arrived, in order per body, once each body has caught up to them. */
        void DrainNpcReactions(std::uint64_t now);

        /** How long an NPC action may wait for its body, and how many may wait at once. */
        static constexpr std::uint64_t kNpcReactionLifetimeMs = 2000;
        static constexpr std::size_t kMaxPendingNpcReactions  = 512;

        struct PendingNpcReaction {
            Shared::RPC::CombatReaction reaction;
            std::uint64_t receivedAt;
        };
        std::vector<PendingNpcReaction> _incomingNpcReactions;
        std::vector<PendingNpcReaction> _npcReactions;
        std::atomic<std::uint32_t> _nextNpcSwing {1};
        std::atomic<std::uint64_t> _nextNpcPresentation {1};

        /** The last action replayed on each NPC, fenced to the life and order it belongs to. */
        struct NpcPresentation {
            std::uint32_t incarnation = 0;
            std::uint32_t revision    = 0;
            std::uint64_t sequence    = 0;
        };
        std::unordered_map<std::uint64_t, NpcPresentation> _lastNpcPresentation;

        /** Queues one action, or holds it with the blow while a resolution is open on this thread. */
        void PublishResolved(const Shared::RPC::CombatReaction &reaction);

        // Actions resolved here and blows reported, queued from whichever
        // thread the engine ran them on, and the game thread's copies of them
        // while it sends them.
        std::mutex _publicationMutex;
        std::vector<Shared::RPC::CombatReaction> _publications;
        std::vector<Shared::RPC::CombatHitReport> _reports;
        std::vector<Shared::RPC::CombatReaction> _sendingPublications;
        std::vector<Shared::RPC::CombatHitReport> _sendingReports;

        /** A blow the local player claimed: its copy of the victim already reacted, and the local body may already have recoiled. */
        struct RecentClaim {
            std::uint64_t at  = 0;
            bool recoilPlayed = false;
            // The reaction the contact ran on the copy, or kNoRow when the
            // copy took none.
            std::uint32_t hitRowIndex = Shared::RPC::CombatHitReport::kNoRow;
        };

        /** How long a claim is remembered: past this, a reaction to the same swing is not the claim's. */
        static constexpr std::uint64_t kClaimedReactionWindowMs = 1500;

        /** The local player's claim of `generation` on `victimNetworkId` within `kClaimedReactionWindowMs`, if there is one. */
        bool FindRecentClaim(std::uint64_t victimNetworkId, std::uint32_t generation, std::uint64_t now, RecentClaim &claim);

        // The local player's recent claims, by victim NetworkID and swing;
        // guarded by `_publicationMutex`.
        std::map<std::pair<std::uint64_t, std::uint32_t>, RecentClaim> _recentClaims;

        // The combo's next strike the local body's attack was handed, as it
        // goes out once its timeline begins; guarded by `_publicationMutex`.
        Shared::RPC::CombatReaction _comboStrike;
        bool _comboStrikeQueued = false;

        // The local player's swing in the air, read by a claim off the game thread.
        std::atomic<std::uint32_t> _localSwingGeneration {0};
        std::atomic<std::uint64_t> _localSwingTarget {0};

        // --- Timing and replay -------------------------------------------------

        /**
         * Builds and queues, as a replay the RunAction detour does not publish
         * again. Forced, whatever runs in the action's slot gives way; else the
         * engine's own `CanBeReplacedBy` decides.
         */
        static bool RunReplay(SDK::wh::combatmodule::C_CombatActor *fighter, SDK::wh::combatmodule::I_CombatActorActionPrivate *action, bool forced = true);

        /** Whether the body behind `fighter` is down -- dead or out cold -- and takes no replayed action. */
        static bool IsDown(const SDK::wh::combatmodule::C_CombatActor *fighter);

        /** Drops every counter request this machine raised on a copy whose strike-back had its time. */
        void DropExpiredRipostes(std::uint64_t now);

        // The time scales owed to replayed swings whose timelines have not
        // begun yet; a deferred Enter consumes one off the main thread.
        ForcedAttackSpeeds<SDK::wh::combatmodule::C_CombatActor> _forcedAttackSpeeds;

        // The swing each remote fighter is playing here, read by the contact.
        SwingGenerations<SDK::wh::combatmodule::C_CombatActor> _swingGenerations;

        // By attacker, the last of its swings a copy here blocked, natively
        // or by replaying its owner's block.
        SwingGenerations<SDK::wh::combatmodule::C_CombatActor> _blockedSwings;

        // By victim, the last of the local player's swings whose blow a copy
        // here showed by replaying its owner's reaction.
        SwingGenerations<SDK::wh::combatmodule::C_CombatActor> _hitSwings;

        /** A copy whose counter request this machine raised, and when it is dropped. */
        struct RaisedRiposte {
            std::uint64_t networkId = 0;
            std::uint64_t dropAt    = 0;
        };

        static constexpr std::uint64_t kRiposteRequestHoldMs = 600;
        std::vector<RaisedRiposte> _raisedRipostes;

        // Why the last ApplyRemoteReaction deferred; always a literal.
        const char *_lastDeferral = "";

        // --- The local body's defence ------------------------------------------

        /** Samples whether the local body holds its block, for the answer to a claim on it. */
        void SampleGuard(const SDK::wh::combatmodule::C_CombatActor *localFighter, std::uint64_t now);

        // The local body's recent defence; written on the game thread and by
        // reaction publishers, read when the server asks about a claim.
        std::mutex _guardHistoryMutex;
        CombatGuardHistory _guardHistory;

        // Each authoritative fighter's last block against its opponent.
        PerfectBlockRows<SDK::wh::combatmodule::C_CombatActor> _perfectBlockRows;

        // --- The referee's rulings ---------------------------------------------

        /** A ruling waiting on the game thread, with the recoil it may still owe. */
        struct PendingRuling {
            Shared::RPC::CombatOutcome outcome;
            std::uint64_t receivedAt = 0;

            bool healthApplied = false;
            bool impactPlayed  = false;
            bool hitPlayed     = false;

            /** The replayed record ran the victim's reaction itself, so no ruled hit follows it. */
            bool recordReacted = false;

            bool recoilPlayed = false;
        };

        /**
         * How long a ruled blow waits for this machine to play the swing that
         * dealt it: the presentation stream's own budget for a swing the body
         * keeps refusing (`kNativeRetryMaximumMs`), past which it never plays.
         */
        static constexpr std::uint64_t kRuledSwingWaitMs = 1000;

        /** The most rulings the network thread may queue between two ticks. */
        static constexpr std::size_t kMaxQueuedRulings = 1024;

        /** Queues a ruling from the network thread. */
        void QueueRuling(const Shared::RPC::CombatOutcome &outcome);

        /** Takes the rulings the network thread queued and plays what each still owes. */
        void DrainRulings(std::uint64_t now);

        /** Plays what is left of one ruling. True once nothing more can come of it. */
        bool ApplyRuling(PendingRuling &ruling, std::uint64_t now);

        /** The health the ruling takes, off the local body. */
        void TakeRuledHealth(const Shared::RPC::CombatOutcome &outcome);

        /** The recoil the ruling owes the attacker's body here. Applied, still owed, or never possible. */
        ReactionResult RunRuledRecoil(const Shared::RPC::CombatOutcome &outcome);

        /** After a claim: the hit reaction the ruling owes the victim's body here. */
        ReactionResult RunRuledHit(const Shared::RPC::CombatOutcome &outcome);

        /**
         * The blow, delivered to the victim's body here the way the engine
         * delivers its own: the record queued to the game rules, which the
         * body reacts to and plays the impact from. False when there is no
         * body here or no record to rebuild.
         */
        bool DeliverRuledHit(const Shared::RPC::CombatOutcome &outcome, bool runsReaction) const;

        // Rulings from the network thread, and those still owing a recoil.
        std::mutex _rulingMutex;
        /** A cancel, dated on both clocks: presentations are shifted onto this machine's, rulings stay on the server's. */
        struct Cancellation {
            Shared::RPC::CombatCancel command;
            std::uint64_t localAt = 0;
        };
        bool WasRuledCancelled(std::uint64_t first, std::uint64_t second, std::uint64_t ruledAt) const;
        void DrainCancellations();
        void CancelPair(const Cancellation &cancellation);
        std::vector<Cancellation> _incomingCancellations;
        std::map<std::pair<std::uint64_t, std::uint64_t>, Cancellation> _cancelledAt;
        std::set<std::pair<std::uint64_t, std::uint64_t>> _cancelledTargets;

        // Remote players a client script took out of the picker.
        std::set<std::uint64_t> _untargetable;
        std::vector<Shared::RPC::CombatOutcome> _incomingRulings;
        std::vector<Shared::RPC::CombatOutcome> _drainingRulings;
        std::deque<PendingRuling> _rulings;
        std::uint64_t _lastRuling = 0;

        // Each describes one native call on one thread.
        static thread_local const SDK::wh::entitymodule::C_Actor *_offeredCandidate;
        static thread_local SDK::wh::combatmodule::C_CombatActor *_resolvingAttacker;
        static thread_local BlowCapture *_blowCapture;
        static thread_local bool _replaying;
        static thread_local bool _givingWay;
        static thread_local bool _blockSlotsOnly;
        static thread_local float _localAttackSpeed;
        static thread_local const SDK::wh::combatmodule::C_CombatActor *_postUpdatingFighter;
        static thread_local const SDK::wh::combatmodule::C_CombatActor *_runningActionFighter;
    };
} // namespace KCDC::Features::Player
