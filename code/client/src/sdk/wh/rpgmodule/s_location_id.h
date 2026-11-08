#pragma once

#include <cstddef>
#include <cstdint>

namespace SDK::wh::rpgmodule {
    // Windows GUID layout verified through the native RTTR converters.
    // Field names are descriptive; original member names are unknown.
    struct S_LocationId {
        std::uint32_t data1;
        std::uint16_t data2;
        std::uint16_t data3;
        std::uint8_t data4[8];

      private:
        static void AssertLayout() {
            static_assert(offsetof(S_LocationId, data1) == 0x0, "S_LocationId::data1 offset mismatch");
            static_assert(offsetof(S_LocationId, data2) == 0x4, "S_LocationId::data2 offset mismatch");
            static_assert(offsetof(S_LocationId, data3) == 0x6, "S_LocationId::data3 offset mismatch");
            static_assert(offsetof(S_LocationId, data4) == 0x8, "S_LocationId::data4 offset mismatch");
        }
    };

    static_assert(sizeof(S_LocationId) == 0x10, "S_LocationId size mismatch");
} // namespace SDK::wh::rpgmodule
