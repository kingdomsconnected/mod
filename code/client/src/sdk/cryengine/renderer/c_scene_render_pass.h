#pragma once

#include "c_device_render_pass.h"
#include "c_device_render_pass_desc.h"
#include "c_device_resource_layout.h"
#include "c_device_resource_set.h"
#include "c_render_pass_base.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    class CGraphicsPipeline;

    /** ABI copy of the native six-float renderer viewport. */
    struct SceneRenderViewportLayout {
        float topLeftX;
        float topLeftY;
        float width;
        float height;
        float minimumDepth;
        float maximumDepth;
    };

    /** ABI copy of the native integer scissor rectangle. */
    struct SceneRenderRectangleLayout {
        std::int32_t left;
        std::int32_t top;
        std::int32_t right;
        std::int32_t bottom;
    };

    /**
     * Front-end scene render pass.
     *
     * The scalar deleting destructor proves the exact 0x210-byte extent. The
     * constructor, copy constructor, teardown, and public pass setup paths
     * establish the embedded render-pass description and retained device
     * objects, two viewports, scissor rectangle, draw identifiers and filters,
     * pass flags, depth bias, graphics pipeline, and KCD2's pass-name pointer.
     */
    class CSceneRenderPass : public CRenderPassBase {
      public:
        enum class EPassFlags : std::uint32_t {
            None = 0,
            RenderNearest = 1U << 0U,
            ReverseDepth = 1U << 1U,
            UseVrProjectionState = 1U << 2U,
            RequireVrProjectionConstants = 1U << 3U,
            VrProjectionPass = UseVrProjectionState |
                               RequireVrProjectionConstants,
        };

        ~CSceneRenderPass() override = default;

        const CDeviceRenderPassDesc &GetRenderPassDescription() const {
            return m_renderPassDescription0050;
        }
        CDeviceRenderPass *GetRenderPass() const { return m_pRenderPass; }
        const SceneRenderViewportLayout &GetViewport(bool nearest) const {
            return m_viewports[nearest ? 1 : 0];
        }
        const SceneRenderRectangleLayout &GetScissorRectangle() const {
            return m_scissorRectangle01B0;
        }
        CDeviceResourceLayout *GetResourceLayout() const {
            return m_pResourceLayout;
        }
        CDeviceResourceSet *GetPerPassResourceSet() const {
            return m_pPerPassResourceSet;
        }
        CDeviceResourceSet *GetDrawExtraResourceSet() const {
            return m_pDrawExtraResourceSet;
        }
        std::uint16_t GetTechnique() const { return m_technique01D8; }
        std::uint16_t GetStageId() const { return m_stageId01DC; }
        std::uint16_t GetPassId() const { return m_passId01DE; }
        std::uint32_t GetBatchIncludeFilter() const {
            return m_batchIncludeFilter01E0;
        }
        std::uint32_t GetBatchExcludeFilter() const {
            return m_batchExcludeFilter01E4;
        }
        EPassFlags GetPassFlags() const { return m_passFlags01E8; }
        std::uint32_t GetRenderItemGroupCount() const {
            return m_renderItemGroupCount01EC;
        }
        float GetDepthConstantBias() const { return m_depthConstantBias01F0; }
        float GetDepthSlopeBias() const { return m_depthSlopeBias01F4; }
        float GetDepthBiasClamp() const { return m_depthBiasClamp01F8; }
        CGraphicsPipeline *GetGraphicsPipeline() const {
            return m_pGraphicsPipeline;
        }
        const char *GetPassName() const { return m_pPassName; }

      protected:
        CDeviceRenderPassDesc m_renderPassDescription0050; // 0x050
        CDeviceRenderPass *m_pRenderPass;                 // 0x170
        void *m_pRenderPassControlBlock;                  // 0x178
        SceneRenderViewportLayout m_viewports[2];         // 0x180
        SceneRenderRectangleLayout m_scissorRectangle01B0; // 0x1B0
        CDeviceResourceLayout *m_pResourceLayout;         // 0x1C0
        CDeviceResourceSet *m_pPerPassResourceSet;        // 0x1C8
        CDeviceResourceSet *m_pDrawExtraResourceSet;      // 0x1D0
        std::uint16_t m_technique01D8;                    // 0x1D8
        std::uint16_t m_padding01DA;                      // 0x1DA
        std::uint16_t m_stageId01DC;                      // 0x1DC
        std::uint16_t m_passId01DE;                       // 0x1DE
        std::uint32_t m_batchIncludeFilter01E0;           // 0x1E0
        std::uint32_t m_batchExcludeFilter01E4;           // 0x1E4
        EPassFlags m_passFlags01E8;                       // 0x1E8
        std::uint32_t m_renderItemGroupCount01EC;         // 0x1EC
        float m_depthConstantBias01F0;                    // 0x1F0
        float m_depthSlopeBias01F4;                       // 0x1F4
        float m_depthBiasClamp01F8;                       // 0x1F8
        std::uint32_t m_padding01FC;                      // 0x1FC
        CGraphicsPipeline *m_pGraphicsPipeline;           // 0x200
        const char *m_pPassName;                          // 0x208
    };

    static_assert(sizeof(SceneRenderViewportLayout) == 0x18,
                  "SceneRenderViewportLayout size mismatch");
    static_assert(sizeof(SceneRenderRectangleLayout) == 0x10,
                  "SceneRenderRectangleLayout size mismatch");
    static_assert(sizeof(CSceneRenderPass) == 0x210,
                  "CSceneRenderPass size mismatch");
} // namespace SDK
