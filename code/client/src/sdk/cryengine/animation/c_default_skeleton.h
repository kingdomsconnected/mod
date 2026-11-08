#pragma once

#include "../../types.h"
#include "i_default_skeleton.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    /**
     * Concrete CryAnimation default-skeleton implementation.
     *
     * RTTI proves the global hierarchy CDefaultSkeleton -> IDefaultSkeleton.
     * Its table at 0x18406B930 has exactly the 23 inherited entries and adds
     * no primary virtual operations. Independent creation paths allocate
     * exactly 0x320 bytes.
     *
     * `CDefaultSkeleton::LoadNewSKEL` (0x18051BFE8) builds one from a `.chr`:
     * the bones through `SkelLoader_Helpers::InitializeBones` (0x180B316F4),
     * then the colliders, then the `.chrparams`, whose aim and look IK
     * definitions fill the two pose-blender descriptions.
     */
    class CDefaultSkeleton : public IDefaultSkeleton {
      public:
        /** One joint, 0x30 apart; `GetJointCRC32ByID` reads +0x0C, `GetJointParentIDByID` +0x16. */
        struct SJoint {
            CryStringT<char> m_strJointName; // 0x00
            std::uint32_t m_nJointCRC32;     // 0x08, the controller id clips key the joint by
            std::uint32_t m_nJointCRC32Lower; // 0x0C
            std::int32_t m_nOffsetChildren;  // 0x10, 0 when the joint has no children
            std::uint16_t m_numChildren;     // 0x14
            std::int16_t m_idxParent;        // 0x16
            std::byte m_physics0018[0x18];   // 0x18, the joint's physics reference and mass

          private:
            static void AssertLayout() {
                static_assert(offsetof(SJoint, m_nJointCRC32) == 0x08, "CDefaultSkeleton::SJoint::m_nJointCRC32 offset mismatch");
                static_assert(offsetof(SJoint, m_idxParent) == 0x16, "CDefaultSkeleton::SJoint::m_idxParent offset mismatch");
            }
        };

        static_assert(sizeof(SJoint) == 0x30, "CDefaultSkeleton::SJoint size mismatch");

        /**
         * One `<DirectionalBlends><Joint AnimToken=...>` of a `.chrparams` IK
         * definition, 0x18 apart. `GlobalAnimationHeaderAIM::ProcessPoses`
         * (0x183740940) gives a pose clip to the first definition whose token
         * occurs in its path, ignoring case.
         */
        struct DirectionalBlends {
            CryStringT<char> m_AnimToken; // 0x00
            // The older CryAnimation layout, which keeps only the joint
            // indices the names resolve to.
            std::uint32_t m_AnimTokenCRC32;        // 0x08
            std::int16_t m_nParaJointIdx;          // 0x0C
            std::int16_t m_nRotParaJointIdx;       // 0x0E
            std::int16_t m_nStartJointIdx;         // 0x10
            std::int16_t m_nRotStartJointIdx;      // 0x12
            std::int16_t m_nReferenceJointIdx;     // 0x14
            std::int16_t m_nRotReferenceJointIdx;  // 0x16
        };

        static_assert(sizeof(DirectionalBlends) == 0x18, "CDefaultSkeleton::DirectionalBlends size mismatch");

        /**
         * One `<RotationList><Rotation>` entry, 8 apart: the joint whose local
         * rotation a baked pose carries at the same index
         * (`VExampleInit::CopyPoses2`, 0x18373A63C).
         */
        struct SJointsAimIK_Rot {
            std::int16_t m_nJointIdx;    // 0x00
            std::int16_t m_nPosIndex;          // 0x02, -1 when the joint has no position channel
            std::uint8_t m_nPreEvaluate;       // 0x04
            std::uint8_t m_nAdditive;          // 0x05
            std::int16_t m_nRotJointParentIdx; // 0x06
        };

        static_assert(sizeof(SJointsAimIK_Rot) == 0x8, "CDefaultSkeleton::SJointsAimIK_Rot size mismatch");

        ~CDefaultSkeleton() override = default;

        const DynArray<SJoint> &GetJoints() const {
            return m_arrModelJoints;
        }

        const char *GetModelFilePath() const {
            return m_strFilePath.m_pData;
        }

        const DynArray<DirectionalBlends> &GetLookBlends() const {
            return m_lookBlends;
        }

        const DynArray<SJointsAimIK_Rot> &GetLookRotations() const {
            return m_lookRotations;
        }

        const DynArray<DirectionalBlends> &GetAimBlends() const {
            return m_aimBlends;
        }

        const DynArray<SJointsAimIK_Rot> &GetAimRotations() const {
            return m_aimRotations;
        }

      private:
        DynArray<SJoint> m_arrModelJoints;           // 0x008
        // Constructor 0x180A36384 sets the look descriptor's three eye
        // limits to 45 degrees, zeroes both AABBs and assigns the GUID counter.
        void *m_pJointsCRCToIDMap;                   // 0x010, CInt32HashMap<JointIdType, 512>
        std::byte m_poseDefaultData[0x68];           // 0x018, Skeleton::CPoseData, built by 0x180ABBDA0
        std::byte m_state0080[0x10];                 // 0x080
        DynArray<std::byte> m_IKLimbTypes;           // 0x090
        DynArray<std::byte> m_ADIKTargets;           // 0x098
        DynArray<std::byte> m_recoilImpactJoints;    // 0x0A0, the recoil description's joint array
        std::uint64_t m_state00A8;                   // 0x0A8
        std::int32_t m_recoilWeaponRightJointIdx;    // 0x0B0, -1 by default
        std::int32_t m_recoilWeaponLeftJointIdx;     // 0x0B4, -1 by default
        std::uint32_t m_lookError;                   // 0x0B8, m_poseBlenderLookDesc.m_error
        float m_eyeLimitHalfYawRadians;              // 0x0BC, 45 degrees
        float m_eyeLimitPitchRadiansUp;              // 0x0C0, 45 degrees
        float m_eyeLimitPitchRadiansDown;            // 0x0C4, 45 degrees
        CryStringT<char> m_eyeAttachmentLeftName;    // 0x0C8
        CryStringT<char> m_eyeAttachmentRightName;   // 0x0D0
        DynArray<DirectionalBlends> m_lookBlends;    // 0x0D8, m_poseBlenderLookDesc.m_blends
        DynArray<SJointsAimIK_Rot> m_lookRotations;  // 0x0E0, m_poseBlenderLookDesc.m_rotations
        DynArray<std::byte> m_lookPositions;         // 0x0E8, m_poseBlenderLookDesc.m_positions
        std::uint32_t m_aimError;                    // 0x0F0, m_poseBlenderAimDesc.m_error
        std::uint32_t m_padding00F4;
        DynArray<DirectionalBlends> m_aimBlends;     // 0x0F8, m_poseBlenderAimDesc.m_blends
        DynArray<SJointsAimIK_Rot> m_aimRotations;   // 0x100, m_poseBlenderAimDesc.m_rotations
        DynArray<std::byte> m_aimPositions;          // 0x108, m_poseBlenderAimDesc.m_positions
        DynArray<std::byte> m_aimProcAdjustments;    // 0x110, m_poseBlenderAimDesc.m_procAdjustments
        std::uint32_t m_usePhysProxyBBox;            // 0x118
        std::uint32_t m_padding011C;
        DynArray<std::int32_t> m_BBoxIncludeList;    // 0x120
        Vec3 m_AABBExtensionMin;                     // 0x128
        Vec3 m_AABBExtensionMax;                     // 0x134
        Vec3 m_ModelAABBMin;                         // 0x140, the model's bounds in the default pose
        Vec3 m_ModelAABBMax;                         // 0x14C
        bool m_bHasPhysics[2];                       // 0x158
        std::byte m_padding015A[0x6];
        DynArray<std::byte> m_arrBackupPhyBoneMeshes; // 0x160, PhysicalProxy
        DynArray<std::byte> m_arrBackupBoneEntities;  // 0x168, BONE_ENTITY
        DynArray<std::byte> m_arrBackupPhysInfo;      // 0x170, CryBonePhysics
        std::uint32_t m_guid;                        // 0x178, from a global counter
        std::int16_t m_jointLookup017C[61];          // 0x17C, every entry -1 by default
        std::byte m_padding01F6[0x2];
        void *m_animListIDs[3];                      // 0x1F8, zeroed
        DynArray<std::byte> m_state0210;             // 0x210
        void *m_pAnimationSet;                       // 0x218, _smart_ptr<CAnimationSet>
        DynArray<std::byte> m_arrAnimationLOD;       // 0x220, DynArray<DynArray<uint32>>
        CryStringT<char> m_strFilePath;              // 0x228, set by the constructor (0x180A36384) before loading
        std::uint64_t m_nFilePathCRC64;              // 0x230, the constructor's third argument
        std::uint32_t m_state0238;                   // 0x238, zero
        std::int32_t m_nRefCounter;                  // 0x23C, interlocked zero in the constructor
        std::uint64_t m_state0240;                   // 0x240, not written by the constructor
        void *m_pFacialModel;                        // 0x248, CFacialModel, null
        CryStringT<char> m_strAnimEventFilePath;     // 0x250
        CryStringT<char> m_string0258;               // 0x258
        std::byte m_ModelMesh[0xB0];                 // 0x260, CModelMesh, built by 0x180A367FC
        std::uint32_t m_nType;                       // 0x310, the constructor's second argument (0x11223344 for .chr)
        std::byte m_pad0314[0x4];                    // 0x314
        void *m_pCGA_Object;                         // 0x318, _smart_ptr<IStatObj>, null

        static void AssertLayout() {
            static_assert(offsetof(CDefaultSkeleton, m_arrModelJoints) == 0x008, "CDefaultSkeleton::m_arrModelJoints offset mismatch");
            static_assert(offsetof(CDefaultSkeleton, m_lookBlends) == 0x0D8, "CDefaultSkeleton::m_lookBlends offset mismatch");
            static_assert(offsetof(CDefaultSkeleton, m_lookRotations) == 0x0E0, "CDefaultSkeleton::m_lookRotations offset mismatch");
            static_assert(offsetof(CDefaultSkeleton, m_aimBlends) == 0x0F8, "CDefaultSkeleton::m_aimBlends offset mismatch");
            static_assert(offsetof(CDefaultSkeleton, m_aimRotations) == 0x100, "CDefaultSkeleton::m_aimRotations offset mismatch");
            static_assert(offsetof(CDefaultSkeleton, m_strFilePath) == 0x228, "CDefaultSkeleton::m_strFilePath offset mismatch");
            static_assert(offsetof(CDefaultSkeleton, m_eyeLimitHalfYawRadians) == 0xBC, "CDefaultSkeleton::m_eyeLimitHalfYawRadians offset mismatch");
            static_assert(offsetof(CDefaultSkeleton, m_ModelAABBMin) == 0x140, "CDefaultSkeleton::m_ModelAABBMin offset mismatch");
            static_assert(offsetof(CDefaultSkeleton, m_bHasPhysics) == 0x158, "CDefaultSkeleton::m_bHasPhysics offset mismatch");
            static_assert(offsetof(CDefaultSkeleton, m_guid) == 0x178, "CDefaultSkeleton::m_guid offset mismatch");
            static_assert(offsetof(CDefaultSkeleton, m_pAnimationSet) == 0x218, "CDefaultSkeleton::m_pAnimationSet offset mismatch");
            static_assert(offsetof(CDefaultSkeleton, m_pFacialModel) == 0x248, "CDefaultSkeleton::m_pFacialModel offset mismatch");
            static_assert(offsetof(CDefaultSkeleton, m_nFilePathCRC64) == 0x230, "CDefaultSkeleton::m_nFilePathCRC64 offset mismatch");
            static_assert(offsetof(CDefaultSkeleton, m_nRefCounter) == 0x23C, "CDefaultSkeleton::m_nRefCounter offset mismatch");
            static_assert(offsetof(CDefaultSkeleton, m_nType) == 0x310, "CDefaultSkeleton::m_nType offset mismatch");
        }
    };

    static_assert(sizeof(CDefaultSkeleton) == 0x320, "CDefaultSkeleton size mismatch");
} // namespace SDK
