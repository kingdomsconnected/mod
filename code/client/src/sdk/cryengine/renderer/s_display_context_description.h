#pragma once

#include "../3dengine/i_fog_volume_render_node.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    /**
     * CryEngine's `IRenderer::SDisplayContextDescription`, KCD2's 0x48-byte
     * copy, as `CRenderer::CreateSwapChainBackedContext` (0x1824E8A8C) and
     * `CreateTextureBackedContext` (0x1824E8E74) read it.
     *
     * KCD2 inserts two ints after the resolution that 5.7 does not have. The
     * creators copy them into the context's stored description and nothing
     * the mod reaches reads them, so their meaning is not established: a new
     * description is best made from an existing context's, which carries
     * whatever the game itself puts there.
     */
    struct SDisplayContextDescription {
        /** `FRT_*`. */
        static constexpr std::uint16_t kClearDepth     = 1U << 0U;
        static constexpr std::uint16_t kClearStencil   = 1U << 1U;
        static constexpr std::uint16_t kClearColor     = 1U << 2U;
        static constexpr std::uint16_t kOverlayDepth   = 1U << 4U;
        static constexpr std::uint16_t kOverlayStencil = 1U << 5U;

        /** `EViewportType`: 0 is the main viewport, anything else a secondary one. */
        static constexpr std::uint8_t kViewportDefault   = 0;
        static constexpr std::uint8_t kViewportSecondary = 1;

        void *windowHandle;          // 0x00, null for a context with no window
        ColorF clearColor;           // 0x08
        ColorF clearDepthStencil;    // 0x18
        std::int32_t superSamplingX; // 0x28, clamped to at least 1
        std::int32_t superSamplingY; // 0x2C
        std::int32_t screenWidth;    // 0x30
        std::int32_t screenHeight;   // 0x34
        std::int32_t unknown0038;    // 0x38, KCD2-only
        std::int32_t unknown003C;    // 0x3C, KCD2-only
        std::uint8_t viewportType;   // 0x40
        std::byte padding0041[0x1];
        std::uint16_t renderFlags; // 0x42
        bool vsync;                // 0x44
        std::byte padding0045[0x3];
    };

    static_assert(sizeof(SDisplayContextDescription) == 0x48, "SDisplayContextDescription size mismatch");
    static_assert(offsetof(SDisplayContextDescription, clearColor) == 0x08, "SDisplayContextDescription::clearColor offset mismatch");
    static_assert(offsetof(SDisplayContextDescription, superSamplingX) == 0x28, "SDisplayContextDescription::superSamplingX offset mismatch");
    static_assert(offsetof(SDisplayContextDescription, screenWidth) == 0x30, "SDisplayContextDescription::screenWidth offset mismatch");
    static_assert(offsetof(SDisplayContextDescription, viewportType) == 0x40, "SDisplayContextDescription::viewportType offset mismatch");
    static_assert(offsetof(SDisplayContextDescription, renderFlags) == 0x42, "SDisplayContextDescription::renderFlags offset mismatch");
    static_assert(offsetof(SDisplayContextDescription, vsync) == 0x44, "SDisplayContextDescription::vsync offset mismatch");
} // namespace SDK
