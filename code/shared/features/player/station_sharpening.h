#pragma once

#include <networking/replication/network_entity.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

namespace KCDC::Shared::Game {
    /**
     * The three Mannequin fragments a sharpening player plays, in the order
     * `C_Sharpening` queues them: the walk-in (`StartSharpeningGameplay`,
     * 0x182EB0C60, a `C_HumanSharpeningInAction` at priority 6), the loop
     * the wheel is worked in (`OnEntryAnimationFinished`, a
     * `C_HumanSharpeningAction` at priority 5 with flags 6) and the walk-out
     * (`BeginExitSequence`, 0x182EACB40, a `C_HumanSharpeningOutAction` at
     * priority 6). `ResolveItemHandlingFragmentIds` (0x180D81378) resolves
     * them by these names into the actor's fragment table at 0x24, 0x2C and
     * 0x28, which is where the three actions read them from. Each takes the
     * blade's suffix as its one fragment tag when it initialises
     * (`SetSharpeningFragmentWithMaterialTag`, 0x182EB0148). Only the male
     * database has them.
     */
    inline constexpr const char *kSharpeningInFragment   = "SharpeningMinigameIn";
    inline constexpr const char *kSharpeningLoopFragment = "SharpeningMinigame";
    inline constexpr const char *kSharpeningOutFragment  = "SharpeningMinigameOut";

    /**
     * The grindstone's own looping clip for a blade: `StartGrindstoneAnimation`
     * (0x182EB1C10) appends the blade's suffix -- the same string the
     * player's fragments are tagged with -- to this and starts it on the
     * wheel's layer 0.
     */
    inline std::string GrindstoneAnimationName(std::string_view bladeSuffix) {
        return std::string("grindstone_spinning_").append(bladeSuffix);
    }

    /**
     * The wheel clip's playback scale: `SetGrindstoneAnimationSpeed`
     * (0x182EB0888) multiplies the wheel's speed by the grindstone's
     * `fSpeedMultiplier` and hands it to `SetLayerPlaybackScale(0, ...)`.
     */
    inline float GrindstonePlaybackScale(float wheelSpeed, float speedMultiplier) {
        return std::isfinite(wheelSpeed) && std::isfinite(speedMultiplier) ? wheelSpeed * speedMultiplier : 0.0F;
    }

    /**
     * Where a sharpening owner's blade and wheel are, sampled with its pose:
     * the blend coordinates of the loop fragment it is playing, and the
     * wheel's speed. They move every frame, so they travel on the transform
     * channel, beside the pose they shape, and cost one bit while nobody is
     * at a grindstone.
     *
     * `C_Sharpening::UpdateSwordMotion` integrates and clamps them; the
     * motion parameters are the ones its setters write.
     */
    struct SharpeningMotion {
        static constexpr std::uint32_t kSteps = 254; // even, so a range's midpoint is exact

        bool active = false;

        /** `C_Sharpening` +0x8C, [-0.5, 0.5]: the body's `BlendWeight` (10,
         * 0x182EB0AA8). */
        float swordPosition = 0.0F;

        /** `C_Sharpening` +0x90, [0, 1]: `BlendWeight2` (11, 0x182EB0B30) on the body
         * and the wheel alike. */
        float swordRotation = 0.0F;

        /** `C_Sharpening` +0xAC, [0, 1]: the wheel's speed, decayed by
         * `UpdateWheelSpeed` and raised by each step. */
        float wheelSpeed = 0.0F;

        bool operator==(const SharpeningMotion &) const = default;

        void Serialize(Framework::Networking::Replication::FieldSerializer &fields) {
            fields.Field(active);
            if (!active) {
                *this = {};
                return;
            }
            Steps(fields, swordPosition, -0.5F, 0.5F);
            Steps(fields, swordRotation, 0.0F, 1.0F);
            Steps(fields, wheelSpeed, 0.0F, 1.0F);
        }

      private:
        /** `value` clamped into [low, high] as one of kSteps + 1 evenly spaced
         * levels, both ends exact. */
        static void Steps(Framework::Networking::Replication::FieldSerializer &fields, float &value, float low, float high) {
            std::uint32_t steps = 0;
            if (fields.Writing()) {
                const float clamped = std::isfinite(value) ? std::clamp(value, low, high) : low;
                steps               = static_cast<std::uint32_t>(std::lround((clamped - low) / (high - low) * static_cast<float>(kSteps)));
            }
            fields.BitsRange(steps, 0U, kSteps);
            if (!fields.Writing()) {
                value = low + (high - low) * static_cast<float>(std::min(steps, kSteps)) / static_cast<float>(kSteps);
            }
        }
    };
} // namespace KCDC::Shared::Game
