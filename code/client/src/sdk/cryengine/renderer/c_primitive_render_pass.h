#pragma once

#include "c_device_render_pass.h"
#include "c_device_render_pass_desc.h"
#include "c_device_resource_set.h"
#include "c_render_pass_base.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    class CGraphicsPipeline;

    /** Empty private base present in the native RTTI hierarchy. */
    class NoCopy {
      protected:
        NoCopy() = default;
        ~NoCopy() = default;

      private:
        NoCopy(const NoCopy &) = delete;
        NoCopy &operator=(const NoCopy &) = delete;
    };

    /** ABI copy of the native six-float primitive-pass viewport. */
    struct PrimitiveRenderViewportLayout {
        float topLeftX;
        float topLeftY;
        float width;
        float height;
        float minimumDepth;
        float maximumDepth;
    };

    /** ABI copy of the native integer primitive-pass scissor rectangle. */
    struct PrimitiveRenderRectangleLayout {
        std::int32_t left;
        std::int32_t top;
        std::int32_t right;
        std::int32_t bottom;
    };

    /**
     * Front-end render pass for a sequence of compiled render primitives.
     *
     * Vector emplacement proves the exact 0x2C0-byte extent. Construction,
     * copying, and teardown establish the render-pass description, paired
     * output resource descriptions and device sets, viewport/scissor state,
     * compiled-primitive vector, graphics pipeline, and KCD2 pass name. RTTI
     * additionally proves CRenderPassBase and the empty NoCopy private base.
     */
    class CPrimitiveRenderPass : public CRenderPassBase, private NoCopy {
      public:
        enum class EPrimitivePassFlags : std::uint32_t {
            None = 0,
            UseVrProjectionState = 1U << 0U,
            RequireVrProjectionConstants = 1U << 1U,
            VrProjectionPass = UseVrProjectionState |
                               RequireVrProjectionConstants,
        };

        enum class EClearMask : std::uint32_t {
            None = 0,
            Stencil = 1U << 1U,
            Color0 = 1U << 2U,
        };

        ~CPrimitiveRenderPass() override = default;
        bool IsDirty() const override;

        EPrimitivePassFlags GetPassFlags() const { return m_passFlags0050; }
        const CDeviceRenderPassDesc &GetRenderPassDescription() const {
            return m_renderPassDescription0058;
        }
        CDeviceRenderPass *GetRenderPass() const { return m_pRenderPass; }
        const CDeviceResourceSetDesc &GetOutputResourceDescription() const {
            return m_outputResources0188;
        }
        CDeviceResourceSet *GetOutputResourceSet() const {
            return m_pOutputResourceSet;
        }
        const CDeviceResourceSetDesc &GetNullOutputResourceDescription() const {
            return m_outputNullResources01F8;
        }
        CDeviceResourceSet *GetNullOutputResourceSet() const {
            return m_pOutputNullResourceSet;
        }
        const PrimitiveRenderViewportLayout &GetViewport() const {
            return m_viewport0268;
        }
        const PrimitiveRenderRectangleLayout &GetScissorRectangle() const {
            return m_scissorRectangle0280;
        }
        bool IsScissorEnabled() const { return m_scissorEnabled0290; }
        bool IsAddingPrimitives() const { return m_addingPrimitives0291; }
        EClearMask GetClearMask() const { return m_clearMask0294; }
        std::size_t GetCompiledPrimitiveCount() const {
            return m_compiledPrimitivesBegin
                       ? static_cast<std::size_t>(m_compiledPrimitivesEnd -
                                                  m_compiledPrimitivesBegin)
                       : 0;
        }
        CGraphicsPipeline *GetGraphicsPipeline() const {
            return m_pGraphicsPipeline;
        }
        const char *GetPassName() const { return m_pPassName; }

      protected:
        EPrimitivePassFlags m_passFlags0050;              // 0x050
        std::uint32_t m_padding0054;                       // 0x054
        CDeviceRenderPassDesc m_renderPassDescription0058; // 0x058
        CDeviceRenderPass *m_pRenderPass;                  // 0x178
        void *m_pRenderPassControlBlock;                   // 0x180
        CDeviceResourceSetDesc m_outputResources0188;      // 0x188
        CDeviceResourceSet *m_pOutputResourceSet;          // 0x1F0
        CDeviceResourceSetDesc m_outputNullResources01F8;  // 0x1F8
        CDeviceResourceSet *m_pOutputNullResourceSet;      // 0x260
        PrimitiveRenderViewportLayout m_viewport0268;      // 0x268
        PrimitiveRenderRectangleLayout m_scissorRectangle0280; // 0x280
        bool m_scissorEnabled0290;                         // 0x290
        bool m_addingPrimitives0291;                       // 0x291
        std::byte m_padding0292[0x2];                      // 0x292
        EClearMask m_clearMask0294;                        // 0x294
        void **m_compiledPrimitivesBegin;                  // 0x298
        void **m_compiledPrimitivesEnd;                    // 0x2A0
        void **m_compiledPrimitivesCapacity;               // 0x2A8
        CGraphicsPipeline *m_pGraphicsPipeline;            // 0x2B0
        const char *m_pPassName;                           // 0x2B8
    };

    static_assert(sizeof(PrimitiveRenderViewportLayout) == 0x18,
                  "PrimitiveRenderViewportLayout size mismatch");
    static_assert(sizeof(PrimitiveRenderRectangleLayout) == 0x10,
                  "PrimitiveRenderRectangleLayout size mismatch");
    static_assert(sizeof(CPrimitiveRenderPass) == 0x2C0,
                  "CPrimitiveRenderPass size mismatch");
} // namespace SDK
