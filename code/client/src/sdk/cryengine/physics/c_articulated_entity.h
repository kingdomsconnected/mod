#pragma once

#include "c_rigid_entity.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    /**
     * Concrete CryPhysics articulated-body entity, vtable 0x183A48B30.
     *
     * RTTI proves direct CRigidEntity inheritance at +0x00. Its 81-entry
     * table has the same boundary as CRigidEntity and therefore adds no
     * primary virtual operations. Matching allocation and sized-deletion
     * paths prove the 0x5E0-byte object size.
     */
    class CArticulatedEntity : public CRigidEntity {
      public:
        ~CArticulatedEntity() override = default;

      protected:
        // CryPhysics' CArticulatedEntity, anchored on its constructor
        // 0x1806EC13C: the joint arrays first, the lying-mode block that
        // copies the rigid gravity, takes 5 colliders and 0.2 damping. The
        // constructor also confirms the CRigidEntity layout it overrides:
        // m_Emin at +0x220, m_damping at +0x240, m_bAwake at +0x194 and
        // m_iVarPart0 at +0x18C.
        void *m_infos;                   // 0x438, ae_part_info array
        void *m_joints;                  // 0x440, ae_joint array
        std::int32_t m_nJoints;          // 0x448
        std::int32_t m_nJointsAlloc;     // 0x44C
        SDK::Vec3 m_posPivot;            // 0x450
        SDK::Vec3 m_offsPivot;           // 0x45C
        SDK::Vec3 m_acc;                 // 0x468
        SDK::Vec3 m_wacc;                // 0x474
        std::byte m_state0480[0xF0];     // 0x480
        std::int32_t m_nCollLyingMode;   // 0x570, 5 by default
        SDK::Vec3 m_gravityLyingMode;    // 0x574, the rigid gravity
        float m_dampingLyingMode;        // 0x580, 0.2 by default
        float m_EminLyingMode;           // 0x584
        std::byte m_state0588[0x58];     // 0x588
    };

    static_assert(sizeof(CArticulatedEntity) == 0x5E0,
                  "CArticulatedEntity size mismatch");
} // namespace SDK
