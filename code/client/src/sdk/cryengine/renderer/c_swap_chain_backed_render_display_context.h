#pragma once

#include "c_cry_dx12_gi_output.h"
#include "c_render_display_context.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    struct DxgiSurfaceDescriptionLayout {
        std::uint32_t width;
        std::uint32_t height;
        std::uint32_t format;
        std::uint32_t sampleCount;
        std::uint32_t sampleQuality;
    };

    /** Exact inline CSwapChain value used by the display context. */
    struct CSwapChainLayout {
        CCryDX12SwapChain *swapChain;       // 0x00, intrusive reference
        CCryDX12GIOutput *output;           // 0x08
        DxgiSurfaceDescriptionLayout surfaceDescription; // 0x10
        std::uint32_t refreshRateNumerator; // 0x24
        std::uint32_t refreshRateDenominator; // 0x28
        std::uint32_t padding002C;
    };

    /**
     * Window/swap-chain implementation of CRenderDisplayContext.
     *
     * RTTI proves CRenderDisplayContext as the sole base. Sized deletion gives
     * the exact 0x228-byte extent. The derived tail contains the window,
     * complete inline CSwapChain, monitor/output state, back-buffer vector,
     * presented/proxy textures, swap-proxy/fullscreen flags, and one terminal
     * KCD2 state dword whose source identity remains opaque.
     */
    class CSwapChainBackedRenderDisplayContext
        : public CRenderDisplayContext {
      protected:
        void ReleaseResources() override = 0;
        void ChangeDisplayResolution(
            std::uint32_t displayWidth, std::uint32_t displayHeight,
            const SRenderViewportLayout &viewport) override = 0;

      public:
        ~CSwapChainBackedRenderDisplayContext() override = default;
        bool IsSwapChainBacked() const override { return true; }
        void PrePresent() override = 0;
        void PostPresent() override = 0;
        CTexture *GetPresentedBackBuffer() const override = 0;
        CTexture *GetCurrentBackBuffer() const override = 0;
        bool IsHighDynamicRangeDisplay() const override = 0;
        bool OpaqueVirtual09(float &value) const override = 0;
        std::uint32_t GetColorFormat() const override = 0;
        CTexture *OpaqueVirtual11() override = 0;
        CTexture *GetStorableColorOutput() override = 0;

        void *GetWindowHandle() const { return m_windowHandle; }
        const CSwapChainLayout &GetSwapChain() const { return m_swapChain; }
        void *GetMonitorHandle() const { return m_monitorHandle; }
        CCryDX12GIOutput *GetOutput() const { return m_pOutput; }
        CTexture *const *GetBackBuffersBegin() const {
            return m_backBuffersBegin;
        }
        CTexture *const *GetBackBuffersEnd() const { return m_backBuffersEnd; }
        std::size_t GetBackBufferCount() const {
            return m_backBuffersBegin
                       ? static_cast<std::size_t>(m_backBuffersEnd -
                                                  m_backBuffersBegin)
                       : 0;
        }
        CTexture *GetPresentedBackBufferRaw() const {
            return m_pBackBufferPresented;
        }
        CTexture *GetBackBufferProxy() const { return m_pBackBufferProxy; }
        bool UsesSwapProxy() const { return m_swapProxy; }
        bool IsFullscreen() const { return m_fullscreen; }
        std::uint32_t GetRefreshRateNumerator() const {
            return m_swapChain.refreshRateNumerator;
        }
        std::uint32_t GetRefreshRateDenominator() const {
            return m_swapChain.refreshRateDenominator;
        }

      protected:
        void *m_windowHandle;                 // 0x1B0, HWND
        CSwapChainLayout m_swapChain;         // 0x1B8
        void *m_monitorHandle;                // 0x1E8, HMONITOR-like state
        CCryDX12GIOutput *m_pOutput;          // 0x1F0
        CTexture **m_backBuffersBegin;        // 0x1F8
        CTexture **m_backBuffersEnd;          // 0x200
        CTexture **m_backBuffersCapacity;     // 0x208
        CTexture *m_pBackBufferPresented;     // 0x210
        CTexture *m_pBackBufferProxy;         // 0x218
        bool m_swapProxy;                     // 0x220
        bool m_fullscreen;                    // 0x221
        std::byte m_padding0222[0x2];         // 0x222
        std::uint32_t m_state0224;             // 0x224
    };

    static_assert(sizeof(DxgiSurfaceDescriptionLayout) == 0x14,
                  "DXGI surface description layout mismatch");
    static_assert(sizeof(CSwapChainLayout) == 0x30,
                  "CSwapChain layout mismatch");
    static_assert(sizeof(CSwapChainBackedRenderDisplayContext) == 0x228,
                  "CSwapChainBackedRenderDisplayContext size mismatch");
} // namespace SDK
