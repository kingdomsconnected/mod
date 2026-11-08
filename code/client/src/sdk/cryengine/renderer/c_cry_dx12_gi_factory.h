#pragma once

#include "c_cry_dx12_swap_chain.h"

#include <cstdint>

namespace SDK {
    class IEmptyAdapter3;

    /** Header-independent ABI form of CryEngine's IDXGIFactory6 facade. */
    class IEmptyFactory6 : public IEmptyGfxRefCountable {
      public:
        virtual HResult EnumAdapters(std::uint32_t adapter,
                                     IEmptyAdapter3 **result) = 0;
        virtual HResult MakeWindowAssociation(void *window,
                                              std::uint32_t flags) = 0;
        virtual HResult GetWindowAssociation(void **window) = 0;
        virtual HResult CreateSwapChain(
            IUnknown *device,
            NCryDX12::SwapChainDescriptionLayout *description,
            IEmptySwapChain3 **swapChain) = 0;
        virtual HResult CreateSoftwareAdapter(void *module,
                                              IEmptyAdapter3 **adapter) = 0;
        virtual HResult EnumAdapters1(std::uint32_t adapter,
                                      IEmptyAdapter3 **result) = 0;
        virtual Bool32 IsCurrent() = 0;
        virtual Bool32 IsWindowedStereoEnabled() = 0;
        virtual HResult CreateSwapChainForHwnd(
            IUnknown *device, void *window,
            const DxgiSwapChainDescription1Layout *description,
            const DxgiSwapChainFullscreenDescriptionLayout *fullscreen,
            IEmptyOutput *restrictToOutput,
            IEmptySwapChain3 **swapChain) = 0;
        virtual HResult CreateSwapChainForCoreWindow(
            IUnknown *device, IUnknown *window,
            const DxgiSwapChainDescription1Layout *description,
            IEmptyOutput *restrictToOutput,
            IEmptySwapChain3 **swapChain) = 0;
        virtual HResult GetSharedResourceAdapterLuid(void *resource,
                                                     std::uint64_t *luid) = 0;
        virtual HResult RegisterStereoStatusWindow(
            void *window, std::uint32_t message,
            std::uint32_t *cookie) = 0;
        virtual HResult RegisterStereoStatusEvent(
            void *event, std::uint32_t *cookie) = 0;
        virtual void UnregisterStereoStatus(std::uint32_t cookie) = 0;
        virtual HResult RegisterOcclusionStatusWindow(
            void *window, std::uint32_t message,
            std::uint32_t *cookie) = 0;
        virtual HResult RegisterOcclusionStatusEvent(
            void *event, std::uint32_t *cookie) = 0;
        virtual void UnregisterOcclusionStatus(std::uint32_t cookie) = 0;
        virtual HResult CreateSwapChainForComposition(
            IUnknown *device,
            const DxgiSwapChainDescription1Layout *description,
            IEmptyOutput *restrictToOutput,
            IEmptySwapChain3 **swapChain) = 0;
        virtual std::uint32_t GetCreationFlags() = 0;
        virtual HResult EnumAdapterByLuid(std::uint64_t luid,
                                         const GuidLayout &interfaceId,
                                         void **adapter) = 0;
        virtual HResult EnumWarpAdapter(const GuidLayout &interfaceId,
                                        void **adapter) = 0;
        virtual HResult CheckFeatureSupport(std::uint32_t feature,
                                            void *featureSupportData,
                                            std::uint32_t dataSize) = 0;
        virtual HResult EnumAdapterByGpuPreference(
            std::uint32_t adapter, std::uint32_t preference,
            const GuidLayout &interfaceId, void **result) = 0;
    };

    /**
     * Engine-facing DXGI factory.
     *
     * RTTI proves CCryDX12GIObject<IEmptyFactory6> as the direct base. Create
     * requests an exact 0x18-byte object after CreateDXGIFactory1 succeeds,
     * stores the retained native factory at +0x10, and initializes the
     * intrusive count at +0x08. The 31-slot vtable reaches IDXGIFactory6.
     */
    class CCryDX12GIFactory : public CCryDX12GIObject<IEmptyFactory6> {
      public:
        ~CCryDX12GIFactory() override = default;

        static CCryDX12GIFactory *Create();

        void *GetDXGIFactory() const { return m_pDXGIFactory6; }

      protected:
        void *m_pDXGIFactory6; // 0x10, retained IDXGIFactory6
    };

    static_assert(sizeof(CCryDX12GIObject<IEmptyFactory6>) == 0x10,
                  "CCryDX12GIObject<IEmptyFactory6> size mismatch");
    static_assert(sizeof(CCryDX12GIFactory) == 0x18,
                  "CCryDX12GIFactory size mismatch");
} // namespace SDK
