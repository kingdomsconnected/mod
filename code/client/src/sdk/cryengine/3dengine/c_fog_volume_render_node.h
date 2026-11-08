#pragma once

#include "cry_3d_engine_base.h"
#include "i_fog_volume_render_node.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    /** Exact high-value state within CFogVolumeRenderNode's 0x1F0-byte tail. */
    struct FogVolumeRuntimeStateLayout {
        Matrix34 matrix;                     // 0x000
        std::byte opaque0030[0x60];          // 0x030
        int volumeType;                      // 0x090
        std::byte opaque0094[0x30];          // 0x094
        Vec3 halfSize;                       // 0x0C4
        float globalDensity;                 // 0x0D0
        float densityOffset;                 // 0x0D4
        float nearCutoff;                    // 0x0D8
        float hdrDynamic;                    // 0x0DC
        float softEdges;                     // 0x0E0
        ColorF color;                        // 0x0E4
        bool useGlobalFogColor;              // 0x0F4
        bool ignoresVisAreas;                // 0x0F5
        std::byte opaque00F6[0x2];
        float rampStart;                     // 0x0F8
        float rampEnd;                       // 0x0FC
        float rampInfluence;                 // 0x100
        std::byte opaque0104[0x4];
        float windInfluence;                 // 0x108
        Vec3 noiseSeed;                      // 0x10C
        float state0118;
        float densityNoiseScale;             // 0x11C
        float densityNoiseOffset;            // 0x120
        float densityNoiseTimeFrequency;     // 0x124
        Vec3 densityNoiseFrequency;          // 0x128
        Vec3 emission;                       // 0x134
        Vec3 heightFallOffDirection;         // 0x140
        std::byte opaque014C[0x24];          // 0x14C
        AABB localBounds;                    // 0x170
        float densityFadeStartTime;          // 0x188
        float densityFadeEndTime;            // 0x18C
        float densityFadeStartValue;         // 0x190
        float densityFadeTargetValue;        // 0x194
        std::byte referenceState0198[0x10];  // 0x198
        void *renderResources[2];             // 0x1A8
        AABB worldBounds;                    // 0x1B8
        std::byte opaque01D0[0x20];          // 0x1D0
    };

    /** Concrete localized fog volume created by C3DEngine. */
    class CFogVolumeRenderNode : public IFogVolumeRenderNode,
                                 public Cry3DEngineBase {
      public:
        ~CFogVolumeRenderNode() override = default;

        const Matrix34 &GetFogMatrix() const { return m_runtimeState.matrix; }
        int GetVolumeType() const { return m_runtimeState.volumeType; }
        const Vec3 &GetHalfSize() const { return m_runtimeState.halfSize; }
        float GetGlobalDensity() const { return m_runtimeState.globalDensity; }
        const ColorF &GetFogColor() const { return m_runtimeState.color; }
        const AABB &GetWorldBounds() const {
            return m_runtimeState.worldBounds;
        }

      private:
        FogVolumeRuntimeStateLayout m_runtimeState; // 0x50
    };

    static_assert(sizeof(FogVolumeRuntimeStateLayout) == 0x1F0,
                  "FogVolumeRuntimeStateLayout size mismatch");
    static_assert(sizeof(CFogVolumeRenderNode) == 0x240,
                  "CFogVolumeRenderNode size mismatch");
} // namespace SDK
