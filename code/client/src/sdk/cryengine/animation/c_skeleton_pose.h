#pragma once

#include "../../types.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    /**
     * CryAnimation's CSkeletonPose, embedded at
     * `CCharInstance+0x698`.
     *
     * Laid out by its constructor 0x180ABBC24. The pose data is built at
     * +0x30, the embedded CSkeletonPhysics (its own vtable) sits at +0x100,
     * two further pose-data blocks at +0x248 and +0x2B0, three pose-modifier
     * pointers at +0x320, and CryEngine's default m_AABB of (-2, -2, -2) to
     * (2, 2, 2) pins the bounds at +0x348 with the flag byte after them.
     */
    class CSkeletonPose {
      public:
        const void *m_pVTable;                    // 0x000
        std::int32_t m_nForceSkeletonUpdate;      // 0x008
        bool m_state000C;                         // 0x00C
        std::byte m_padding000D[0x3];
        void *m_state0010[4];                     // 0x010, zeroed
        std::byte m_poseData[0xA0];               // 0x030, Skeleton::CPoseData, built by 0x1805AAD5C
        void *m_state00D0[4];                     // 0x0D0, zeroed
        void *m_pInstance;                        // 0x0F0, CCharInstance
        void *m_pSkeletonAnim;                    // 0x0F8, CSkeletonAnim
        std::byte m_physics[0x148];               // 0x100, CSkeletonPhysics with its vtable
        std::byte m_poseBlock0248[0x68];          // 0x248, built by 0x180ABBDA0
        std::byte m_poseBlock02B0[0x68];          // 0x2B0, built by 0x180ABBDA0
        std::uint64_t m_state0318;                // 0x318
        void *m_poseModifiers[3];                 // 0x320, IAnimationPoseModifier smart pointers
        std::uint64_t m_state0338;                // 0x338
        std::uint64_t m_state0340;                // 0x340
        SDK::Vec3 m_AABBMin;                      // 0x348, (-2, -2, -2)
        SDK::Vec3 m_AABBMax;                      // 0x354, (2, 2, 2)
        std::uint8_t m_flags;                     // 0x360, m_bInstanceVisible, m_bFullSkeletonUpdate and the rest
        std::byte m_padding0361[0xF];

      private:
        static void AssertLayout() {
            static_assert(offsetof(CSkeletonPose, m_poseData) == 0x30, "CSkeletonPose::m_poseData offset mismatch");
            static_assert(offsetof(CSkeletonPose, m_physics) == 0x100, "CSkeletonPose::m_physics offset mismatch");
            static_assert(offsetof(CSkeletonPose, m_poseBlock02B0) == 0x2B0, "CSkeletonPose::m_poseBlock02B0 offset mismatch");
            static_assert(offsetof(CSkeletonPose, m_poseModifiers) == 0x320, "CSkeletonPose::m_poseModifiers offset mismatch");
            static_assert(offsetof(CSkeletonPose, m_AABBMin) == 0x348, "CSkeletonPose::m_AABBMin offset mismatch");
            static_assert(offsetof(CSkeletonPose, m_flags) == 0x360, "CSkeletonPose::m_flags offset mismatch");
        }
    };

    static_assert(sizeof(CSkeletonPose) == 0x370, "CSkeletonPose size mismatch");
} // namespace SDK
