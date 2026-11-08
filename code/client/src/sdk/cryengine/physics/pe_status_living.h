#pragma once

#include "../../types.h"
#include "i_physical_entity.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    /**
     * Living-entity status query, the physics answer for a walking body.
     *
     * Filled by `CLivingEntity::GetStatus` (0x1808A58A0), whose type-2 branch
     * writes every member below under the entity's own seqlock; the layout is
     * the classic CryPhysics one and each offset is proven by that branch's
     * stores. The engine writes up to offset 0x98, so the trailing opaque run
     * keeps the caller's storage at least that large.
     *
     * `vel` is the velocity the physics step actually produced, plus the
     * ground collider's own when standing on something that moves;
     * `velRequested` is what the last `pe_action_move` asked for. `bFlying`
     * flips on every step off a ledge, so an airborne answer worth acting on
     * is `bFlying` together with a `timeFlying` long enough to not be stair
     * noise.
     */
    struct pe_status_living {
        EPE_Status type = ePE_status_living; // 0x00
        std::int32_t bFlying = 0;        // 0x04
        float timeFlying = 0.0F;         // 0x08
        Vec3 camOffset {};               // 0x0C
        Vec3 vel {};                     // 0x18
        Vec3 velUnconstrained {};        // 0x24
        Vec3 velRequested {};            // 0x30
        Vec3 velGround {};               // 0x3C
        float groundHeight = 0.0F;       // 0x48
        Vec3 groundSlope {};             // 0x4C
        std::int32_t groundSurfaceIdx = 0; // 0x58
        std::byte opaque005C[0x44] {};     // written by the engine up to 0x98

        static void AssertLayout() {
            static_assert(offsetof(pe_status_living, bFlying) == 0x04, "pe_status_living::bFlying offset mismatch");
            static_assert(offsetof(pe_status_living, timeFlying) == 0x08, "pe_status_living::timeFlying offset mismatch");
            static_assert(offsetof(pe_status_living, vel) == 0x18, "pe_status_living::vel offset mismatch");
            static_assert(offsetof(pe_status_living, velUnconstrained) == 0x24, "pe_status_living::velUnconstrained offset mismatch");
            static_assert(offsetof(pe_status_living, velRequested) == 0x30, "pe_status_living::velRequested offset mismatch");
            static_assert(offsetof(pe_status_living, groundHeight) == 0x48, "pe_status_living::groundHeight offset mismatch");
            static_assert(offsetof(pe_status_living, groundSlope) == 0x4C, "pe_status_living::groundSlope offset mismatch");
            static_assert(offsetof(pe_status_living, groundSurfaceIdx) == 0x58, "pe_status_living::groundSurfaceIdx offset mismatch");
        }
    };

    static_assert(sizeof(pe_status_living) == 0xA0, "pe_status_living size mismatch");
} // namespace SDK
