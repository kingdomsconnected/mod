#pragma once

#include "i_render_node.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    /**
     * Concrete ocean environment renderer.
     *
     * COcean's table at 0x184777000 has 73 entries, exactly IRenderNode's
     * eight IShadowCaster slots and 65 render-node slots, and its
     * constructor 0x183537E78 writes the full IRenderNode prefix (-1 at
     * 0x38, "dd" at 0x4D) before its own members. Those follow the stock
     * order; the anchors are the material paths it loads and its vector
     * constructor iterators (2 x 0x80 at 0x440, 2 x 0x1C at 0x540, two pairs
     * of 0x18-byte vectors at 0x5A8 and 0x5D8).
     */
    class COcean : public IRenderNode {
      public:
        ~COcean() override = default;

      private:
        void *m_pMaterial;                        // 0x050, IMaterial *, the constructor's argument
        std::int32_t m_state0058;                 // 0x058, untouched by the constructor
        std::uint32_t m_nVertsCount;              // 0x05C
        std::uint32_t m_nIndicesCount;            // 0x060
        std::int32_t m_state0064;                 // 0x064, untouched by the constructor
        void *m_pBottomCapMaterial;               // 0x068, _smart_ptr<IMaterial>, "WaterOceanBottom"
        void *m_pBottomCapRenderMesh;             // 0x070, _smart_ptr<IRenderMesh>
        std::byte m_bottomCap0078[0x30];          // 0x078, zeroed; the stock bottom-cap vertex and index PodArrays and two more words
        std::byte m_Camera[0x310];                // 0x0A8, CCamera named "COcean"
        void *m_pREOcclusionQueries[4];           // 0x3B8, CREOcclusionQuery *[CYCLE_BUFFERS_NUM]
        void *m_pShaderOcclusionQuery;            // 0x3D8, IShader *, "OcclusionTest"
        float m_fLastFov;                         // 0x3E0
        float m_fLastVisibleFrameTime;            // 0x3E4
        std::int32_t m_nLastVisibleFrameId;       // 0x3E8
        float m_fRECustomData[12];                // 0x3EC
        float m_fREOceanBottomCustomData[8];      // 0x41C
        bool m_bOceanFFT;                         // 0x43C
        std::byte m_padding043D[0x3];
        std::byte m_wvParams[2][0x80];            // 0x440, CREWaterVolume::SParams[RT_COMMAND_BUF_COUNT]
        std::byte m_wvoParams[2][0x1C];           // 0x540, CREWaterVolume::SOceanParams[RT_COMMAND_BUF_COUNT]
        void *m_pFogIntoMat;                      // 0x578, _smart_ptr<IMaterial>, "Fog/OceanInto"
        void *m_pFogOutofMat;                     // 0x580, "Fog/OceanOutof"
        void *m_pFogIntoMatLowSpec;               // 0x588, "Fog/OceanIntoLowSpec"
        void *m_pFogOutofMatLowSpec;              // 0x590, "Fog/OceanOutofLowSpec"
        void *m_pWVRE[2];                         // 0x598, CREWaterVolume *[RT_COMMAND_BUF_COUNT]
        std::byte m_wvVertices[2][0x18];          // 0x5A8, std::vector<SVF_P3F_C4B_T2F>, game-allocated
        std::byte m_wvIndices[2][0x18];           // 0x5D8, std::vector<uint16>, game-allocated

        static void AssertLayout() {
            static_assert(offsetof(COcean, m_pMaterial) == 0x50, "COcean::m_pMaterial offset mismatch");
            static_assert(offsetof(COcean, m_Camera) == 0xA8, "COcean::m_Camera offset mismatch");
            static_assert(offsetof(COcean, m_pShaderOcclusionQuery) == 0x3D8, "COcean::m_pShaderOcclusionQuery offset mismatch");
            static_assert(offsetof(COcean, m_bOceanFFT) == 0x43C, "COcean::m_bOceanFFT offset mismatch");
            static_assert(offsetof(COcean, m_wvParams) == 0x440, "COcean::m_wvParams offset mismatch");
            static_assert(offsetof(COcean, m_pFogIntoMat) == 0x578, "COcean::m_pFogIntoMat offset mismatch");
            static_assert(offsetof(COcean, m_pWVRE) == 0x598, "COcean::m_pWVRE offset mismatch");
            static_assert(offsetof(COcean, m_wvIndices) == 0x5D8, "COcean::m_wvIndices offset mismatch");
        }
    };

    static_assert(sizeof(COcean) == 0x608, "COcean size mismatch");
} // namespace SDK
