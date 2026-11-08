#pragma once

#include "c_signal.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::shared {
    /**
     * A model value without a vtable: the value in its own eight-byte slot and
     * the signal that announces its changes.
     *
     * The compact form a model builds inline next to its full
     * `C_ModelProperty` members -- `HorseRuntimeData::CtorProperties`
     * (0x180BE0F00) writes eight of them in a row, the value zeroed
     * and the signal set to its kind's dispatch with no subscriber.
     */
    template <typename T>
    struct S_ModelValue {
        static_assert(sizeof(T) <= 0x8, "S_ModelValue value does not fit its slot");

        union {
            T m_value;                 // 0x00
            std::uint64_t m_valueBits; // 0x00
        };
        C_Signal m_signal; // 0x08
    };

    static_assert(sizeof(S_ModelValue<bool>) == 0x18, "S_ModelValue<bool> size mismatch");
    static_assert(sizeof(S_ModelValue<std::uint32_t>) == 0x18, "S_ModelValue<uint32> size mismatch");
} // namespace SDK::wh::shared
