#pragma once

#include "c_rigid_entity.h"

#include <cstddef>

namespace SDK {
    /**
     * Concrete CryPhysics wheeled-vehicle entity.
     *
     * RTTI proves the global hierarchy CWheeledVehicleEntity -> CRigidEntity.
     * Its table at 0x18407F588 has exactly the 81 inherited entries and adds
     * no primary virtual operations. Factory allocation and vector deletion
     * independently prove the complete 0x3D38-byte size.
     */
    class CWheeledVehicleEntity : public CRigidEntity {
      public:
        ~CWheeledVehicleEntity() override = default;

      private:
        std::byte m_opaque0438[0x3900];
    };

    static_assert(sizeof(CWheeledVehicleEntity) == 0x3D38,
                  "CWheeledVehicleEntity size mismatch");
} // namespace SDK
