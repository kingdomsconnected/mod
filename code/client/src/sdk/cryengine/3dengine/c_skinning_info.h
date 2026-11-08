#pragma once

#include "../../types.h"
#include "cry_bone_desc_data.h"
#include "physical_proxy.h"

#include <cstddef>

namespace SDK {
    /**
     * What a `.chr`'s chunks load into,
     * embedded in its CContentCGF at +0x110 and handed to
     * `SkelLoader_Helpers::InitializeBones` (0x180B316F4). Only the bones and
     * the colliders are modeled, so the modeled extent is a lower bound.
     */
    class CSkinningInfo {
      public:
        DynArray<CryBoneDescData> &GetBones() {
            return m_arrBonesDesc;
        }

        DynArray<PhysicalProxy> &GetPhysicalProxies() {
            return m_arrPhyBoneMeshes;
        }

      private:
        std::byte m_opaque0000[0x10];               // 0x00, the reference target
        DynArray<CryBoneDescData> m_arrBonesDesc;   // 0x10
        DynArray<std::byte> m_LookIK_Rot;           // 0x18, DynArray<SJointsAimIK_Rot>
        DynArray<std::byte> m_LookIK_Pos;           // 0x20, DynArray<SJointsAimIK_Pos>
        DynArray<std::byte> m_LookDirBlends;        // 0x28, DynArray<DirectionalBlends>
        DynArray<std::byte> m_AimIK_Rot;            // 0x30, DynArray<SJointsAimIK_Rot>
        DynArray<std::byte> m_AimIK_Pos;            // 0x38, DynArray<SJointsAimIK_Pos>
        DynArray<std::byte> m_AimDirBlends;         // 0x40, DynArray<DirectionalBlends>
        DynArray<PhysicalProxy> m_arrPhyBoneMeshes; // 0x48

        static void AssertLayout() {
            static_assert(offsetof(CSkinningInfo, m_arrBonesDesc) == 0x10, "CSkinningInfo::m_arrBonesDesc offset mismatch");
            static_assert(offsetof(CSkinningInfo, m_arrPhyBoneMeshes) == 0x48, "CSkinningInfo::m_arrPhyBoneMeshes offset mismatch");
        }
    };
} // namespace SDK
