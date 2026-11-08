#pragma once

#include "i_skeleton_anim.h"
#include "s_parametric_sampler.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    /** Exact 0x78-byte animation instance stored in a skeleton FIFO. */
    class CAnimation {
      public:
        virtual ~CAnimation() = default;
        virtual void unk_fn0001() = 0;

        SParametricSampler *m_pParametricSampler; // 0x08
        std::int32_t m_animationId; // 0x10
        std::uint8_t m_currentSegmentIndexPrev0; // 0x14
        std::uint8_t m_currentSegmentIndexPrev1; // 0x15
        std::uint8_t m_currentSegmentIndex0;     // 0x16
        std::uint8_t m_currentSegmentIndex1;     // 0x17
        float m_segmentNormalizedTimeDelta;      // 0x18
        float m_startTime;                       // 0x1C
        float m_transitionTime;                  // 0x20
        float m_transitionPriority;              // 0x24
        float m_transitionWeight;                // 0x28
        float m_playbackSpeed;                   // 0x2C
        float m_playbackWeight;                  // 0x30
        float m_currentSegmentExpectedDurationSeconds; // 0x34
        float m_expectedTotalDurationSeconds;          // 0x38
        float m_segmentNormalizedTimePrev0; // 0x3C
        float m_segmentNormalizedTimePrev1; // 0x40
        float m_segmentNormalizedTime0;     // 0x44
        float m_segmentNormalizedTime1;     // 0x48
        std::uint32_t m_staticFlags;         // 0x4C
        std::uint16_t m_dynamicFlags0;       // 0x50
        std::uint16_t m_dynamicFlags1;       // 0x52
        std::uint32_t m_userToken;           // 0x54
        float m_userData[8];                 // 0x58

      private:
        static void AssertLayout() {
            static_assert(offsetof(CAnimation, m_animationId) == 0x10, "CAnimation::m_animationId offset mismatch");
        }
    };

    static_assert(sizeof(CAnimation) == 0x78, "CAnimation size mismatch");

    /**
     * One of CSkeletonAnim's sixteen FIFO layers. CSkeletonAnim's constructor
     * runs the element constructor 0x180ABB5A4 sixteen times from +0x40 with
     * stride 0x40; the element opens with a null qword, so the animation
     * array sits at element +0x08.
     */
    struct SkeletonAnimationLayerLayout {
        void *m_pointer0000;                          // 0x00, null in the constructor
        CAnimation *m_pAnimations;                    // 0x08, DynArray data (empty sentinel 0x185675F70), count at pointer - 4
        float m_playbackScale;                        // 0x10, 1.0
        float m_transitionTime;                       // 0x14
        float m_transitionWeight;                     // 0x18
        float m_blendWeight;                          // 0x1C, 1.0
        std::uint8_t m_active;                        // 0x20
        std::byte m_pad0021[0x3];                     // 0x21
        std::uint32_t m_state0024;                    // 0x24, zero
        void *m_pointer0028[2];                       // 0x28, built by 0x180ABB5E4: two pointers and a flag byte
        std::uint8_t m_state0038;                     // 0x38, zero
        std::byte m_pad0039[0x7];                     // 0x39

        std::uint32_t GetAnimationCount() const {
            if (m_pAnimations == nullptr) {
                return 0;
            }
            return *(reinterpret_cast<const std::uint32_t *>(m_pAnimations) - 1);
        }

        CAnimation *GetAnimation(std::uint32_t index) {
            return index < GetAnimationCount() ? &m_pAnimations[index] : nullptr;
        }

        const CAnimation *GetAnimation(std::uint32_t index) const {
            return index < GetAnimationCount() ? &m_pAnimations[index] : nullptr;
        }
    };

    static_assert(sizeof(SkeletonAnimationLayerLayout) == 0x40,
                  "SkeletonAnimationLayerLayout size mismatch");
    static_assert(offsetof(SkeletonAnimationLayerLayout, m_pAnimations) == 0x08, "SkeletonAnimationLayerLayout::m_pAnimations offset mismatch");
    static_assert(offsetof(SkeletonAnimationLayerLayout, m_playbackScale) == 0x10, "SkeletonAnimationLayerLayout::m_playbackScale offset mismatch");
    static_assert(offsetof(SkeletonAnimationLayerLayout, m_active) == 0x20, "SkeletonAnimationLayerLayout::m_active offset mismatch");

    /**
     * Concrete CryAnimation skeleton-animation implementation.
     *
     * RTTI proves the global hierarchy CSkeletonAnim -> ISkeletonAnim. Its
     * table at 0x183A711E0 implements all 52 interface entries and appends
     * one concrete operation. Default construction, scalar deletion, and its
     * embedding inside CCharInstance all prove the 0x588-byte layout. Binary
     * serialization and the constructor at 0x180ABB460 establish sixteen
     * 0x40-byte FIFO layers at +0x40 (animation arrays at +0x48 + 0x40 * n)
     * and exact 0x78-byte CAnimation entries.
     */
    class CSkeletonAnim : public ISkeletonAnim {
      public:
        static constexpr std::uint32_t AnimationLayerCount = 16;

        ~CSkeletonAnim() override = default;

        virtual void unk_fn0052() = 0; // +0x1A0

        SkeletonAnimationLayerLayout *GetLayer(std::uint32_t index) {
            return index < AnimationLayerCount ? &m_layers[index] : nullptr;
        }

        const SkeletonAnimationLayerLayout *GetLayer(std::uint32_t index) const {
            return index < AnimationLayerCount ? &m_layers[index] : nullptr;
        }

      private:
        // Built by 0x180ABB640: three null pointers, a std::vector<uint32> and a flag.
        void *m_pointer0008;                       // 0x008
        void *m_pointer0010;                       // 0x010
        void *m_pointer0018;                       // 0x018
        std::byte m_vector0020[0x18];              // 0x020, MSVC std::vector<uint32>
        std::uint8_t m_state0038;                  // 0x038
        std::byte m_pad0039[0x7];                  // 0x039
        SkeletonAnimationLayerLayout m_layers[16]; // 0x040
        void *m_pointer0440[2];                    // 0x440, 0x180ABB5E4 run: two pointers, flag byte
        std::uint8_t m_state0450;                  // 0x450
        std::byte m_pad0451[0x7];                  // 0x451
        void *m_pointer0458[2];                    // 0x458, 0x180ABB5E4 run
        std::uint8_t m_state0468;                  // 0x468
        std::byte m_pad0469[0x7];                  // 0x469
        void *m_pointer0470;                       // 0x470, null
        void *m_pointer0478;                       // 0x478, null
        void *m_pointer0480[2];                    // 0x480, 0x180ABB5E4 run
        std::uint8_t m_state0490;                  // 0x490
        std::byte m_pad0491[0x7];                  // 0x491
        void *m_pointer0498[2];                    // 0x498, 0x180ABB5E4 run
        std::uint8_t m_state04A8;                  // 0x4A8
        std::byte m_pad04A9[0x7];                  // 0x4A9
        // Written by the instance's init 0x180ABAB28 rather than the constructor.
        std::uint32_t m_state04B0;                 // 0x4B0
        std::uint8_t m_state04B4;                  // 0x4B4
        std::byte m_state04B5[0x3];                // 0x4B5, a byte pair the init clears
        std::uint16_t m_state04B8;                 // 0x4B8
        std::uint8_t m_flags04BA;                  // 0x4BA, the init clears bit 0
        std::byte m_pad04BB[0x1];
        std::byte m_state04BC[0x20];               // 0x4BC, zeroed by the init
        std::uint32_t m_pad04DC;
        void *m_pSkeletonPose;                     // 0x4E0, CSkeletonPose, the init's third argument
        void *m_pInstance;                         // 0x4E8, CCharInstance, the init's second argument
        std::uint8_t m_state04F0;                  // 0x4F0
        std::uint8_t m_state04F1;                  // 0x4F1
        std::byte m_pad04F2[0x2];
        SDK::QuatT m_relativeMovement;             // 0x4F4, identity after the init
        void *m_pointer0510;                       // 0x510, 0x185631B80 in the constructor
        void *m_pointer0518[2];                    // 0x518, cleared by the init
        std::uint32_t m_state0528;                 // 0x528, zero
        std::byte m_pad052C[0x4];                  // 0x52C
        void *m_pointer0530[3];                    // 0x530, null
        std::int32_t m_state0548;                  // 0x548, -1
        std::byte m_pad054C[0x4];                  // 0x54C
        void *m_pointer0550[6];                    // 0x550, null
        std::uint8_t m_state0580;                  // 0x580, 1
        std::byte m_pad0581[0x3];                  // 0x581
        std::uint32_t m_state0584;                 // 0x584, zero

        static void AssertLayout() {
            static_assert(offsetof(CSkeletonAnim, m_layers) == 0x40, "CSkeletonAnim::m_layers offset mismatch");
            static_assert(offsetof(CSkeletonAnim, m_pointer0440) == 0x440, "CSkeletonAnim::m_pointer0440 offset mismatch");
            static_assert(offsetof(CSkeletonAnim, m_pointer0510) == 0x510, "CSkeletonAnim::m_pointer0510 offset mismatch");
            static_assert(offsetof(CSkeletonAnim, m_state0548) == 0x548, "CSkeletonAnim::m_state0548 offset mismatch");
            static_assert(offsetof(CSkeletonAnim, m_state0584) == 0x584, "CSkeletonAnim::m_state0584 offset mismatch");
        }
    };

    static_assert(sizeof(CSkeletonAnim) == 0x588,
                  "CSkeletonAnim size mismatch");
} // namespace SDK
