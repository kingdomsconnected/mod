#pragma once

#include "cry_3d_engine_base.h"
#include "i_road_render_node.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    template <typename T>
    struct RoadRenderVectorLayout {
        T *begin;
        T *end;
        T *capacityEnd;
    };

    /** Concrete road/terrain-overlay node created by C3DEngine. */
    class CRoadRenderNode : public IRoadRenderNode,
                            public Cry3DEngineBase {
      public:
        ~CRoadRenderNode() override = default;

        const RoadRenderVectorLayout<Vec3> &GetVertices() const {
            return m_vertices;
        }
        const AABB &GetWorldBounds() const { return m_worldBounds; }
        std::uint8_t GetSortPriorityRaw() const { return m_sortPriority; }
        bool IgnoresTerrainHoles() const { return m_ignoreTerrainHoles; }
        IPhysicalEntity *GetPhysicalEntity() const { return m_pPhysicalEntity; }
        std::uint16_t GetLayerIdRaw() const { return m_layerId; }
        const Matrix34 &GetRoadMatrix() const { return m_matrix; }

      private:
        void *m_pRenderResource;             // 0x050
        void *m_pMaterial;                   // 0x058
        RoadRenderVectorLayout<Vec3> m_vertices; // 0x060
        float m_texCoordInfo[4];             // 0x078
        AABB m_worldBounds;                  // 0x088
        // The tail of stock SData, which the texcoords at 0x78 and the box at
        // 0x88 open; the constructor 0x181179ADC leaves it to deserialization.
        std::uint32_t m_numVertices;         // 0x0A0
        std::uint32_t m_numIndices;          // 0x0A4
        std::uint32_t m_numTangents;         // 0x0A8
        std::uint32_t m_physicsGeometryCount; // 0x0AC
        std::uint32_t m_sourceVertexCount;   // 0x0B0
        std::byte m_padding00B4[0x4];
        std::byte m_renderState00B8[0x60];   // 0x0B8
        bool m_registeredForUpdates;         // 0x118
        bool m_physicalize;                  // 0x119
        std::uint8_t m_sortPriority;         // 0x11A
        bool m_ignoreTerrainHoles;           // 0x11B
        std::byte m_padding011C[0x4];
        IPhysicalEntity *m_pPhysicalEntity; // 0x120
        std::uint16_t m_layerId;             // 0x128
        std::byte m_padding012A[0x2];
        Matrix34 m_matrix;                   // 0x12C
    };

    static_assert(sizeof(RoadRenderVectorLayout<Vec3>) == 0x18,
                  "RoadRenderVectorLayout size mismatch");
    static_assert(sizeof(CRoadRenderNode) == 0x160,
                  "CRoadRenderNode size mismatch");
} // namespace SDK
