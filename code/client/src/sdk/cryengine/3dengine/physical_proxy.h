#pragma once

#include "../../types.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    /**
     * One collider mesh of a loaded `.chr`, 0x20 apart in
     * `CSkinningInfo::m_arrPhyBoneMeshes`. Its points are in the bone's own
     * frame: `CDefaultSkeleton::SetupPhysicalProxies` (0x180991A4C) hands
     * them to the geometry manager untransformed, and the ragdoll places the
     * part at the bone's absolute pose.
     */
    struct PhysicalProxy {
        std::uint32_t ChunkID;                 // 0x00, matched against CryBonePhysics::pPhysGeom
        DynArray<Vec3> m_arrPoints;            // 0x08
        DynArray<std::uint16_t> m_arrIndices;  // 0x10
        DynArray<std::uint8_t> m_arrMaterials; // 0x18

      private:
        static void AssertLayout() {
            static_assert(offsetof(PhysicalProxy, m_arrPoints) == 0x08, "PhysicalProxy::m_arrPoints offset mismatch");
            static_assert(offsetof(PhysicalProxy, m_arrMaterials) == 0x18, "PhysicalProxy::m_arrMaterials offset mismatch");
        }
    };

    static_assert(sizeof(PhysicalProxy) == 0x20, "PhysicalProxy size mismatch");
} // namespace SDK
