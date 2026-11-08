#pragma once

#include <cstddef>
#include <cstdint>

namespace SDK {
    /**
     * Names one of the renderer's display contexts: CryEngine's
     * `SDisplayContextKey`, a `CryVariant<uint32 id, HWND>` in 16 bytes --
     * the value in the first qword, the alternative index in the byte at +8.
     * Index 0 is the empty variant, which the renderer reads as the main
     * context (`CRenderer::FindDisplayContext` 0x1805719C4 returns the base
     * context for it); 1 is an id, which every context made without a
     * window gets; 2 is a window handle.
     *
     * 16-byte aligned, as the engine's is: the pass builder
     * (`CreateGeneralPassRenderingInfo` 0x18043E940) copies the key it is
     * handed with `movaps`, which faults on anything less.
     */
    struct alignas(16) SDisplayContextKey {
        static constexpr std::uint8_t kEmpty = 0;
        static constexpr std::uint8_t kId    = 1;
        static constexpr std::uint8_t kHwnd  = 2;

        std::uint64_t value = 0;
        std::uint8_t index  = kEmpty;
        std::byte padding0009[0x7] {};

        bool IsEmpty() const {
            return index == kEmpty;
        }
    };

    static_assert(sizeof(SDisplayContextKey) == 0x10, "SDisplayContextKey size mismatch");
    static_assert(alignof(SDisplayContextKey) == 0x10, "SDisplayContextKey alignment mismatch");
    static_assert(offsetof(SDisplayContextKey, index) == 0x08, "SDisplayContextKey::index offset mismatch");
} // namespace SDK
