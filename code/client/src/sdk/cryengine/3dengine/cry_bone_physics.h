#pragma once

#include <cstddef>
#include <cstdint>

namespace SDK {
    /**
     * One bone's ragdoll description,
     * 0x70 bytes as `CSkeletonPhysics::BuildPhysicalEntity` (0x180B339B0)
     * and `CDefaultSkeleton::SetupPhysicalProxies` (0x180991A4C) read it.
     * While the skeleton loads, `pPhysGeom` still holds the id of the
     * `PhysicalProxy` chunk that becomes its collider.
     */
    struct CryBonePhysics {
        std::intptr_t pPhysGeom;     // 0x00, a PhysicalProxy::ChunkID until SetupPhysicalProxies
        std::int32_t flags;          // 0x08, -1 when the bone has no physics
        float min[3];                // 0x0C
        float max[3];                // 0x18
        float spring_angle[3];       // 0x24
        float spring_tension[3];     // 0x30
        float damping[3];            // 0x3C
        float framemtx[3][3];        // 0x48, the joint frame in the physical parent's frame

      private:
        static void AssertLayout() {
            static_assert(offsetof(CryBonePhysics, flags) == 0x08, "CryBonePhysics::flags offset mismatch");
            static_assert(offsetof(CryBonePhysics, framemtx) == 0x48, "CryBonePhysics::framemtx offset mismatch");
        }
    };

    static_assert(sizeof(CryBonePhysics) == 0x70, "CryBonePhysics size mismatch");
} // namespace SDK
