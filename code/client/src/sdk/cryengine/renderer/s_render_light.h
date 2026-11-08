#pragma once

#include "../../types.h"
#include "../3dengine/i_fog_volume_render_node.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    /**
     * A light handed to the renderer for one pass: KCD2's 0x320-byte
     * `SRenderLight`.
     *
     * Built in place by its native constructor (0x1805A337C, what
     * `CScriptBind_Entity::LoadLight` builds its local with), then shaped
     * with the two native setters, which keep the derived copies of colour
     * and radius in step. Its native destructor releases its native-owned
     * strings and reference-counted members after the renderer copies it.
     */
    struct alignas(16) SRenderLight {
        /** `DLF_*`. */
        static constexpr std::uint32_t kDirectional     = 0x2;
        static constexpr std::uint32_t kPoint           = 0x40;
        static constexpr std::uint32_t kDeferredCubemap = 0x800;
        static constexpr std::uint32_t kSun             = 0x40000;
        static constexpr std::uint32_t kAmbient         = 0x200000;
        // Full mask applied by KCD2's native light parser for cast_shadow.
        static constexpr std::uint32_t kCastShadows     = 0x30000020;

        SRenderLight();
        ~SRenderLight();

        SRenderLight(const SRenderLight &) = delete;
        SRenderLight &operator=(const SRenderLight &) = delete;

        /** Writes the colour at +0x20 and its base copy at +0x1F0. */
        void SetLightColor(const ColorF &color);

        /** Writes the radius and bulb radius, then the values derived from them. */
        void SetRadius(float radius, float bulbRadius);

        /** Where the light is, written to both the origin the renderer reads and its base copy. */
        void SetPosition(const Vec3 &position) {
            m_origin     = position;
            m_baseOrigin = position;
        }

        std::int32_t m_id; // 0x00, written by EF_ADDDlight
        std::uint32_t m_n3DEngineUpdateFrameID; // 0x04
        std::uint32_t m_nEntityId;              // 0x08, -1 by default
        std::uint32_t m_flags; // 0x0C
        Vec3 m_origin;         // 0x10
        // Named in CryEngine's SRenderLight order, anchored on the defaults
        // the constructor writes: radius 100, an opaque black colour, the three box
        // extents of 10, the shadow biases of 1, the 4 m shadow update radius
        // beside an update ratio of 1 << 8, the two matrices and the 45
        // degree projector angle.
        float m_fRadius;                 // 0x1C, 100 by default
        float m_Color[4];                // 0x20, ColorF, (0, 0, 0, 1) by default
        float m_SpecMult;                // 0x30
        float m_state0034;               // 0x34, 1 by default
        float m_state0038;               // 0x38, 1 by default
        float m_fHDRDynamic;             // 0x3C
        std::int16_t m_scissor[4];       // 0x40, m_sX, m_sY, m_sWidth, m_sHeight
        void *m_pDiffuseCubemap;         // 0x48, ITexture
        void *m_pSpecularCubemap;        // 0x50, ITexture
        float m_fBoxWidth;               // 0x58
        float m_fBoxHeight;              // 0x5C
        float m_fBoxLength;              // 0x60
        float m_state0064;               // 0x64
        float m_state0068[3];            // 0x68, 1 by default
        std::uint32_t m_state0074;       // 0x74, 0xFFFF00FF: the attenuation falloff and sort bytes
        float m_state0078[2];            // 0x78, 0.5 and 0
        void *m_pointers0080[4];         // 0x80, four reference-counted pointers
        void *m_pOwner;                  // 0xA0, ILightSource
        void *m_pShadowMapFrustums;      // 0xA8
        void *m_state00B0;               // 0xB0
        float m_fShadowBias;             // 0xB8
        float m_fShadowSlopeBias;        // 0xBC
        float m_fShadowAutoBiasScale;    // 0xC0
        float m_state00C4;               // 0xC4, 1 by default
        float m_fShadowResolutionScale;  // 0xC8
        float m_fShadowUpdateMinRadius;  // 0xCC, 4 by default
        std::uint16_t m_nShadowMinResolution; // 0xD0
        std::uint16_t m_nShadowUpdateRatio;   // 0xD2, 1 << 8
        std::uint16_t m_state00D4;       // 0xD4
        std::byte m_padding00D6[0x2];
        void *m_pLightDynTexSource;      // 0xD8, IDynTextureSource
        void *m_pLightImage;             // 0xE0, ITexture
        float m_ProjMatrix[16];          // 0xE8
        float m_ObjMatrix[12];           // 0x128
        float m_fLightFrustumAngle;      // 0x158, 45 by default
        float m_fProjectorNearPlane;     // 0x15C
        const char *m_sName;             // 0x160
        std::byte m_Shader[0x10];        // 0x168, SShaderItem
        std::byte m_opaque0178[0x6C];    // 0x178
        Vec3 m_baseOrigin; // 0x1E4
        std::byte m_opaque01F0[0x130];

      private:
        static void AssertLayout() {
            static_assert(offsetof(SRenderLight, m_flags) == 0x0C, "SRenderLight::m_flags offset mismatch");
            static_assert(offsetof(SRenderLight, m_origin) == 0x10, "SRenderLight::m_origin offset mismatch");
            static_assert(offsetof(SRenderLight, m_baseOrigin) == 0x1E4, "SRenderLight::m_baseOrigin offset mismatch");
            static_assert(offsetof(SRenderLight, m_Color) == 0x20, "SRenderLight::m_Color offset mismatch");
            static_assert(offsetof(SRenderLight, m_fShadowAutoBiasScale) == 0xC0, "SRenderLight::m_fShadowAutoBiasScale offset mismatch");
            static_assert(offsetof(SRenderLight, m_fShadowResolutionScale) == 0xC8, "SRenderLight::m_fShadowResolutionScale offset mismatch");
            static_assert(offsetof(SRenderLight, m_fShadowUpdateMinRadius) == 0xCC, "SRenderLight::m_fShadowUpdateMinRadius offset mismatch");
            static_assert(offsetof(SRenderLight, m_fLightFrustumAngle) == 0x158, "SRenderLight::m_fLightFrustumAngle offset mismatch");
            static_assert(offsetof(SRenderLight, m_sName) == 0x160, "SRenderLight::m_sName offset mismatch");
        }
    };

    static_assert(sizeof(SRenderLight) == 0x320, "SRenderLight size mismatch");
} // namespace SDK
