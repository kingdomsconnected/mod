#pragma once

#include "shared/features/player/player_combat_claim.h"
#include "shared/features/player/player_combat_reaction.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// The decisions of player-versus-player melee, kept apart from the engine so
// they can be stated -- and tested -- on their own. Nothing here dereferences
// a fighter or a body: they are compared as values, which is also what makes
// a pointer read off a model whose body has already gone harmless.
namespace KCDC::Features::Player {
    /**
     * Who is who on this machine as of one tick, and every rule that follows
     * from it. `TFighter` is the combat side of a body, `TBody` the body the
     * local opponent picker weighs.
     */
    template <typename TFighter, typename TBody>
    class DuelRoster {
      public:
        void Clear() {
            _remoteFighters.clear();
            _authorities.clear();
            _npcFences.clear();
            _networkIds.clear();
            _pickableBodies.clear();
            _stanceHolds.clear();
            _lockHolds.clear();
            _localFighter         = nullptr;
            _localNetworkId       = 0;
            _localTargetNetworkId = 0;
            _engagedBody          = nullptr;
            _engagedNetworkId     = 0;
        }

        void SetLocal(TFighter *fighter, std::uint64_t networkId) {
            _localFighter   = fighter;
            _localNetworkId = networkId;
            if (fighter != nullptr && networkId != 0) {
                _networkIds[fighter] = networkId;
            }
        }

        /** A replicated player's fighter; `pickable` when the local picker may weigh its body. */
        void AddRemote(TFighter *fighter, std::uint64_t networkId, TBody *body, bool pickable) {
            if (fighter == nullptr) {
                return;
            }
            _remoteFighters.insert(fighter);
            _networkIds[fighter] = networkId;
            if (pickable && body != nullptr) {
                _pickableBodies.insert(body);
            }
        }

        void AddNpc(TFighter *fighter, std::uint64_t networkId, bool authority, std::uint32_t incarnation = 0, std::uint8_t epoch = 0, std::uint32_t revision = 0, float maximumHealth = 100.0F) {
            if (fighter == nullptr) {
                return;
            }
            _networkIds[fighter] = networkId;
            _npcFences[fighter]  = {incarnation, epoch, revision, maximumHealth};
            if (authority) {
                _authorities.insert(fighter);
            }
            else {
                _remoteFighters.insert(fighter);
            }
        }

        struct NpcFence {
            std::uint32_t incarnation;
            std::uint8_t epoch;
            std::uint32_t revision;
            float maximumHealth;
        };
        const NpcFence *NpcIdentity(const TFighter *fighter) const {
            const auto found = _npcFences.find(fighter);
            return found != _npcFences.end() ? &found->second : nullptr;
        }

        bool IsAuthority(const TFighter *fighter) const {
            return IsLocal(fighter) || _authorities.contains(fighter);
        }

        bool IsNetworked(const TFighter *fighter) const {
            return fighter != nullptr && _networkIds.contains(fighter);
        }

        bool CanPublishReaction(const TFighter *fighter, const TFighter *opponent) const {
            return fighter != opponent && IsAuthority(fighter) && IsNetworked(opponent);
        }

        void SetLocalTarget(std::uint64_t networkId) {
            _localTargetNetworkId = networkId;
        }

        /** Owner-requested stance, published for native combat worker hooks. */
        void HoldRemoteStance(const TFighter *fighter) {
            if (IsRemote(fighter)) {
                _stanceHolds.insert(fighter);
            }
        }

        bool HoldsRemoteStance(const TFighter *fighter) const {
            return _stanceHolds.contains(fighter);
        }

        /** The owner's own lock, which the copy is handed; published for native combat worker hooks. */
        void HoldRemoteLock(const TFighter *fighter) {
            if (IsRemote(fighter)) {
                _lockHolds.insert(fighter);
            }
        }

        bool HoldsRemoteLock(const TFighter *fighter) const {
            return _lockHolds.contains(fighter);
        }

        /** The replicated player the local one is in a fight with, and its body. */
        void SetEngaged(TBody *body, std::uint64_t networkId) {
            _engagedBody      = body;
            _engagedNetworkId = body != nullptr ? networkId : 0;
        }

        TBody *EngagedBody() const {
            return _engagedBody;
        }

        std::uint64_t EngagedNetworkId() const {
            return _engagedNetworkId;
        }

