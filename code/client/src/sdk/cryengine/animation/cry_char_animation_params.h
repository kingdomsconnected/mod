#pragma once

#include <cstddef>
#include <cstdint>

namespace SDK {
    /**
     * KCD2 blend-space dimensions accepted by ISkeletonAnim.
     *
     * The ids are also proven by `CAnimatedCharacter::SetDesiredLocalLocation`
     * (0x18083B82C), which derives each value from the frame's desired
     * location and writes it to the skeleton under these numbers -- after
     * asking the request's prediction store for an override.
     */
    enum EMotionParamID : std::int32_t {
        eMotionParamID_TravelSpeed = 0,
        eMotionParamID_TurnSpeed = 1,
        eMotionParamID_TravelAngle = 2,
        eMotionParamID_TravelSlope = 3,
        eMotionParamID_TurnAngle = 4,
        eMotionParamID_TravelDist = 5,
        eMotionParamID_StopLeg = 6,
        eMotionParamID_UserParam0 = 7,
        eMotionParamID_UserParam1 = 8,
        eMotionParamID_SlopePerpendicular = 9,
        eMotionParamID_BlendWeight = 10,
        eMotionParamID_BlendWeight2 = 11,
        eMotionParamID_BlendWeight3 = 12,
        eMotionParamID_BlendWeight4 = 13,
        eMotionParamID_BlendWeight5 = 14,
        eMotionParamID_BlendWeight6 = 15,
        eMotionParamID_BlendWeight7 = 16,
        eMotionParamID_BlendWeight_Last = eMotionParamID_BlendWeight7,
        eMotionParamID_COUNT = 17,
        eMotionParamID_INVALID = eMotionParamID_COUNT,
    };

    static_assert(sizeof(EMotionParamID) == 0x4,
                  "EMotionParamID size mismatch");

    /** Playback and transition flags accepted by ISkeletonAnim::StartAnimation. */
    enum CA_AnimationFlags : std::uint32_t {
        CA_MANUAL_UPDATE = 0x000001,
        CA_LOOP_ANIMATION = 0x000002,
        CA_REPEAT_LAST_KEY = 0x000004,
        CA_TRANSITION_TIMEWARPING = 0x000008,
        CA_START_AT_KEYTIME = 0x000010,
        CA_START_AFTER = 0x000020,
        CA_IDLE2MOVE = 0x000040,
        CA_MOVE2IDLE = 0x000080,
        CA_ALLOW_ANIM_RESTART = 0x000100,
        CA_KEYFRAME_SAMPLE_30Hz = 0x000200,
        CA_DISABLE_MULTILAYER = 0x000400,
        CA_FORCE_SKELETON_UPDATE = 0x000800,
        CA_TRACK_VIEW_EXCLUSIVE = 0x001000,
        CA_REMOVE_FROM_FIFO = 0x002000,
        CA_FULL_ROOT_PRIORITY = 0x004000,
        CA_FORCE_TRANSITION_TO_ANIM = 0x008000,
        CA_FADEOUT = 0x40000000,
    };

    /**
     * Exact KCD2 parameters consumed by ISkeletonAnim::StartAnimation.
     *
     * Unlike newer CryEngine branches, this build has no interpolation-type
     * member between flags and the user token. The native start path copies
     * four complete 16-byte blocks and forwards offsets 0x20-0x3F as the
     * eight animation user-data values.
     */
    struct alignas(8) CryCharAnimationParams {
        explicit constexpr CryCharAnimationParams(
            std::int32_t layer = 0, std::uint32_t flags = 0)
            : m_nLayerID(layer), m_nFlags(flags) {}

        float m_fTransTime = -1.0f;          // 0x00
        float m_fKeyTime = -1.0f;            // 0x04
        float m_fPlaybackSpeed = 1.0f;       // 0x08
        float m_fAllowMultilayerAnim = 1.0f; // 0x0C
        std::int32_t m_nLayerID;              // 0x10
        float m_fPlaybackWeight = 1.0f;       // 0x14
        std::uint32_t m_nFlags;               // 0x18
        std::uint32_t m_nUserToken = 0;       // 0x1C
        float m_fUserData[8]{};               // 0x20
    };

    static_assert(sizeof(CryCharAnimationParams) == 0x40,
                  "CryCharAnimationParams size mismatch");
    static_assert(alignof(CryCharAnimationParams) == 0x8,
                  "CryCharAnimationParams alignment mismatch");
    static_assert(offsetof(CryCharAnimationParams, m_nLayerID) == 0x10,
                  "CryCharAnimationParams layer offset mismatch");
    static_assert(offsetof(CryCharAnimationParams, m_nUserToken) == 0x1C,
                  "CryCharAnimationParams user-token offset mismatch");
    static_assert(offsetof(CryCharAnimationParams, m_fUserData) == 0x20,
                  "CryCharAnimationParams user-data offset mismatch");
} // namespace SDK
