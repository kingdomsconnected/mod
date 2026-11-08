#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

namespace KCDC::Shared::Game {
    /** The locomotion set a player body plays on foot. */
    enum class LocomotionGait : std::uint8_t {
        Walk,
        Run,
        Sprint,
        StealthWalk,
        StealthRun,
    };

    /**
     * The travel speeds one locomotion blendspace animates, generated from
     * Animations.pak (tools/generate_locomotion_bands.py).
     *
     * CAnimatedCharacter::SetDesiredLocalLocation hands the blendspace the
     * body's requested travel speed as `eMotionParamID_TravelSpeed`
     * at 0x18083B82C. Outside `[minSpeed, maxSpeed]`, the stride stays at
     * the nearest limit regardless of the body's actual speed.
     */
    struct LocomotionBand {
        bool female;
        LocomotionGait gait;
        const char *blendSpace;
        float minSpeed;
        float maxSpeed;
    };

    extern const std::vector<LocomotionBand> g_LocomotionBands;

    /**
     * The set a body plays for what its owner holds: the crouch picks the
     * stealth set, then the sprint, then the walk/jog toggle
     * (`PlayerInput::run`, C_PlayerInput+0x7C, on for a jog).
     */
    inline LocomotionGait GaitForInput(bool run, bool sprint, bool crouch) {
        if (crouch) {
            return run ? LocomotionGait::StealthRun : LocomotionGait::StealthWalk;
        }
        if (sprint) {
            return LocomotionGait::Sprint;
        }
        return run ? LocomotionGait::Run : LocomotionGait::Walk;
    }

    /**
     * The band of that set on that body, or nullptr. The female body has no
     * stealth locomotion of its own, so its crouched sets fall back to its
     * relaxed walk and jog.
     */
    inline const LocomotionBand *FindLocomotionBand(bool female, LocomotionGait gait) {
        for (const LocomotionBand &band : g_LocomotionBands) {
            if (band.female == female && band.gait == gait) {
                return &band;
            }
        }
        if (gait == LocomotionGait::StealthWalk) {
            return FindLocomotionBand(female, LocomotionGait::Walk);
        }
        if (gait == LocomotionGait::StealthRun) {
            return FindLocomotionBand(female, LocomotionGait::Run);
        }
        return nullptr;
    }

    /**
     * How fast a remote body's position correction may move it, in m/s.
     *
     * The correction goes into the body's movement as forced movement, after
     * its travel speed was taken from the replayed input
     * (NativePlayerModule::SubmitCharacterMoveRequest), so the stride never
     * shows it: every metre per second of it is a metre per second the feet
     * slide. Large corrections make a walking body visibly skate.
     *
     * So the correction is held to what keeps the body's speed inside the
     * band its locomotion animates: added to a travel speed `s` in any
     * direction it gives a speed within `s +/- c`, and that stays in the band
     * for `c <= min(max - s, s - min)`. A body below its band -- standing,
     * or slower than the set its input names -- gets `kMinCorrectionSpeed`
     * so a gap still closes. Past `kGentleSnapShare` of the snap distance,
     * the limit rises toward `maxLerpSpeed`, reaching it at the snap. A gap
     * beyond the gentle correction rate closes faster before it would snap.
     */
    inline constexpr float kMinCorrectionSpeed = 0.5F;
    inline constexpr float kGentleSnapShare    = 0.5F;

    inline float CorrectionSpeedLimit(const LocomotionBand *band, float travelSpeed, float errorLength, float snapDistance, float maxLerpSpeed) {
        float headroom = 0.0F;
        if (band != nullptr && travelSpeed >= band->minSpeed && travelSpeed <= band->maxSpeed) {
            headroom = std::min(band->maxSpeed - travelSpeed, travelSpeed - band->minSpeed);
        }
        float limit            = std::max(kMinCorrectionSpeed, headroom);
        const float rampStart  = kGentleSnapShare * snapDistance;
        const float rampLength = snapDistance - rampStart;
        if (errorLength > rampStart && rampLength > 0.0F) {
            const float t = std::min(1.0F, (errorLength - rampStart) / rampLength);
            limit += (maxLerpSpeed - limit) * t;
        }
        return std::min(limit, maxLerpSpeed);
    }
} // namespace KCDC::Shared::Game
