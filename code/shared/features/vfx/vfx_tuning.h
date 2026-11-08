#pragma once

#include <mafianet/bit_stream.h>

#include <cmath>

namespace KCDC::Shared::Game {
    /**
     * The knobs a caller turns on one playing particle effect: the fields of
     * CryEngine's `SpawnParams` that mean something without an editor.
     *
     * Every one is read when the emitter is created and never again, which is
     * why a change to any of them rebuilds the emitter rather than being pushed
     * at it.
     *
     * The bounds are the mod's, not the engine's. The engine accepts anything
     * finite; what these stop is a resource turning one effect into a
     * frame-rate incident on everyone else's machine.
     */
    struct VfxTuning {
        /** Multiplies every size the effect authored. */
        float scale = 1.0F;
        /** Multiplies how many particles are emitted. */
        float countScale = 1.0F;
        /** Multiplies emission speed. */
        float speedScale = 1.0F;
        /** Multiplies how fast the emitter's own time runs. */
        float timeScale = 1.0F;
        /** Feeds the effect's strength curves; -1 leaves them where the effect authored them. */
        float strength = -1.0F;
        /** Seconds between restarts of the whole emitter; 0 never restarts it. */
        float pulsePeriod = 0.0F;
        /** Start at the equilibrium state the effect would reach if it had already been running. What a fire wants. */
        bool prime = false;

        static constexpr float kMinScale       = 0.01F;
        static constexpr float kMaxScale       = 32.0F;
        static constexpr float kMaxCountScale  = 16.0F;
        static constexpr float kMaxSpeedScale  = 16.0F;
        static constexpr float kMaxTimeScale   = 16.0F;
        static constexpr float kMaxPulsePeriod = 600.0F;

        bool operator==(const VfxTuning &) const = default;

        bool IsSane() const {
            return InRange(scale, kMinScale, kMaxScale) && InRange(countScale, kMinScale, kMaxCountScale) && InRange(speedScale, 0.0F, kMaxSpeedScale) && InRange(timeScale, kMinScale, kMaxTimeScale) && InRange(strength, -1.0F, 1.0F) && InRange(pulsePeriod, 0.0F, kMaxPulsePeriod);
        }

        /** Serves the entity's delta channel and the burst RPC's plain bitstream alike, through the one FieldSerializer both offer. */
        template <typename Fields>
        void SerializeFields(Fields &fields) {
            fields.Field(scale);
            fields.Field(countScale);
            fields.Field(speedScale);
            fields.Field(timeScale);
            fields.Field(strength);
            fields.Field(pulsePeriod);
            fields.Field(prime);
        }

      private:
        static bool InRange(float value, float low, float high) {
            return std::isfinite(value) && value >= low && value <= high;
        }
    };
} // namespace KCDC::Shared::Game
