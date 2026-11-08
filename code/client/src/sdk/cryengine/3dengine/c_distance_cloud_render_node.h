#pragma once

#include "cry_3d_engine_base.h"
#include "i_distance_cloud_render_node.h"

#include <cstddef>

namespace SDK {
    /** Concrete localized cloud node created by C3DEngine. */
    class CDistanceCloudRenderNode : public IDistanceCloudRenderNode,
                                     public Cry3DEngineBase {
      public:
        ~CDistanceCloudRenderNode() override = default;

        const Vec3 &GetPosition() const { return m_position; }
        float GetSizeX() const { return m_sizeX; }
        float GetSizeY() const { return m_sizeY; }
        float GetRotationZ() const { return m_rotationZ; }
        void *GetMaterialRaw() const { return m_pMaterial; }

      private:
        Vec3 m_position;          // 0x50
        float m_sizeX;            // 0x5C
        float m_sizeY;            // 0x60
        float m_rotationZ;        // 0x64
        void *m_pMaterial;        // 0x68, retained material reference
        AABB m_WSBBox;            // 0x70, returned by GetBBox (0x1834AF214) and FillBBox (0x1834AB12C)
        std::byte m_opaque0088[0x8]; // 0x88, not written by the constructor 0x1834A978C

        static void AssertLayout() {
            static_assert(offsetof(CDistanceCloudRenderNode, m_pMaterial) == 0x68, "CDistanceCloudRenderNode::m_pMaterial offset mismatch");
            static_assert(offsetof(CDistanceCloudRenderNode, m_WSBBox) == 0x70, "CDistanceCloudRenderNode::m_WSBBox offset mismatch");
        }
    };

    static_assert(sizeof(CDistanceCloudRenderNode) == 0x90,
                  "CDistanceCloudRenderNode size mismatch");
} // namespace SDK
