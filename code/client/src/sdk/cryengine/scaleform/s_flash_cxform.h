#pragma once

#include <cstddef>
#include <cstdint>

namespace SDK {
    /** Native RGBA multiply floats and unsigned additive bytes. */
    struct SFlashCxform {
        float mul[4] {1, 1, 1, 1};
        std::uint8_t add[4] {};

      private:
        static void AssertLayout() {
            static_assert(offsetof(SFlashCxform, mul) == 0, "SFlashCxform::mul offset mismatch");
            static_assert(offsetof(SFlashCxform, add) == 0x10, "SFlashCxform::add offset mismatch");
        }
    };
    static_assert(sizeof(SFlashCxform) == 0x14, "SFlashCxform size mismatch");
} // namespace SDK
