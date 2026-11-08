#pragma once

#include "../../types.h"

#include <cstdint>

namespace SDK {
    struct ColorB {
        std::uint8_t r, g, b, a;

        static ColorB FromARGB(std::uint32_t color) {
            return {static_cast<std::uint8_t>(color >> 16), static_cast<std::uint8_t>(color >> 8), static_cast<std::uint8_t>(color), static_cast<std::uint8_t>(color >> 24)};
        }
    };
    static_assert(sizeof(ColorB) == 4);

    struct SAuxGeomRenderFlags {
        // Verified in native PSO construction at 0x1807C7C60. Zero selects
        // 3D, solid, no culling, depth test/write on and no blending.
        static constexpr std::uint32_t DepthTestOff  = 0x00200000;
        static constexpr std::uint32_t DepthWriteOff = 0x00400000;
        static constexpr std::uint32_t AlphaBlended  = 0x20000000;
        std::uint32_t value                          = 0;
    };
    static_assert(sizeof(SAuxGeomRenderFlags) == 4);

    /** Native command collector; the game consumes it at the end of the frame. */
    struct IRenderAuxGeom {
        virtual ~IRenderAuxGeom() = default;
        // Explicit hidden return storage: these native flags are non-trivial.
        // SetRenderFlags returns the previous flags (0x1803D4320).
        virtual SAuxGeomRenderFlags *SetRenderFlags(SAuxGeomRenderFlags *previous, const SAuxGeomRenderFlags &flags) = 0;
        virtual SAuxGeomRenderFlags *GetRenderFlags(SAuxGeomRenderFlags *result) const                               = 0;
        virtual void unk_fn0003()                                                                                    = 0;
        virtual void unk_fn0004()                                                                                    = 0;
        virtual void unk_fn0005()                                                                                    = 0;
        virtual void unk_fn0006()                                                                                    = 0;
        virtual void unk_fn0007()                                                                                    = 0;
        // +0x40, 0x1824ED604; ScriptBind_System::DrawLine proves the ABI.
        virtual void DrawLine(const Vec3 &from, const ColorB &fromColor, const Vec3 &to, const ColorB &toColor, float width) = 0;
        virtual void unk_fn0009()                                                                                            = 0;
        virtual void unk_fn0010()                                                                                            = 0;
        virtual void unk_fn0011()                                                                                            = 0;
        virtual void unk_fn0012()                                                                                            = 0;
        virtual void unk_fn0013()                                                                                            = 0;
        virtual void unk_fn0014()                                                                                            = 0;
        virtual void unk_fn0015()                                                                                            = 0;
        virtual void unk_fn0016()                                                                                            = 0;
        // +0x88, 0x1824F14E8: three position/color pairs.
        virtual void DrawTriangle(const Vec3 &a, const ColorB &aColor, const Vec3 &b, const ColorB &bColor, const Vec3 &c, const ColorB &cColor) = 0;
    };
} // namespace SDK
