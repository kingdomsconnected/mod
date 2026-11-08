#pragma once

#include "c_attachment_manager.h"
#include "c_skeleton_anim.h"
#include "c_skeleton_pose.h"
#include "i_default_skeleton.h"
#include "i_character_instance.h"
#include "i_skeleton_pose.h"

#include <cstddef>

namespace SDK {
    /**
     * Concrete CryAnimation character instance.
     *
     * RTTI proves the global hierarchy CCharInstance -> ICharacterInstance.
     * Its table at 0x1840602F8 has exactly the 73 inherited entries and adds
     * no primary virtual operations. Two allocation paths and sized deletion
     * independently prove the complete 0xBB0-byte size. The constructor at
     * 0x180ABB0FC builds CAttachmentManager at +0x08, an 8-byte subobject
     * at +0x108, CSkeletonAnim at +0x110 and the skeleton pose at +0x698,
     * then binds the animation to this instance and that pose (0x180ABAB28)
     * -- CryEngine's m_AttachmentManager / m_SkeletonAnim / m_SkeletonPose.
     */
    class CCharInstance : public ICharacterInstance {
      public:
        ~CCharInstance() override = default;

        // Both subobjects declare pure virtuals in the SDK, so they cannot be
        // members by value; their storage is named and sized from the types.
        CAttachmentManager *GetAttachmentManager() { return reinterpret_cast<CAttachmentManager *>(m_AttachmentManager); }
        const CAttachmentManager *GetAttachmentManager() const { return reinterpret_cast<const CAttachmentManager *>(m_AttachmentManager); }

        CSkeletonAnim *GetSkeletonAnim() { return reinterpret_cast<CSkeletonAnim *>(m_SkeletonAnim); }
        const CSkeletonAnim *GetSkeletonAnim() const { return reinterpret_cast<const CSkeletonAnim *>(m_SkeletonAnim); }

      private:
        alignas(8) std::byte m_AttachmentManager[sizeof(CAttachmentManager)]; // 0x008, CAttachmentManager
        void *m_pointer0108;                    // 0x108, 8-byte subobject; its constructor 0x180ABB34C stores 0x185631BE0
        alignas(8) std::byte m_SkeletonAnim[sizeof(CSkeletonAnim)];           // 0x110, CSkeletonAnim
        // CryAnimation's CCharInstance order from here, anchored by the
        // constructor 0x180ABB0FC: identity location, a (0, 1, 0) view
        // direction, the file path, playback scale 1 and the start-animation
        // flag set.
        CSkeletonPose m_SkeletonPose;           // 0x698
        SDK::QuatTS m_location;                 // 0xA08
        SDK::Vec3 m_Viewdir;                    // 0xA28
        std::uint32_t m_paddingA34;
        SDK::CryStringT<char> m_strFilePath;    // 0xA38
        std::byte m_stateA40[0x30];             // 0xA40, zeroed
        void *m_pDefaultSkeleton;               // 0xA70, _smart_ptr<CDefaultSkeleton>
        void *m_pFacialInstance;                // 0xA78, _smart_ptr<CFacialInstance>
        std::uint32_t m_CharEditMode;           // 0xA80
        volatile std::uint32_t m_rpFlags;       // 0xA84, 1 by default
        std::uint32_t m_LastUpdateFrameID_Pre;  // 0xA88
        std::uint32_t m_LastUpdateFrameID_Post; // 0xA8C
        float m_fPostProcessZoomAdjustedDistanceFromCamera; // 0xA90
        float m_fDeltaTime;                     // 0xA94
        float m_fOriginalDeltaTime;             // 0xA98
        float m_fPlaybackScale;                 // 0xA9C, 1 by default
        bool m_bEnableStartAnimation;           // 0xAA0, true by default
        std::byte m_paddingAA1[0x3];
        std::uint32_t m_stateAA4;               // 0xAA4
        void *m_pParentRenderNode;              // 0xAA8, ICharacterRenderNode
        void *m_pInstanceMaterial;              // 0xAB0, _smart_ptr<IMaterial>
        std::byte m_Extents[0x10];              // 0xAB8, CGeomExtents
        std::int32_t m_nRefCounter;             // 0xAC8
        std::uint32_t m_LastRenderedFrameID;    // 0xACC
        std::uint8_t m_stateAD0;                // 0xAD0
        std::uint8_t m_stateAD1;                // 0xAD1
        std::uint16_t m_stateAD2;               // 0xAD2
        std::uint32_t m_stateAD4;               // 0xAD4
        std::int32_t m_stateAD8;                // 0xAD8, -1 by default
        std::uint32_t m_paddingADC;
        std::byte m_stateAE0[0x50];             // 0xAE0, built by 0x180ABB430: a 2, a -1 and zeroed vectors
        const void *m_pStaticB30;               // 0xB30
        std::uint64_t m_stateB38;               // 0xB38
        std::uint32_t m_stateB40;               // 0xB40
        bool m_stateB44;                        // 0xB44
        std::byte m_paddingB45[0x3];
        std::uint64_t m_stateB48[2];            // 0xB48
        std::byte m_stateB58[0x40];             // 0xB58, built by 0x1805618EC
        std::uint32_t m_stateB98;               // 0xB98
        std::uint32_t m_paddingB9C;
        std::byte m_conditionVariable[0x10];    // 0xBA0, Concurrency::details::_Condition_variable

        static void AssertLayout() {
            static_assert(offsetof(CCharInstance, m_AttachmentManager) == 0x08, "CCharInstance::m_AttachmentManager offset mismatch");
            static_assert(offsetof(CCharInstance, m_pointer0108) == 0x108, "CCharInstance::m_pointer0108 offset mismatch");
            static_assert(offsetof(CCharInstance, m_SkeletonAnim) == 0x110, "CCharInstance::m_SkeletonAnim offset mismatch");
            static_assert(offsetof(CCharInstance, m_SkeletonPose) == 0x698, "CCharInstance::m_SkeletonPose offset mismatch");
            static_assert(offsetof(CCharInstance, m_location) == 0xA08, "CCharInstance::m_location offset mismatch");
            static_assert(offsetof(CCharInstance, m_strFilePath) == 0xA38, "CCharInstance::m_strFilePath offset mismatch");
            static_assert(offsetof(CCharInstance, m_pDefaultSkeleton) == 0xA70, "CCharInstance::m_pDefaultSkeleton offset mismatch");
            static_assert(offsetof(CCharInstance, m_fPlaybackScale) == 0xA9C, "CCharInstance::m_fPlaybackScale offset mismatch");
            static_assert(offsetof(CCharInstance, m_nRefCounter) == 0xAC8, "CCharInstance::m_nRefCounter offset mismatch");
            static_assert(offsetof(CCharInstance, m_conditionVariable) == 0xBA0, "CCharInstance::m_conditionVariable offset mismatch");
        }
    };

    static_assert(sizeof(CCharInstance) == 0xBB0,
                  "CCharInstance size mismatch");
} // namespace SDK