        TFighter *LocalFighter() const {
            return _localFighter;
        }

        std::uint64_t LocalNetworkId() const {
            return _localNetworkId;
        }

        std::uint64_t LocalTargetNetworkId() const {
            return _localTargetNetworkId;
        }

        const std::unordered_set<const TBody *> &PickableBodies() const {
            return _pickableBodies;
        }

        bool IsRemote(const TFighter *fighter) const {
            return fighter != nullptr && _remoteFighters.count(fighter) != 0;
        }

        bool IsLocal(const TFighter *fighter) const {
            return fighter != nullptr && fighter == _localFighter;
        }

        bool IsPickable(const TBody *body) const {
            return body != nullptr && _pickableBodies.count(body) != 0;
        }

        /** The network id a fighter stands for here, or 0. */
        std::uint64_t NetworkIdOf(const TFighter *fighter) const {
            const auto found = _networkIds.find(fighter);
            return found != _networkIds.end() ? found->second : 0;
        }

        // --- Rules --------------------------------------------------------------

        /** Paired native clinches have no network replay; replicated combatants are excluded. */
        bool IsPlayerPair(const TFighter *fighter, const TFighter *opponent) const {
            return fighter != opponent && IsNetworked(fighter) && IsNetworked(opponent);
        }

        /** Whether a fight has a replicated player's body on either side, the local one's lock included. */
        bool InvolvesRemote(const TFighter *fighter, const TFighter *opponent) const {
            if (fighter == nullptr) {
                return false;
            }
            return IsRemote(fighter) || IsRemote(opponent) || (IsLocal(fighter) && _localTargetNetworkId != 0);
        }

        /**
         * Whether a contact is dropped here: one remote player's blow on
         * another's body. Their two machines resolve it and the ruling plays
         * it here; resolved here too, it would play twice, from the copies'
         * positions. The local player's blow on a remote body is claimed, and
         * NPC contacts resolve only on their victim's authority.
         */
        bool ShouldDropContact(const TFighter *attacker, const TFighter *victim) const {
            return IsRemote(attacker) && IsRemote(victim);
        }

        /** The attacker whose blow this machine resolves and publishes, or null. */
        TFighter *ResolvingAttacker(TFighter *attacker, const TFighter *victim) const {
            return IsAuthority(victim) && IsNetworked(attacker) && attacker != victim ? attacker : nullptr;
        }

        /**
         * Whether an attacker's planner may post every window it scheduled
         * onto `opponent`: when no replicated body is involved, and onto the
         * local body, whose windows only its own input fires. Onto a
         * replicated body it may post the plain block alone (`BlocksOnly`).
         */
        bool MayDispatchSlots(const TFighter *attacker, const TFighter *opponent) const {
            if (!IsRemote(attacker) && !IsRemote(opponent)) {
                return true;
            }
            return IsAuthority(opponent);
        }

        /**
         * Whether the windows onto `opponent` are cut to the plain block: a
         * replicated body's block trigger fires on the guard its owner holds,
         * which is the owner's own block, while a perfect block, a master
         * strike, a riposte or a dodge fired there would be this machine
         * deciding them for the owner.
         */
        bool BlocksOnly(const TFighter *opponent) const {
            return IsRemote(opponent);
        }

        /**
         * Whether a lethal variant may be weighed: never in a fight with a
         * replicated player in it, and never against the local player either.
         * A player's life is the referee's on every machine, its own included,
         * so a finisher played on one is a death nobody here completes: the
         * body is left in the dying half of the sync, whose combat action
         * outranks the ragdoll and the stand-up after it.
         */
        bool MayWeighLethal(const TFighter *fighter, const TFighter *opponent) const {
            return fighter == nullptr || (!IsRemote(fighter) && !IsRemote(opponent) && !IsLocal(opponent));
        }

        /** Whether the local picker may treat `candidate` as hostile. */
        bool IsHostileCandidate(const TFighter *asker, const TFighter *candidate, const TBody *candidateBody) const {
            return asker != nullptr && IsLocal(asker) && IsRemote(candidate) && IsPickable(candidateBody);
        }

