#pragma once

#include "../../types.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    /**
     * Sets a physical entity's velocity, through IPhysicalEntity::Action.
     * Laid out as CScriptBind_Entity::SetVelocityEx (0x1837DF5D8) builds it:
     * type 10, both part selectors left unused (INT32_MIN), then the linear
     * and angular velocities.
     */
    struct pe_action_set_velocity {
        std::int32_t type                 = 10;
        std::int32_t ipart                = INT32_MIN;
        std::int32_t partid               = INT32_MIN;
        Vec3 v                            = {0.0F, 0.0F, 0.0F}; // 0x0C
        Vec3 w                            = {0.0F, 0.0F, 0.0F}; // 0x18
        std::int32_t bRotationAroundPivot = 0;                  // 0x24
    };

    static_assert(offsetof(pe_action_set_velocity, v) == 0x0C, "pe_action_set_velocity v offset mismatch");
    static_assert(offsetof(pe_action_set_velocity, w) == 0x18, "pe_action_set_velocity w offset mismatch");
    static_assert(sizeof(pe_action_set_velocity) == 0x28, "pe_action_set_velocity size mismatch");
} // namespace SDK
