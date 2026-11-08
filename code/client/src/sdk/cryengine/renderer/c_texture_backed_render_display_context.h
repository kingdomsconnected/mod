#pragma once

#include "c_render_display_context.h"

namespace SDK {
    /**
     * Off-screen display context backed by the base color-target reference.
     *
     * RTTI proves CRenderDisplayContext as the sole base and sized deletion
     * proves that this specialization adds no storage: its exact extent is
     * still 0x1B0. Resolution changes lazily create `$TextureRT %d:Current`
     * into the base color target at +0xA8 using dimensions +0x8C/+0x90 and
     * the virtual color format. Presented and storable output slots return
     * that target; the current swap-chain-buffer slot returns null.
     */
    class CTextureBackedRenderDisplayContext
        : public CRenderDisplayContext {
      protected:
        void ChangeDisplayResolution(
            std::uint32_t displayWidth, std::uint32_t displayHeight,
            const SRenderViewportLayout &viewport) override = 0;

      public:
        ~CTextureBackedRenderDisplayContext() override = default;
        CTexture *GetPresentedBackBuffer() const override {
            return m_pColorTarget;
        }
        CTexture *GetCurrentBackBuffer() const override { return nullptr; }
        CTexture *OpaqueVirtual11() override { return nullptr; }
        CTexture *GetStorableColorOutput() override { return m_pColorTarget; }
    };

    static_assert(sizeof(CTextureBackedRenderDisplayContext) == 0x1B0,
                  "CTextureBackedRenderDisplayContext size mismatch");
} // namespace SDK
