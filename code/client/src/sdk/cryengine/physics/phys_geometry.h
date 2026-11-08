#pragma once

#include "../../types.h"
#include "i_geometry.h"

namespace SDK {
    /** Manager-owned record: 0x58-byte pool stride at 0x18097C52C; fields filled by RegisterGeometry (0x1808100EC) and CTriMesh::CalcPhysicalProperties (0x18080F238). */
    struct phys_geometry {
        IGeometry *pGeom;
        Vec3 Ibody;
        Quat q;
        Vec3 origin;
        float V;
        std::int32_t nRefCount;
        std::int32_t surface_idx;
        std::byte m_opaque003C[4];
        int *pMatMapping;
        std::int32_t nMats;
        std::byte m_opaque004C[12]; // padding and optional foreign data; not accessed by the mod

      private:
        static void AssertLayout() {
            static_assert(offsetof(phys_geometry, pGeom) == 0x00);
            static_assert(offsetof(phys_geometry, Ibody) == 0x08);
            static_assert(offsetof(phys_geometry, q) == 0x14);
            static_assert(offsetof(phys_geometry, origin) == 0x24);
            static_assert(offsetof(phys_geometry, V) == 0x30);
            static_assert(offsetof(phys_geometry, nRefCount) == 0x34);
            static_assert(offsetof(phys_geometry, surface_idx) == 0x38);
            static_assert(offsetof(phys_geometry, pMatMapping) == 0x40);
            static_assert(offsetof(phys_geometry, nMats) == 0x48);
        }
    };

    static_assert(sizeof(phys_geometry) == 0x58);
} // namespace SDK