      private:
        std::unordered_map<const TFighter *, NpcFence> _npcFences;
        std::unordered_set<const TFighter *> _authorities;
        std::unordered_set<const TFighter *> _remoteFighters;
        std::unordered_set<const TFighter *> _stanceHolds;
        std::unordered_set<const TFighter *> _lockHolds;
        std::unordered_map<const TFighter *, std::uint64_t> _networkIds;
        std::unordered_set<const TBody *> _pickableBodies;
        TFighter *_localFighter             = nullptr;
        std::uint64_t _localNetworkId       = 0;
        std::uint64_t _localTargetNetworkId = 0;
        TBody *_engagedBody                 = nullptr;
        std::uint64_t _engagedNetworkId     = 0;
    };

    /**
     * Which replicated player the local one is fighting, kept between the
     * exchanges that say so: a swing either way, a lock either way. A fight
     * lasts `kHoldMs` past its last exchange; it ends sooner when the other
     * player walks out of reach, goes down or leaves. A swing is noted from
     * whichever thread commits it, hence the atomics; the two halves may be
     * read a moment apart, which at worst holds a fight one exchange longer.
     */
    class DuelEngagement {
      public:
        static constexpr std::uint64_t kHoldMs = 6000;

        /** An exchange with `networkId` at `now`; a new opponent replaces the old. */
        void Note(std::uint64_t networkId, std::uint64_t now) {
            if (networkId == 0) {
                return;
            }
            _networkId.store(networkId);
            _until.store(now + kHoldMs);
        }

        /** The opponent at `now`, or 0 once the fight has lapsed. */
        std::uint64_t Current(std::uint64_t now) const {
            return now < _until.load() ? _networkId.load() : 0;
        }

        void End() {
            _until.store(0);
            _networkId.store(0);
        }

      private:
        std::atomic<std::uint64_t> _networkId {0};
        std::atomic<std::uint64_t> _until {0};
    };

    /**
     * A rotation of rosters: one is filled while the others may still be read,
     * then published whole. A reader holds a roster only for one native call,
     * and a slot is refilled `TSlots - 1` publications after it was current.
     */
    template <typename TRoster, std::size_t TSlots>
    class RosterRing {
        static_assert(TSlots >= 2, "a ring needs a slot to fill besides the one being read");

      public:
        RosterRing()                              = default;
        RosterRing(const RosterRing &)            = delete;
        RosterRing &operator=(const RosterRing &) = delete;

        /** The slot to fill next: never the current one. Cleared by the caller. */
        TRoster &Next() {
            return _slots[_next];
        }

        /** Makes the slot just filled current. */
        void Publish() {
            _current.store(&_slots[_next], std::memory_order_release);
            _next = (_next + 1) % TSlots;
        }

        const TRoster &Current() const {
            return *_current.load(std::memory_order_acquire);
        }

        /** Every slot cleared, the first current. */
        template <typename TClear>
        void Reset(TClear &&clear) {
            for (TRoster &slot : _slots) {
                clear(slot);
            }
            _current.store(&_slots[0], std::memory_order_release);
            _next = 1;
        }

      private:
        std::array<TRoster, TSlots> _slots {};
        std::atomic<const TRoster *> _current {&_slots[0]};
        std::size_t _next = 1;
    };

    /**
     * Time scales owed to replayed swings whose timelines have not begun: the
     * sender's value is handed to the next timeline of that fighter and to no
     * other. A deferred timeline consumes it off the main thread, hence the
     * lock.
     */
    template <typename TFighter>
    class ForcedAttackSpeeds {
      public:
        /** Owes `speed` to `fighter`'s next timeline; an unknown (non-positive) speed owes nothing. */
        void Force(const TFighter *fighter, float speed) {
            if (fighter == nullptr || !(speed > 0.0F)) {
                return;
            }
            std::scoped_lock lock(_mutex);
            _speeds[fighter] = speed;
        }

        /** Hands out and forgets what `fighter` is owed; false when nothing is. */
        bool Take(const TFighter *fighter, float &speed) {
            std::scoped_lock lock(_mutex);
            const auto owed = _speeds.find(fighter);
            if (owed == _speeds.end()) {
                return false;
            }
            speed = owed->second;
            _speeds.erase(owed);
            return true;
        }

        void Forget(const TFighter *fighter) {
            std::scoped_lock lock(_mutex);
            _speeds.erase(fighter);
        }

