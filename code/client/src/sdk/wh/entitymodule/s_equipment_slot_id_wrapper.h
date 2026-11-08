#pragma once

#include <cstdint>

namespace SDK::wh::entitymodule {
    /**
     * Reflected value type used by native equipment-slot vectors.
     *
     * Native slot consumers advance these values in four-byte increments and
     * pass the contained value to the equipment-slot registry. The stripped
     * field identifier is not independently recoverable, so its semantic name
     * remains raw.
     */
    struct S_EquipmentSlotIdWrapper {
        std::uint32_t GetValueRaw() const { return m_value; }

      private:
        std::uint32_t m_value; // 0x00
    };

    static_assert(sizeof(S_EquipmentSlotIdWrapper) == 0x4,
                  "S_EquipmentSlotIdWrapper size mismatch");
} // namespace SDK::wh::entitymodule
