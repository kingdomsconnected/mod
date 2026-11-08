#pragma once

#include "c_render_pass_base.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    class CGraphicsPipeline;

    /**
     * Front-end render-resource transfer pass.
     *
     * Both constructors clear the exact 0x60-byte derived tail and the scalar
     * deleting destructor proves the complete 0xB0-byte extent. The common
     * cleanup path dispatches on the byte at +0x50, detaches invalidation
     * callbacks from the participating resources at +0x58/+0x60 as required
     * by that command, and clears all three fields. The command-specific
     * payload remains opaque because its discriminant values and member types
     * are not yet proven strongly enough to publish.
     */
    class CResourceTransferPass : public CRenderPassBase {
      public:
        ~CResourceTransferPass() override = default;

        std::uint8_t GetTransferCommandRaw() const {
            return m_transferCommand0050;
        }
        void *GetSourceResource() const { return m_pSourceResource; }
        void *GetDestinationResource() const { return m_pDestinationResource; }
        CGraphicsPipeline *GetGraphicsPipeline() const {
            return m_pGraphicsPipeline;
        }

      protected:
        std::uint8_t m_transferCommand0050; // 0x50
        std::byte m_padding0051[0x7];       // 0x51
        void *m_pSourceResource;            // 0x58
        void *m_pDestinationResource;       // 0x60
        std::byte m_transferArguments0068[0x40]; // 0x68
        CGraphicsPipeline *m_pGraphicsPipeline;  // 0xA8
    };

    static_assert(sizeof(CResourceTransferPass) == 0xB0,
                  "CResourceTransferPass size mismatch");
} // namespace SDK
