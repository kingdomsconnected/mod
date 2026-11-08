#pragma once

#include "cry_3d_engine_base.h"
#include "i_water_wave_render_node.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    /** Concrete localized water-wave node created by C3DEngine. */
    class CWaterWaveRenderNode : public IWaterWaveRenderNode,
                                 public Cry3DEngineBase {
      public:
        ~CWaterWaveRenderNode() override = default;

        std::uint64_t GetWaveId() const { return m_waveId; }
        const SWaterWaveParams &GetWaveParamsRaw() const { return m_params; }
        const Vec3 &GetWorldPosition() const { return m_worldPosition; }
        const Matrix34 &GetWorldTransform() const { return m_worldTransform; }
        void *GetMaterialRaw() const { return m_pMaterial; }

      private:
        std::uint64_t m_waveId;            // 0x050
        std::uint32_t m_state0058;         // 0x058
        std::byte m_padding005C[0x4];
        void *m_pRenderResource;            // 0x060
        void *m_pMaterial;                  // 0x068, retained reference
        void *m_pState0070;                 // 0x070
        std::byte m_geometryState0078[0x40];// 0x078
        SWaterWaveParams m_params;          // 0x0B8
        std::uint32_t m_state00F0;          // 0x0F0
        Vec3 m_worldPosition;               // 0x0F4
        Matrix34 m_worldTransform;          // 0x100
        Vec3 m_state0130;                   // 0x130
        Vec3 m_state013C;                   // 0x13C
        AABB m_WSBBox;                      // 0x148, returned by GetBBox (0x18353ECC0) and FillBBox (0x18353E268)

        static void AssertLayout() {
            static_assert(offsetof(CWaterWaveRenderNode, m_params) == 0xB8, "CWaterWaveRenderNode::m_params offset mismatch");
            static_assert(offsetof(CWaterWaveRenderNode, m_WSBBox) == 0x148, "CWaterWaveRenderNode::m_WSBBox offset mismatch");
        }
    };

    static_assert(sizeof(CWaterWaveRenderNode) == 0x160,
                  "CWaterWaveRenderNode size mismatch");
} // namespace SDK