        /** Forgets what is owed to every fighter `keep` says no to: one gone before its timeline began. */
        template <typename TKeep>
        void Retain(TKeep &&keep) {
            std::scoped_lock lock(_mutex);
            std::erase_if(_speeds, [&keep](const auto &entry) {
                return !keep(entry.first);
            });
        }

        void Clear() {
            std::scoped_lock lock(_mutex);
            _speeds.clear();
        }

        std::size_t Size() const {
            std::scoped_lock lock(_mutex);
            return _speeds.size();
        }

      private:
        mutable std::mutex _mutex;
        std::unordered_map<const TFighter *, float> _speeds;
    };

    /**
     * The swing each replicated fighter is playing on this machine, by the
     * attack generation its owner gave it: the name a blow it lands here is
     * reported under. Written on the game thread as a swing is replayed, read
     * by the contact handler on whichever thread the engine runs it, hence
     * the lock.
     */
    template <typename TFighter>
    class SwingGenerations {
      public:
        /** `fighter` is now swinging `generation`; zero is not a swing and is ignored. */
        void Note(const TFighter *fighter, std::uint32_t generation) {
            if (fighter == nullptr || generation == 0) {
                return;
            }
            std::scoped_lock lock(_mutex);
            _generations[fighter] = generation;
        }

        /** The generation `fighter` last swung here, or 0. */
        std::uint32_t Of(const TFighter *fighter) const {
            std::scoped_lock lock(_mutex);
            const auto found = _generations.find(fighter);
            return found != _generations.end() ? found->second : 0;
        }

        /** Forgets every fighter `keep` says no to. */
        template <typename TKeep>
        void Retain(TKeep &&keep) {
            std::scoped_lock lock(_mutex);
            std::erase_if(_generations, [&keep](const auto &entry) {
                return !keep(entry.first);
            });
        }

        void Clear() {
            std::scoped_lock lock(_mutex);
            _generations.clear();
        }

        std::size_t Size() const {
            std::scoped_lock lock(_mutex);
            return _generations.size();
        }

      private:
        mutable std::mutex _mutex;
        std::unordered_map<const TFighter *, std::uint32_t> _generations;
    };

    /**
     * The perfect block each fighter last played and against whom: its row,
     * the riposte row it strikes back with, and its candidate flag -- what a
     * receiver rebuilds it from. They belong to their fighter and opponent,
     * even on a shared simulator.
     */
    template <typename TFighter>
    class PerfectBlockRows {
      public:
        struct Rows {
            std::uint32_t row           = Shared::RPC::CombatReaction::kInvalidRowIndex;
            std::uint32_t riposteRow    = Shared::RPC::CombatReaction::kInvalidRowIndex;
            std::uint8_t candidateFlags = 0;
        };

        void Note(const TFighter *fighter, const TFighter *opponent, const Rows &rows) {
            std::scoped_lock lock(_mutex);
            _rows[fighter] = {opponent, rows};
        }

        /** What `fighter` last perfect-blocked `opponent` with; an invalid row when it did not. */
        Rows Find(const TFighter *fighter, const TFighter *opponent) const {
            std::scoped_lock lock(_mutex);
            const auto found = _rows.find(fighter);
            return found != _rows.end() && found->second.opponent == opponent ? found->second.rows : Rows {};
        }

        template <typename TKeep>
        void Retain(TKeep &&keep) {
            std::scoped_lock lock(_mutex);
            std::erase_if(_rows, [&keep](const auto &entry) {
                return !keep(entry.first, entry.second.opponent);
            });
        }

        void Clear() {
            std::scoped_lock lock(_mutex);
            _rows.clear();
        }

      private:
        struct Block {
            const TFighter *opponent;
            Rows rows;
        };
        mutable std::mutex _mutex;
        std::unordered_map<const TFighter *, Block> _rows;
    };

    /**
     * How long the local swing at a remote player is held back: the swing
     * reaches the defender through the server and the answer comes back the
     * same way, which is this machine's round trip plus the defender's -- the
     * latter taken to match -- plus the relay's own tick. Zero without a
     * round trip to base it on. The engine caps the hold itself.
     */
    inline float SwingHoldSeconds(std::int32_t roundTripMs, float marginSeconds) {
        if (roundTripMs < 0) {
            return 0.0F;
        }
        return (2.0F * static_cast<float>(roundTripMs) * 0.001F) + marginSeconds;
    }

