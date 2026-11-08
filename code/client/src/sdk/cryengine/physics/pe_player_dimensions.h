#pragma once

#include "sdk/cryengine/physics/pe_params_particle.h"
#include "sdk/cryengine/physics/pe_player_dynamics.h"
#include "sdk/types.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    /**
     * A living entity's collision cylinder, `ePE_player_dimensions`.
     *
     * Stock CryPhysics layout. The validator `CLivingEntity::SetParams`
     * runs first (0x180AB36DC) reads `heightPivot` at +0x04, `heightEye` at
     * +0x08 and `sizeCollider` at +0x0C, and rejects a `sizeCollider` whose
     * squared length passes 10000; the setter itself (0x18084CDAC, type 1)
     * reads `heightCollider` at +0x18 and `dirUnproj` at +0x24. It also
     * unprojects: a larger cylinder that would overlap the world is pushed out
     * along `dirUnproj` or refused, so a caller reads the result back rather
     * than assuming it.
     *
     * `sizeCollider` is the cylinder's half extents -- x and y the radius, z
     * the half height -- and `heightCollider` how far its centre sits above
     * the entity's origin.
     */
    struct pe_player_dimensions: pe_params {
        float heightPivot;                          // +0x04
        float heightEye;                            // +0x08
        Vec3 sizeCollider;                          // +0x0C
        float heightCollider;                       // +0x18
        float headRadius;                           // +0x1C
        float heightHead;                           // +0x20
        Vec3 dirUnproj;                             // +0x24
        float maxUnproj;                            // +0x30
        std::int32_t bUseCapsule;                   // +0x34
        float groundContactEps;                     // +0x38

        pe_player_dimensions(): pe_params {ePE_player_dimensions} {
            const float unused = UnusedFloat();
            heightPivot = heightEye = heightCollider = headRadius = heightHead = groundContactEps = unused;
            sizeCollider = Vec3 {unused, unused, unused};
            // The stock constructor's own defaults: unproject straight up, and
            // "auto" for how far.
            dirUnproj   = Vec3 {0.0F, 0.0F, 1.0F};
            maxUnproj   = 0.0F;
            bUseCapsule = kUnusedInt;
        }
    };

    static_assert(offsetof(pe_player_dimensions, sizeCollider) == 0x0C, "pe_player_dimensions::sizeCollider offset mismatch");
    static_assert(offsetof(pe_player_dimensions, heightCollider) == 0x18, "pe_player_dimensions::heightCollider offset mismatch");
    static_assert(offsetof(pe_player_dimensions, dirUnproj) == 0x24, "pe_player_dimensions::dirUnproj offset mismatch");
    static_assert(sizeof(pe_player_dimensions) == 0x3C, "pe_player_dimensions size mismatch");
} // namespace SDK
