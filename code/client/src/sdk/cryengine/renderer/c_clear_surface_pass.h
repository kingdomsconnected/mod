#pragma once

#include "c_resource_transfer_pass.h"

#include <cstdint>

namespace SDK {
    /** RTTI-proven utility-pass interface; its only virtual is destruction. */
    class IUtilityRenderPass {
      public:
        virtual ~IUtilityRenderPass() = default;
    };

    /**
     * Clear operation implemented through the resource-transfer pipeline.
     *
     * KCD2 expands the historically stateless utility pass into an exact
     * 0xC0-byte object. Every recovered constructor stores its graphics
     * pipeline at +0x08 and constructs a complete CResourceTransferPass at
     * +0x10 with that same pipeline. The scalar deleting destructor tears
     * down the embedded pass and sized-deletes exactly 0xC0 bytes.
     */
    class CClearSurfacePass : public IUtilityRenderPass {
      public:
        ~CClearSurfacePass() override = default;

        CGraphicsPipeline *GetGraphicsPipeline() const {
            return m_pGraphicsPipeline;
        }
        CResourceTransferPass &GetTransferPass() { return m_transferPass; }
        const CResourceTransferPass &GetTransferPass() const {
            return m_transferPass;
        }

      protected:
        CGraphicsPipeline *m_pGraphicsPipeline; // 0x08
        CResourceTransferPass m_transferPass;   // 0x10
    };

    static_assert(sizeof(IUtilityRenderPass) == 0x08,
                  "IUtilityRenderPass size mismatch");
    static_assert(sizeof(CClearSurfacePass) == 0xC0,
                  "CClearSurfacePass size mismatch");
} // namespace SDK
