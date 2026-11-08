#pragma once

#include "../../cryengine/action/i_mannequin_action.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    /** Native constructor 0x180F45D20 extends TAction with the owner identity. */
    class C_BaseAction: public SDK::TAction<SDK::SAnimationContext> {
      public:
        SDK::EntityId GetOwnerEntityId() const {
            return m_ownerEntityId;
        }

      private:
        SDK::EntityId m_ownerEntityId; // 0x90
        std::uint8_t m_value0094; // 0x94, ctor 0x180F45D20 stores its third argument (a char)
        std::byte m_padding0095[0x3];

        static void AssertLayout() {
            static_assert(offsetof(C_BaseAction, m_ownerEntityId) == 0x90, "C_BaseAction owner offset mismatch");
        }
    };

    static_assert(sizeof(C_BaseAction) == 0x98, "C_BaseAction size mismatch");
} // namespace SDK::wh::entitymodule
