#pragma once

#include "t_key_name.h"
#include <cstddef>
#include <cstdint>

namespace SDK {
    /** Native CryInput event; SendEventToListeners copies three 16-byte blocks. */
    struct SInputEvent {
        std::uint8_t deviceType = 0xFF;
        std::byte m_opaque0001[3] {};
        std::uint32_t state = 0;
        TKeyName keyName;
        std::uint32_t keyId    = 0xFFFFFFFF;
        std::int32_t modifiers = 0;
        float value            = 0.0f;
        std::byte m_opaque001C[4] {};
        void *pSymbol = nullptr; // 0x20, SInputSymbol *; left null for explicitly posted events
        std::uint8_t deviceIndex    = 0;
        std::uint8_t deviceUniqueID = 0;
        std::byte m_opaque002A[6] {};

      private:
        static void AssertLayout() {
            static_assert(offsetof(SInputEvent, deviceType) == 0x00);
            static_assert(offsetof(SInputEvent, state) == 0x04);
            static_assert(offsetof(SInputEvent, keyName) == 0x08);
            static_assert(offsetof(SInputEvent, keyId) == 0x10);
            static_assert(offsetof(SInputEvent, modifiers) == 0x14);
            static_assert(offsetof(SInputEvent, value) == 0x18);
            static_assert(offsetof(SInputEvent, pSymbol) == 0x20);
            static_assert(offsetof(SInputEvent, deviceIndex) == 0x28);
            static_assert(offsetof(SInputEvent, deviceUniqueID) == 0x29);
        }
    };
    static_assert(sizeof(SInputEvent) == 0x30, "SInputEvent size mismatch");
} // namespace SDK
