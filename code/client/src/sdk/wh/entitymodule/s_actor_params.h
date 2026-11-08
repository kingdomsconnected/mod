#pragma once

#include "../../types.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    /**
     * One set of dynamic arm aim poses, the left, right and both-arms
     * animation names a mode of the actor aims with.
     */
    struct S_DynamicAimPose {
        SDK::CryStringT<char> m_leftArmAimPose;  // 0x00
        SDK::CryStringT<char> m_rightArmAimPose; // 0x08
        SDK::CryStringT<char> m_bothArmsAimPose; // 0x10
    };

    static_assert(sizeof(S_DynamicAimPose) == 0x18, "S_DynamicAimPose size mismatch");

    /**
     * One of the six 0x28-byte entries `S_ActorParams` carries at +0x48,
     * built by 0x180913F98. Nothing the script reader writes reaches them,
     * so only the constructor's defaults are known.
     */
    struct S_ActorParamsEntry0048 {
        std::uint64_t m_value0000; // 0x00, ctor zeroes
        float m_value0008;         // 0x08, ctor sets 1.0
        std::uint32_t m_value000C; // 0x0C, ctor zeroes
        void *m_values0010[3];     // 0x10, ctor zeroes all three
    };

    static_assert(sizeof(S_ActorParamsEntry0048) == 0x28, "S_ActorParamsEntry0048 size mismatch");

    /**
     * The actor's look, aim and turning parameters, embedded at
     * `C_Actor+0x288`.
     *
     * Constructed with its defaults by 0x180F2CEB4, the same constructor the
     * per-class "Game Params" cache (0x180F2CB04) runs before 0x180F2D87C
     * fills it from the entity script's `gameParams` table. Every name below
     * is the script key that reader stores into the field, and the angles are
     * read in degrees and stored in radians.
     */
    struct S_ActorParams {
        S_DynamicAimPose m_idleDynamicAimPose; // 0x00, "idleLeftArmAimPose" and its siblings
        S_DynamicAimPose m_runDynamicAimPose;  // 0x18, "runLeftArmAimPose" and its siblings
        SDK::CryStringT<char> m_string0030;    // 0x30, built empty and never read by the script reader
        SDK::CryStringT<char> m_lookAtSimpleHeadBoneName; // 0x38, "lookAtSimpleHeadBone", "Bip01 Head" by default
        float m_param0040; // 0x40, 1.0 by default
        float m_param0044; // 0x44, 1.0 by default
        S_ActorParamsEntry0048 m_entries0048[6]; // 0x48, each built by 0x180913F98
        float m_lookFOVRadians;  // 0x138, "lookFOV"
        float m_aimFOVRadians;   // 0x13C, "aimFOV", pi by default
        std::int32_t m_cornerSmoother; // 0x140, "cornerSmoother", 1 by default
        std::uint32_t m_aimIKLayer;    // 0x144, "aimIKLayer", 14 by default
        std::uint32_t m_lookIKLayer;   // 0x148, "lookIKLayer", 15 by default
        float m_bothArmsAimHalfFOV;    // 0x14C, half of "bothArmsAimFOV"
        float m_bothArmsAimPitchFactor; // 0x150, "bothArmsAimPitchFactor"
        bool m_flag0154; // 0x154, true by default
        bool m_flag0155; // 0x155, true by default
        bool m_canUseComplexLookIK; // 0x156, "canUseComplexLookIK"
        bool m_useDynamicAimPoses;  // 0x157, set when any dynamic aim pose was read
        float m_turnThresholdAngle; // 0x158, "turnThresholdAngle"
        float m_minimumAngleForTurnWithoutDelay; // 0x15C, "minimumAngleForTurnWithoutDelay", never below the threshold angle
        float m_turnThresholdTime;     // 0x160, "turnThresholdTime"
        float m_stepThresholdDistance; // 0x164, "stepThresholdDistance"
        float m_stepThresholdTime;     // 0x168, "stepThresholdTime"
        float m_maxDeltaAngleRateNormal;     // 0x16C, "maxDeltaAngleRateNormal"
        float m_maxDeltaAngleRateAnimTarget; // 0x170, "maxDeltaAngleRateAnimTarget"
        float m_maxDeltaAngleMultiplayer;    // 0x174, "maxDeltaAngleMultiplayer"
        float m_maxDeltaAngleRateJukeTurn;   // 0x178, "maxDeltaAngleRateJukeTurn"
        float m_animatedCharacterTurnSpeedSmoothingTime; // 0x17C, "animatedCharacterTurnSpeedSmoothingTime"
        bool m_smoothedZTurning; // 0x180, "smoothedZTurning"

        // The movement styles this body is allowed, written by
        // SetMovementRestriction and read by the style gate every query.
        std::uint8_t m_allowSprint; // 0x181, C_Actor+0x409
        std::uint8_t m_allowRun;    // 0x182, C_Actor+0x40A
        std::uint8_t m_flag0183;    // 0x183, true by default
        std::byte m_padding0184[0x4];

        static void AssertLayout() {
            static_assert(offsetof(S_ActorParams, m_lookAtSimpleHeadBoneName) == 0x38, "S_ActorParams::m_lookAtSimpleHeadBoneName offset mismatch");
            static_assert(offsetof(S_ActorParams, m_entries0048) == 0x48, "S_ActorParams::m_entries0048 offset mismatch");
            static_assert(offsetof(S_ActorParams, m_lookFOVRadians) == 0x138, "S_ActorParams::m_lookFOVRadians offset mismatch");
            static_assert(offsetof(S_ActorParams, m_canUseComplexLookIK) == 0x156, "S_ActorParams::m_canUseComplexLookIK offset mismatch");
            static_assert(offsetof(S_ActorParams, m_turnThresholdAngle) == 0x158, "S_ActorParams::m_turnThresholdAngle offset mismatch");
            static_assert(offsetof(S_ActorParams, m_animatedCharacterTurnSpeedSmoothingTime) == 0x17C, "S_ActorParams::m_animatedCharacterTurnSpeedSmoothingTime offset mismatch");
            static_assert(offsetof(S_ActorParams, m_allowSprint) == 0x181, "S_ActorParams::m_allowSprint offset mismatch");
        }
    };

    static_assert(sizeof(S_ActorParams) == 0x188, "S_ActorParams size mismatch");
} // namespace SDK::wh::entitymodule
