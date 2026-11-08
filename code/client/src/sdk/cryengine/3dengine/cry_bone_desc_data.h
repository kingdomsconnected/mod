#pragma once

#include "../../types.h"
#include "cry_bone_physics.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    /**
     * One bone of a loaded `.chr`, as `CLoaderCGF::ReadCompiledBones`
     * (0x180D13B64) widens the 0x248-byte file record into it and
     * `SkelLoader_Helpers::InitializeBones` (0x180B316F4) reads it, 0x260
     * apart. `m_DefaultB2W` is the bone's frame in model space at bind pose;
     * the skeleton's default pose, its skinning bind and its ragdoll part
     * placement all start from it.
     */
    struct CryBoneDescData {
        std::uint32_t m_nControllerID; // 0x000, the plain CRC32 of the name every clip keys the bone by
        CryBonePhysics m_PhysInfo[2];  // 0x008, LOD 0 and LOD 1
        float m_fMass;                 // 0x0E8
        Matrix34 m_DefaultW2B;         // 0x0EC, the inverse of m_DefaultB2W
        Matrix34 m_DefaultB2W;         // 0x11C
        char m_arrBoneName[256];       // 0x14C
        std::int32_t m_nLimbId;        // 0x24C
        std::int32_t m_nOffsetParent;  // 0x250
        std::uint32_t m_numChildren;   // 0x254
        std::int32_t m_nOffsetChildren; // 0x258, 0 when the bone has no children

      private:
        static void AssertLayout() {
            static_assert(offsetof(CryBoneDescData, m_PhysInfo) == 0x008, "CryBoneDescData::m_PhysInfo offset mismatch");
            static_assert(offsetof(CryBoneDescData, m_fMass) == 0x0E8, "CryBoneDescData::m_fMass offset mismatch");
            static_assert(offsetof(CryBoneDescData, m_DefaultW2B) == 0x0EC, "CryBoneDescData::m_DefaultW2B offset mismatch");
            static_assert(offsetof(CryBoneDescData, m_DefaultB2W) == 0x11C, "CryBoneDescData::m_DefaultB2W offset mismatch");
            static_assert(offsetof(CryBoneDescData, m_arrBoneName) == 0x14C, "CryBoneDescData::m_arrBoneName offset mismatch");
            static_assert(offsetof(CryBoneDescData, m_nOffsetParent) == 0x250, "CryBoneDescData::m_nOffsetParent offset mismatch");
            static_assert(offsetof(CryBoneDescData, m_nOffsetChildren) == 0x258, "CryBoneDescData::m_nOffsetChildren offset mismatch");
        }
    };

    static_assert(sizeof(CryBoneDescData) == 0x260, "CryBoneDescData size mismatch");
} // namespace SDK
