#pragma once

#include <cstddef>
#include <cstdint>

namespace SDK {
    // A four-byte RGBA vertex colour.
    struct SMeshColor {
      public:
        SMeshColor() = default;
        SMeshColor(std::uint8_t red, std::uint8_t green, std::uint8_t blue, std::uint8_t alpha): r(red), g(green), b(blue), a(alpha) {}

        std::uint8_t GetAlpha() const {
            return a;
        }

        void MaskA(std::uint8_t mask) {
            a &= mask;
        }

      private:
        std::uint8_t r;
        std::uint8_t g;
        std::uint8_t b;
        std::uint8_t a;

        static void AssertLayout() {
            static_assert(offsetof(SMeshColor, r) == 0x0, "SMeshColor::r offset mismatch");
            static_assert(offsetof(SMeshColor, g) == 0x1, "SMeshColor::g offset mismatch");
            static_assert(offsetof(SMeshColor, b) == 0x2, "SMeshColor::b offset mismatch");
            static_assert(offsetof(SMeshColor, a) == 0x3, "SMeshColor::a offset mismatch");
        }
    };

    static_assert(sizeof(SMeshColor) == 0x4, "SMeshColor size mismatch");
} // namespace SDK
