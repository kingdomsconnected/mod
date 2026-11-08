#pragma once

#include "../../types.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    /**
     * One baked pose of a directional (aim or look) blend: a local rotation
     * per entry of the skeleton's IK rotation list, and a position per entry
     * of its position list, in list order (`VExampleInit::CopyPoses2`,
     * 0x18373A63C).
     */
    struct AimIKPose {
        DynArray<Quat> m_arrRotation; // 0x00
        DynArray<Vec3> m_arrPosition; // 0x08
    };

    static_assert(sizeof(AimIKPose) == 0x10, "AimIKPose size mismatch");

    /**
     * The header of one aim or look pose clip, 0x7C0 apart at
     * `CharacterManager::m_arrGlobalAIM` (appended by
     * `CAnimationManager::CreateGAH_AIM`, 0x180B14848; table 0x183BC9198).
     *
     * Retail never builds its controllers: `CharacterManager::LoadAnimationImageFileAIM`
     * (0x1809916C8) fills the baked poses from `animations/DirectionalBlends.img`
     * and sets `kFlagCreated`, so `CAnimationSet::LoadFileAIM` skips loading
     * and processing. The aim blender (`SDirectionalBlender::AccumulateAimPoses`,
     * 0x180961B64) reads nothing but `m_arrAimIKPosesAIM`, the middle pose
     * and the polar grid; the last two come only from the primary joints.
     */
    class GlobalAnimationHeaderAIM {
      public:
        /** `CA_ASSET_CREATED`: the poses are in memory. */
        static constexpr std::uint32_t kFlagCreated = 0x800;

        virtual void unk_fn0000() = 0; // +0x00

        /** Slot 1 (0x18113CE50) answers `m_FilePath`. */
        virtual const char *GetFilePath() const = 0; // +0x08

        bool IsCreated() const {
            return (m_nFlags & kFlagCreated) != 0;
        }

        DynArray<AimIKPose> &GetPoses() {
            return m_arrAimIKPosesAIM;
        }

        /** CHUNK_GAHAIM_INFO::VirtualExample: one polar-grid cell, 12 bytes. */
        struct VirtualExample {
            std::uint8_t i0, i1, i2, i3;
            std::int16_t v0, v1, v2, v3;
        };

      private:
        std::uint64_t m_state0008;               // 0x08, copied by the copy constructor at 0x183743EC8, not set by the default one
        std::uint64_t m_state0010;               // 0x10, zero in the constructor at 0x1815D0BAC
        std::uint32_t m_nFlags;                  // 0x18
        std::uint32_t m_state001C;               // 0x1C, copied with +0x20 as one qword
        std::uint32_t m_state0020;               // 0x20
        std::byte m_pad0024[0x4];                // 0x24
        CryStringT<char> m_FilePath;             // 0x28
        std::uint16_t m_nRef_by_Model;           // 0x30, CryEngine's three counters plus one more, each copied as uint16
        std::uint16_t m_nTouchedCounter;         // 0x32
        std::uint16_t m_nControllers;            // 0x34
        std::uint16_t m_state0036;               // 0x36, zeroed by the destructor 0x18374440C
        float m_fStartSec;                       // 0x38, -1.0
        float m_fEndSec;                         // 0x3C, -1.0
        float m_fTotalDuration;                  // 0x40, -1.0
        std::byte m_pad0044[0x4];                // 0x44
        void *m_arrController;                   // 0x48, DynArray<IController_AutoPtr> data
        std::uint32_t m_AnimTokenCRC32;          // 0x50
        std::byte m_pad0054[0x4];                // 0x54
        DynArray<AimIKPose> m_arrAimIKPosesAIM;  // 0x58
        std::uint64_t m_nExist;                  // 0x60
        Quat m_MiddleAimPoseRot;                 // 0x68, identity
        Quat m_MiddleAimPose;                    // 0x78, identity
        VirtualExample m_PolarGrid[17 * 9];      // 0x88, XGRID * YGRID, copied as one 0x72C block
        std::byte m_pad07B4[0x4];                // 0x7B4
        void *m_VE2;                             // 0x7B8, DynArray<VirtualExampleInit2> data

        static void AssertLayout() {
            static_assert(offsetof(GlobalAnimationHeaderAIM, m_nFlags) == 0x18, "GlobalAnimationHeaderAIM::m_nFlags offset mismatch");
            static_assert(offsetof(GlobalAnimationHeaderAIM, m_FilePath) == 0x28, "GlobalAnimationHeaderAIM::m_FilePath offset mismatch");
            static_assert(offsetof(GlobalAnimationHeaderAIM, m_fStartSec) == 0x38, "GlobalAnimationHeaderAIM::m_fStartSec offset mismatch");
            static_assert(offsetof(GlobalAnimationHeaderAIM, m_arrController) == 0x48, "GlobalAnimationHeaderAIM::m_arrController offset mismatch");
            static_assert(offsetof(GlobalAnimationHeaderAIM, m_MiddleAimPoseRot) == 0x68, "GlobalAnimationHeaderAIM::m_MiddleAimPoseRot offset mismatch");
            static_assert(offsetof(GlobalAnimationHeaderAIM, m_PolarGrid) == 0x88, "GlobalAnimationHeaderAIM::m_PolarGrid offset mismatch");
            static_assert(offsetof(GlobalAnimationHeaderAIM, m_VE2) == 0x7B8, "GlobalAnimationHeaderAIM::m_VE2 offset mismatch");
            static_assert(offsetof(GlobalAnimationHeaderAIM, m_arrAimIKPosesAIM) == 0x58, "GlobalAnimationHeaderAIM::m_arrAimIKPosesAIM offset mismatch");
        }
    };

    static_assert(sizeof(GlobalAnimationHeaderAIM::VirtualExample) == 0xC, "GlobalAnimationHeaderAIM::VirtualExample size mismatch");
    static_assert(sizeof(GlobalAnimationHeaderAIM) == 0x7C0, "GlobalAnimationHeaderAIM size mismatch");
} // namespace SDK
