#pragma once

#include "c_device_compute_pso.h"
#include "c_device_resource_layout.h"
#include "c_device_resource_set.h"
#include "c_render_pass_base.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    class CGraphicsPipeline;
    class CShader;

    /**
     * Front-end compute render pass.
     *
     * The scalar deleting destructor proves the exact 0x170-byte extent.
     * Construction and IsDirty establish the complete front half through the
     * current PSO update count. Teardown then proves the three retained device
     * objects and the constant manager at +0x108; KCD2 stores the graphics
     * pipeline at +0x130 and adds the native pass-name pointer at +0x168.
     */
    class CComputeRenderPass : public CRenderPassBase {
      public:
        enum class EPassFlags : std::uint32_t {
            None = 0,
            ReflectConstantBuffersFromShader = 1U << 0U,
        };

        enum class EDirtyFlags : std::uint32_t {
            None = 0,
            ResourceLayout = 1U << 0U,
            Resources = 1U << 1U,
            Technique = 1U << 2U,
            All = ResourceLayout | Resources | Technique,
        };

        ~CComputeRenderPass() override = default;
        bool IsDirty() const override;

        EPassFlags GetPassFlags() const { return m_flags0050; }
        EDirtyFlags GetDirtyFlags() const { return m_dirtyMask0054; }
        CShader *GetShader() const { return m_pShader; }
        std::uint32_t GetTechniqueNameCrc() const {
            return m_techniqueNameCrc0060;
        }
        std::uint64_t GetRuntimeMask() const { return m_runtimeMask0068; }
        std::uint32_t GetDispatchSizeX() const { return m_dispatchSize0070[0]; }
        std::uint32_t GetDispatchSizeY() const { return m_dispatchSize0070[1]; }
        std::uint32_t GetDispatchSizeZ() const { return m_dispatchSize0070[2]; }
        CDeviceResourceSet *GetResourceSet() const { return m_pResourceSet; }
        CDeviceResourceLayout *GetResourceLayout() const {
            return m_pResourceLayout;
        }
        CDeviceComputePSO *GetPipelineState() const {
            return m_pPipelineState;
        }
        std::int32_t GetCurrentPsoUpdateCount() const {
            return m_currentPsoUpdateCount0100;
        }
        CGraphicsPipeline *GetGraphicsPipeline() const {
            return m_pGraphicsPipeline;
        }
        bool IsPendingConstantUpdate() const {
            return m_pendingConstantUpdate0138;
        }
        bool IsCompiled() const { return m_compiled0139; }
        const char *GetPassName() const { return m_pPassName; }

      protected:
        EPassFlags m_flags0050;                    // 0x050
        EDirtyFlags m_dirtyMask0054;               // 0x054
        CShader *m_pShader;                        // 0x058
        std::uint32_t m_techniqueNameCrc0060;      // 0x060
        std::uint32_t m_padding0064;               // 0x064
        std::uint64_t m_runtimeMask0068;           // 0x068
        std::uint32_t m_dispatchSize0070[3];       // 0x070
        std::uint32_t m_padding007C;               // 0x07C
        std::byte m_resourceDescription0080[0x68]; // 0x080
        CDeviceResourceSet *m_pResourceSet;        // 0x0E8
        CDeviceResourceLayout *m_pResourceLayout;  // 0x0F0
        CDeviceComputePSO *m_pPipelineState;       // 0x0F8
        std::int32_t m_currentPsoUpdateCount0100;  // 0x100
        std::uint32_t m_padding0104;               // 0x104
        std::byte m_constantManager0108[0x28];     // 0x108
        CGraphicsPipeline *m_pGraphicsPipeline;    // 0x130
        bool m_pendingConstantUpdate0138;          // 0x138
        bool m_compiled0139;                       // 0x139
        std::byte m_profilingStats013A[0x2E];      // 0x13A
        const char *m_pPassName;                   // 0x168
    };

    static_assert(sizeof(CComputeRenderPass) == 0x170,
                  "CComputeRenderPass size mismatch");
} // namespace SDK
