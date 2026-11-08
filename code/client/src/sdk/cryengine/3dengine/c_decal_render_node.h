#pragma once

#include "cry_3d_engine_base.h"
#include "i_decal_render_node.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    /** Exact constructor/destructor-proven state following IDecalRenderNode. */
    struct DecalRenderNodeRuntimeStateLayout {
        Matrix34 matrix;                    // 0x00
        std::byte opaque0030[0x28];         // 0x30
        AABB worldBounds;                   // 0x58
        std::uint16_t layerId;              // 0x70
        std::byte padding0072[0x2];
        std::uint32_t lastRenderedFrameId;  // 0x74
        void *state0078;                    // 0x78, released by destruction
        void *state0080;                    // 0x80
    };

    /**
     * Concrete dynamic decal render node created by C3DEngine.
     *
     * Factory case eERType_Decal allocates exactly 0xD8 bytes. RTTI proves
     * IDecalRenderNode at +0x00 and the empty Cry3DEngineBase tag at +0x50;
     * the 74-slot table adds only IDecalRenderNode::CleanUpOldDecals.
     */
    class CDecalRenderNode : public IDecalRenderNode,
                             public Cry3DEngineBase {
      public:
        ~CDecalRenderNode() override = default;

        const Matrix34 &GetDecalMatrix() const { return m_runtimeState.matrix; }
        const AABB &GetWorldBounds() const {
            return m_runtimeState.worldBounds;
        }
        std::uint16_t GetLayerIdRaw() const { return m_runtimeState.layerId; }
        std::uint32_t GetLastRenderedFrameId() const {
            return m_runtimeState.lastRenderedFrameId;
        }

      private:
        DecalRenderNodeRuntimeStateLayout m_runtimeState; // 0x50
    };

    static_assert(sizeof(DecalRenderNodeRuntimeStateLayout) == 0x88,
                  "DecalRenderNodeRuntimeStateLayout size mismatch");
    static_assert(sizeof(CDecalRenderNode) == 0xD8,
                  "CDecalRenderNode size mismatch");
} // namespace SDK
