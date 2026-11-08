#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>

namespace KCDC::Common::Spawn {
    /**
     * Per-frame ceiling on spawn work.
     *
     * This is the one thing the game's own spawner queue does that this mod's
     * spawners did not. `wh::xgenaimodule::C_EntitySpawnerQueue` exists for no
     * other reason: its drain (`0x1820B94B0`) gives itself a deadline of
     * `wh_ai_EntitySpawnerTimeBudget` milliseconds, executes one request per
     * iteration, and leaves whatever does not fit for the next frame.
     *
     * Draining a batch unconditionally turns a lobby arriving at once into
     * that many `SpawnEntity` + `InitEntity` + character loads inside a single
     * `PostUpdate`, which is a visible hitch rather than a slower join.
     *
     * The budget never blocks the first command of a frame, so a single spawn
     * costing more than the whole budget still makes progress instead of
     * deadlocking the queue against itself.
     */
    class SpawnBudget final {
      public:
        explicit SpawnBudget(std::chrono::microseconds budget): _deadline(std::chrono::steady_clock::now() + budget) {}

        /** True once something has run and the deadline has passed. */
        bool Exhausted() const {
            return _executed > 0 && std::chrono::steady_clock::now() >= _deadline;
        }

        void Charge() {
            ++_executed;
        }

      private:
        std::chrono::steady_clock::time_point _deadline;
        std::size_t _executed = 0;
    };

    /**
     * What one frame may spend draining one spawner. Both spawners are drained
     * back to back in `Application::PostUpdate`, so a frame that has work
     * waiting in both can spend twice this.
     *
     * Deliberately smaller than one cold actor spawn: the point is to cap the
     * damage of a batch, not to fit a fixed number of spawns into a frame.
     */
    inline constexpr std::chrono::microseconds kSpawnFrameBudget {2000};

    /**
     * How long a replica waits before asking for a body again, spread by its
     * own network id.
     *
     * Without the spread every body that fails together comes back together.
     * The old backoff was a flat second set *before* the attempt, so a hundred
     * bodies refused by the same level-load spawn lock all realigned to the
     * same deadline and retried in lockstep, once a second, forever. Keying the
     * offset off the network id is enough to break the convoy and costs no
     * state.
     */
    inline std::uint64_t SpawnRetryDelayMs(std::uint64_t networkId) {
        return 1000 + (networkId % 500);
    }
} // namespace KCDC::Common::Spawn
