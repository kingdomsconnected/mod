#pragma once

#include "i_render_node.h"

#include <cstddef>

namespace SDK {
    struct ColorF {
        float r;
        float g;
        float b;
        float a;
    };

    /** Exact fog-volume configuration consumed by IFogVolumeRenderNode. */
    struct SFogVolumeProperties {
        int volumeType;                    // 0x00
        Vec3 size;                         // 0x04
        ColorF color;                      // 0x10
        bool useGlobalFogColor;            // 0x20
        bool ignoresVisAreas;              // 0x21
        bool affectsThisAreaOnly;          // 0x22
        std::byte padding0023;
        float globalDensity;               // 0x24
        float densityOffset;               // 0x28
        float softEdges;                   // 0x2C
        float hdrDynamic;                  // 0x30
        float nearCutoff;                  // 0x34
        float heightFallOffDirLong;        // 0x38
        float heightFallOffDirLati;        // 0x3C
        float heightFallOffShift;          // 0x40
        float heightFallOffScale;          // 0x44
        float rampStart;                   // 0x48
        float rampEnd;                     // 0x4C
        float rampInfluence;               // 0x50
        float windInfluence;               // 0x54
        float densityNoiseScale;           // 0x58
        float densityNoiseOffset;          // 0x5C
        float densityNoiseTimeFrequency;   // 0x60
        Vec3 densityNoiseFrequency;        // 0x64
        Vec3 emission;                     // 0x70
    };

    /** Localized fog-volume render-node specialization. */
    class IFogVolumeRenderNode : public IRenderNode {
      public:
        ~IFogVolumeRenderNode() override = default;

        virtual void SetFogVolumeProperties(
            const SFogVolumeProperties &properties) = 0; // +0x248
        virtual void FadeGlobalDensity(float fadeTime,
                                       float newGlobalDensity) = 0; // +0x250
    };

    static_assert(sizeof(ColorF) == 0x10, "ColorF size mismatch");
    static_assert(offsetof(SFogVolumeProperties, globalDensity) == 0x24,
                  "SFogVolumeProperties::globalDensity offset mismatch");
    static_assert(offsetof(SFogVolumeProperties, densityNoiseFrequency) == 0x64,
                  "SFogVolumeProperties::densityNoiseFrequency offset mismatch");
    static_assert(sizeof(SFogVolumeProperties) == 0x7C,
                  "SFogVolumeProperties size mismatch");
    static_assert(sizeof(IFogVolumeRenderNode) == 0x50,
                  "IFogVolumeRenderNode size mismatch");
} // namespace SDK
