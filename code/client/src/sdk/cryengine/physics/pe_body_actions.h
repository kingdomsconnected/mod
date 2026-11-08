#pragma once

#include "../../types.h"

#include <bit>
#include <cstddef>
#include <cstdint>

namespace SDK {
    /**
     * CryPhysics' MARK_UNUSED for a float: the bit pattern a parameter
     * structure carries in a field the caller leaves to the engine. A vector
     * is unused when its x is. `CScriptBind_Entity::AwakePhysics`
     * (0x180AC2520) and `AddImpulse` (0x1837CFD7C) both write 0xFFBFFFFF.
     */
    inline constexpr float kPhysicsUnusedFloat = std::bit_cast<float>(0xFFBFFFFFU);

    /**
     * Pushes a physical entity, through IPhysicalEntity::Action. Laid out as
     * CScriptBind_Entity::AddImpulse (0x1837CFD7C) builds it on its angular
     * branch: type 2, the linear impulse, the angular one, the world point it
     * lands on (unused leaves it at the centre of mass), the part id and
     * index, and when to apply it -- 2, after the next step, as the binding
     * asks.
     */
    struct pe_action_impulse {
        std::int32_t type       = 2;
        Vec3 impulse            = {0.0F, 0.0F, 0.0F};                // 0x04
        Vec3 angImpulse         = {kPhysicsUnusedFloat, 0.0F, 0.0F}; // 0x10
        Vec3 point              = {kPhysicsUnusedFloat, 0.0F, 0.0F}; // 0x1C
        std::int32_t partid     = INT32_MIN;                         // 0x28
        std::int32_t ipart      = INT32_MIN;                         // 0x2C
        std::int32_t iApplyTime = 2;                                 // 0x30
        std::int32_t iSource    = 0;                                 // 0x34
    };

    static_assert(offsetof(pe_action_impulse, impulse) == 0x04, "pe_action_impulse impulse offset mismatch");
    static_assert(offsetof(pe_action_impulse, angImpulse) == 0x10, "pe_action_impulse angImpulse offset mismatch");
    static_assert(offsetof(pe_action_impulse, point) == 0x1C, "pe_action_impulse point offset mismatch");
    static_assert(offsetof(pe_action_impulse, partid) == 0x28, "pe_action_impulse partid offset mismatch");
    static_assert(offsetof(pe_action_impulse, iApplyTime) == 0x30, "pe_action_impulse iApplyTime offset mismatch");
    static_assert(sizeof(pe_action_impulse) == 0x38, "pe_action_impulse size mismatch");

    /**
     * Wakes a body or puts it to sleep, through IPhysicalEntity::Action. As
     * CScriptBind_Entity::AwakePhysics (0x180AC2520) builds it: type 8, the
     * wanted state, and the minimum time awake left unused.
     */
    struct pe_action_awake {
        std::int32_t type   = 8;
        std::int32_t bAwake = 1;                   // 0x04
        float minAwakeTime  = kPhysicsUnusedFloat; // 0x08
    };

    static_assert(offsetof(pe_action_awake, bAwake) == 0x04, "pe_action_awake bAwake offset mismatch");
    static_assert(sizeof(pe_action_awake) == 0x0C, "pe_action_awake size mismatch");

    /**
     * Stops a body, through IPhysicalEntity::Action: velocities and
     * accumulated forces cleared, and with `bClearContacts` its contacts
     * too. As CScriptBind_Entity::ResetPhysics (0x1837DC2D4) builds it.
     */
    struct pe_action_reset {
        std::int32_t type           = 4;
        std::int32_t bClearContacts = 1; // 0x04
    };

    static_assert(sizeof(pe_action_reset) == 0x08, "pe_action_reset size mismatch");

    /**
     * Whether a body is awake, through IPhysicalEntity::GetStatus, which
     * answers it as its return value. `CRigidEntity::GetStatus` (0x1808A6C90)
     * returns its `m_bAwake` (+0x194, low byte) for type 7 with `lag` 0.
     */
    struct pe_status_awake {
        std::int32_t type = 7;
        std::int32_t lag  = 0; // 0x04
    };

    static_assert(sizeof(pe_status_awake) == 0x08, "pe_status_awake size mismatch");

    /**
     * A body's motion and mass, through IPhysicalEntity::GetStatus. Type 8,
     * as `CRigidEntity::GetStatus` (0x1808A6C90) fills it: velocity,
     * angular velocity and the two accelerations while awake (zero while
     * asleep), then the centre of mass, the submerged fraction and the mass,
     * its body's `M` (+0x2C4). The tail is CryPhysics' own and not written by
     * that branch.
     */
    struct pe_status_dynamics {
        std::int32_t type   = 8;
        std::int32_t partid = INT32_MIN; // 0x04
        std::int32_t ipart  = INT32_MIN; // 0x08
        Vec3 v {};                       // 0x0C
        Vec3 w {};                       // 0x18
        Vec3 a {};                       // 0x24
        Vec3 wa {};                      // 0x30
        Vec3 centerOfMass {};            // 0x3C
        float submergedFraction = 0.0F;  // 0x48
        float mass              = 0.0F;  // 0x4C
        float energy            = 0.0F;  // 0x50
        std::int32_t nContacts  = 0;     // 0x54
        float timeInterval      = 0.0F;  // 0x58
    };

    static_assert(offsetof(pe_status_dynamics, v) == 0x0C, "pe_status_dynamics v offset mismatch");
    static_assert(offsetof(pe_status_dynamics, centerOfMass) == 0x3C, "pe_status_dynamics centerOfMass offset mismatch");
    static_assert(offsetof(pe_status_dynamics, mass) == 0x4C, "pe_status_dynamics mass offset mismatch");
    static_assert(sizeof(pe_status_dynamics) == 0x5C, "pe_status_dynamics size mismatch");
} // namespace SDK
