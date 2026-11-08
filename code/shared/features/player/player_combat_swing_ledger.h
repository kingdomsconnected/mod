#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace KCDC::Shared::RPC {
    /**
     * The swings one attacker has in flight, as the server relayed them: the
     * one thing a blow either player reports can be checked against.
     *
     * Each relayed swing opens a window that runs from the relay to its own
     * withdraw -- the moment the blade can no longer land -- plus the
     * `slackMs` a victim needs to see the swing and answer it. Each victim may
     * be struck by a swing once. A swing past its window, or one the server
     * never relayed, struck nobody.
     *
     * A generation opens one window, once: a swing the ledger already holds
     * is never opened again, so it cannot be struck again by sending it
     * twice. And no attacker opens more than `kMaxSwingsPerSecond` windows a
     * second, which no fighter's arms can outpace.
     */
    struct CombatSwingLedger {
        /**
         * Swings kept per attacker: eight seconds of them at the most the
         * rate allows, longer than any window and its slack stay open, so a
         * swing is still held -- and still refused when sent again -- for as
         * long as it could strike.
         */
        static constexpr std::size_t kMaxSwings = 64;

        /** Victims one swing may strike: a sweep through a crowd. */
        static constexpr std::size_t kMaxVictims = 4;

        /** The longest window a swing may claim: no authored attack withdraws later. */
        static constexpr std::uint32_t kMaxWindowMs = 4000;

        /** Windows one attacker may open in any second: a combo, a riposte and a master strike together stay well under it. */
        static constexpr std::size_t kMaxSwingsPerSecond = 8;

        /** Contacts one master strike may land on its victim: the pair's touch, then the strike. */
        static constexpr std::uint8_t kSyncStrikesPerVictim = 3;

        enum class Result : std::uint8_t {
            Accepted,
            UnknownSwing,
            Expired,
            AlreadyStruck,
            TooManyVictims,
        };

        struct Swing {
            std::uint32_t generation      = 0;
            std::uint64_t relayedAt       = 0;
            std::uint32_t windowMs        = 0;
            std::uint8_t strikesPerVictim = 1;
            std::uint8_t victimCount      = 0;

            /** A riposte's strike-back: the one swing the engine prices charged. */
            bool counter = false;
            std::array<std::uint64_t, kMaxVictims> victims {};
            std::vector<std::uint64_t> cancelledVictims;
        };

        /**
         * Opens a swing the server relayed at `now`, landing no later than
         * `windowMs` after, and striking each victim at most
         * `strikesPerVictim` times; `counter` for a riposte's strike-back.
         * False for a generation the ledger already holds, and for one past
         * the attacker's rate.
         */
        bool Relay(std::uint32_t generation, std::uint64_t now, std::uint32_t windowMs, std::uint8_t strikesPerVictim = 1, bool counter = false) {
            if (generation == 0 || Find(generation) != nullptr) {
                return false;
            }
            const auto lastSecond = std::count_if(swings.begin(), swings.end(), [now](const Swing &swing) {
                return swing.generation != 0 && now >= swing.relayedAt && now - swing.relayedAt < 1000;
            });
            if (static_cast<std::size_t>(lastSecond) >= kMaxSwingsPerSecond) {
                return false;
            }
            Swing &swing           = swings[next];
            swing                  = Swing {};
            swing.generation       = generation;
            swing.relayedAt        = now;
            swing.windowMs         = std::min(windowMs, kMaxWindowMs);
            swing.strikesPerVictim = std::max<std::uint8_t>(strikesPerVictim, 1);
            swing.counter          = counter;
            next                   = (next + 1) % kMaxSwings;
            return true;
        }

        /** What `Strike` would answer, recording nothing: whether a claim is worth holding for a ruling. */
        Result Check(std::uint32_t generation, std::uint64_t victim, std::uint64_t now, std::uint32_t slackMs) const {
            const Swing *swing = Find(generation);
            if (swing == nullptr) {
                return Result::UnknownSwing;
            }
            if (now < swing->relayedAt || now - swing->relayedAt > static_cast<std::uint64_t>(swing->windowMs) + slackMs) {
                return Result::Expired;
            }
            if (StrikesOn(*swing, victim) >= swing->strikesPerVictim) {
                return Result::AlreadyStruck;
            }
            if (std::find(swing->cancelledVictims.begin(), swing->cancelledVictims.end(), victim) != swing->cancelledVictims.end()) {
                return Result::AlreadyStruck;
            }
            if (swing->victimCount >= kMaxVictims) {
                return Result::TooManyVictims;
            }
            return Result::Accepted;
        }

        /** Rules on `victim` being struck at `now` by `generation`, and records it when accepted. */
        Result Strike(std::uint32_t generation, std::uint64_t victim, std::uint64_t now, std::uint32_t slackMs) {
            const Result result = Check(generation, victim, now, slackMs);
            if (result == Result::Accepted) {
                Swing *swing                         = Find(generation);
                swing->victims[swing->victimCount++] = victim;
            }
            return result;
        }

        /** How many times `generation` has struck `victim`; zero for a swing the ledger does not hold. */
        std::uint8_t StrikesOn(std::uint32_t generation, std::uint64_t victim) const {
            const Swing *swing = Find(generation);
            return swing != nullptr ? StrikesOn(*swing, victim) : 0;
        }

        /** Whether `generation` is a riposte's strike-back the ledger holds. */
        bool IsCounter(std::uint32_t generation) const {
            const Swing *swing = Find(generation);
            return swing != nullptr && swing->counter;
        }

        void Reset() {
            swings = {};
            next   = 0;
        }

        /** A truce retires only this opponent from swings already in flight. */
        void CancelAgainst(std::uint64_t victim) {
            for (Swing &swing : swings) {
                if (swing.generation != 0 && std::find(swing.cancelledVictims.begin(), swing.cancelledVictims.end(), victim) == swing.cancelledVictims.end()) {
                    swing.cancelledVictims.push_back(victim);
                }
            }
        }

        static const char *ResultName(Result result) {
            switch (result) {
            case Result::Accepted: return "accepted";
            case Result::UnknownSwing: return "no such swing was relayed";
            case Result::Expired: return "the swing had withdrawn";
            case Result::AlreadyStruck: return "the swing already struck this victim";
            case Result::TooManyVictims: return "the swing already struck as many as one swing can";
            }
            return "unknown";
        }

        std::array<Swing, kMaxSwings> swings {};
        std::size_t next = 0;

      private:
        const Swing *Find(std::uint32_t generation) const {
            if (generation == 0) {
                return nullptr;
            }
            const auto found = std::find_if(swings.begin(), swings.end(), [generation](const Swing &swing) {
                return swing.generation == generation;
            });
            return found != swings.end() ? &*found : nullptr;
        }

        Swing *Find(std::uint32_t generation) {
            return const_cast<Swing *>(static_cast<const CombatSwingLedger *>(this)->Find(generation));
        }

        static std::uint8_t StrikesOn(const Swing &swing, std::uint64_t victim) {
            std::uint8_t struck = 0;
            for (std::uint8_t i = 0; i < swing.victimCount; ++i) {
                struck += swing.victims[i] == victim ? 1 : 0;
            }
            return struck;
        }
    };

    /**
     * How long, in milliseconds, a swing's blade can still land after the
     * attacker's engine committed it: the time to the hit at the swing's own
     * speed, then the authored span from hit to withdraw, which the timeline
     * does not scale (`C_CombatActorTimeCop::BeginAttackTimeline`).
     */
    inline std::uint32_t CombatSwingWindowMs(float timeToHit, float timeToWithdraw, float attackSpeed) {
        if (!std::isfinite(timeToHit) || !std::isfinite(timeToWithdraw) || timeToHit < 0.0F) {
            return 0;
        }
        const float speed   = std::isfinite(attackSpeed) && attackSpeed > 0.0F ? attackSpeed : 1.0F;
        const float seconds = (timeToHit / speed) + std::max(timeToWithdraw - timeToHit, 0.0F);
        return static_cast<std::uint32_t>(std::lround(std::clamp(seconds * 1000.0F, 0.0F, static_cast<float>(CombatSwingLedger::kMaxWindowMs))));
    }
} // namespace KCDC::Shared::RPC