    /**
     * A defender's own recent defence, as its machine saw it: the spans it
     * held its block, the perfect blocks it played and against whom, and
     * when each remote swing at it started playing here. What answers a
     * `CombatClaimQuery`: the claimed swing's span is judged against what
     * the defender really did during it.
     */
    class CombatGuardHistory final {
      public:
        /** How long anything is remembered. */
        static constexpr std::uint64_t kKeepMs = 4000;

        /** The share of a swing's window before which its blade cannot yet land. */
        static constexpr float kEarliestHitShare = 0.35F;

        /** Samples the block at `now`; a sample only marks a change. */
        void NoteBlocking(std::uint64_t now, bool blocking) {
            if (blocking == _blocking) {
                return;
            }
            _blocking = blocking;
            Push(_blocks, {now, blocking ? 1ULL : 0ULL});
        }

        /** A perfect block this machine's player played against `attacker`. */
        void NotePerfectBlock(std::uint64_t now, std::uint64_t attacker) {
            Push(_perfects, {now, attacker});
        }

        /** `attacker`'s swing `generation` started playing here at `now`, able to land for `windowMs`. */
        void NoteSwing(std::uint64_t now, std::uint64_t attacker, std::uint32_t generation, std::uint32_t windowMs) {
            Push(_swings, {now, attacker, generation, std::max<std::uint32_t>(windowMs, 1)});
        }

        /** How this machine defended against that swing; `known` false when it never played it. */
        Shared::RPC::CombatClaimAnswer Judge(std::uint64_t attacker, std::uint32_t generation) const {
            Shared::RPC::CombatClaimAnswer answer;
            for (const Swing &swing : _swings) {
                if (swing.attacker != attacker || swing.generation != generation) {
                    continue;
                }
                answer.known              = true;
                const std::uint64_t end   = swing.at + swing.windowMs;
                const std::uint64_t first = swing.at + static_cast<std::uint64_t>(static_cast<float>(swing.windowMs) * kEarliestHitShare);
                for (const Entry &perfect : _perfects) {
                    if (perfect.value == attacker && perfect.at >= swing.at && perfect.at <= end) {
                        answer.flags = Shared::RPC::CombatHitFlags::kBlocked | Shared::RPC::CombatHitFlags::kPerfectBlock;
                        return answer;
                    }
                }
                if (BlockingDuring(first, end)) {
                    answer.flags = Shared::RPC::CombatHitFlags::kBlocked;
                }
                return answer;
            }
            return answer;
        }

        /** Forgets everything older than `kKeepMs` before `now`. */
        void Trim(std::uint64_t now) {
            const auto old = [now](std::uint64_t at) {
                return at + kKeepMs < now;
            };
            // The newest block change is kept however old: it says what holds now.
            while (_blocks.size() > 1 && old(_blocks[1].at)) {
                _blocks.erase(_blocks.begin());
            }
            std::erase_if(_perfects, [&](const Entry &entry) {
                return old(entry.at);
            });
            std::erase_if(_swings, [&](const Swing &swing) {
                return old(swing.at);
            });
        }

      private:
        struct Entry {
            std::uint64_t at;
            std::uint64_t value;
        };

        struct Swing {
            std::uint64_t at;
            std::uint64_t attacker;
            std::uint32_t generation;
            std::uint32_t windowMs;
        };

        /** Bounds every list, whatever its owner forgets to trim. */
        static constexpr std::size_t kMaxEntries = 64;

        template <typename T>
        static void Push(std::vector<T> &entries, const T &entry) {
            if (entries.size() >= kMaxEntries) {
                entries.erase(entries.begin());
            }
            entries.push_back(entry);
        }

        /** Whether the block was up at any moment in [from, to]. */
        bool BlockingDuring(std::uint64_t from, std::uint64_t to) const {
            bool up = false;
            for (const Entry &change : _blocks) {
                if (change.at >= from) {
                    break;
                }
                up = change.value != 0;
            }
            if (up) {
                return true;
            }
            return std::any_of(_blocks.begin(), _blocks.end(), [from, to](const Entry &change) {
                return change.at >= from && change.at <= to && change.value != 0;
            });
        }

        std::vector<Entry> _blocks;
        std::vector<Entry> _perfects;
        std::vector<Swing> _swings;
        bool _blocking = false;
    };
} // namespace KCDC::Features::Player
