#pragma once

#include "../../ncrydx12/c_device_object_dx12.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    using HResult = std::int32_t;
    using Bool32 = std::int32_t;

    struct GuidLayout {
        std::uint32_t data1;
        std::uint16_t data2;
        std::uint16_t data3;
        std::uint8_t data4[8];
    };

    struct DxgiModeDescriptionLayout {
        std::uint32_t width;
        std::uint32_t height;
        std::uint32_t refreshRateNumerator;
        std::uint32_t refreshRateDenominator;
        std::uint32_t format;
        std::uint32_t scanlineOrdering;
        std::uint32_t scaling;
    };

    struct DxgiFrameStatisticsLayout {
        std::uint32_t presentCount;
        std::uint32_t presentRefreshCount;
        std::uint32_t syncRefreshCount;
        std::uint32_t padding000C;
        std::int64_t syncQpcTime;
        std::int64_t syncGpuTime;
    };

    struct DxgiSwapChainDescription1Layout {
        std::uint32_t width;
        std::uint32_t height;
        std::uint32_t format;
        Bool32 stereo;
        std::uint32_t sampleCount;
        std::uint32_t sampleQuality;
        std::uint32_t bufferUsage;
        std::uint32_t bufferCount;
        std::uint32_t scaling;
        std::uint32_t swapEffect;
        std::uint32_t alphaMode;
        std::uint32_t flags;
    };

    struct DxgiSwapChainFullscreenDescriptionLayout {
        std::uint32_t refreshRateNumerator;
        std::uint32_t refreshRateDenominator;
        std::uint32_t scanlineOrdering;
        std::uint32_t scaling;
        Bool32 windowed;
    };

    struct RectLayout {
        std::int32_t left;
        std::int32_t top;
        std::int32_t right;
        std::int32_t bottom;
    };

    struct PointLayout {
        std::int32_t x;
        std::int32_t y;
    };

    struct DxgiPresentParametersLayout {
        std::uint32_t dirtyRectCount;
        std::uint32_t padding0004;
        const RectLayout *dirtyRects;
        const RectLayout *scrollRect;
        const PointLayout *scrollOffset;
    };

    struct DxgiRgbaLayout {
        float red;
        float green;
        float blue;
        float alpha;
    };

    struct DxgiMatrix3x2Layout {
        float m11;
        float m12;
        float m21;
        float m22;
        float m31;
        float m32;
    };

    class IUnknown {
      public:
        virtual HResult QueryInterface(const GuidLayout &interfaceId,
                                       void **object) = 0;
        virtual std::uint32_t AddRef() = 0;
        virtual std::uint32_t Release() = 0;
    };

    /** CryEngine's ref-counted graphics interface adds a destructor at slot 3. */
    class IEmptyGfxRefCountable : public IUnknown {
      public:
        virtual ~IEmptyGfxRefCountable() = default;
    };

    class IEmptyOutput;

    /**
     * Header-independent ABI form of CryEngine's IDXGISwapChain3 facade.
     *
     * RTTI proves IEmptyGfxRefCountable as its direct base. Slots 4 through
     * 40 follow IDXGIObject, IDXGIDeviceSubObject, and IDXGISwapChain1/2/3 in
     * their native order; slot 3 is CryEngine's additional deleting
     * destructor and therefore is not present in the Microsoft interface.
     */
    class IEmptySwapChain3 : public IEmptyGfxRefCountable {
      public:
        virtual HResult SetPrivateData(const GuidLayout &name,
                                       std::uint32_t dataSize,
                                       const void *data) = 0;
        virtual HResult SetPrivateDataInterface(const GuidLayout &name,
                                                const IUnknown *object) = 0;
        virtual HResult GetPrivateData(const GuidLayout &name,
                                       std::uint32_t *dataSize,
                                       void *data) = 0;
        virtual HResult GetParent(const GuidLayout &interfaceId,
                                  void **parent) = 0;
        virtual HResult GetDevice(const GuidLayout &interfaceId,
                                  void **device) = 0;
        virtual HResult Present(std::uint32_t syncInterval,
                                std::uint32_t flags) = 0;
        virtual HResult GetBuffer(std::uint32_t buffer,
                                  const GuidLayout &interfaceId,
                                  void **surface) = 0;
        virtual HResult SetFullscreenState(Bool32 fullscreen,
                                           IEmptyOutput *target) = 0;
        virtual HResult GetFullscreenState(Bool32 *fullscreen,
                                           IEmptyOutput **target) = 0;
        virtual HResult GetDesc(
            NCryDX12::SwapChainDescriptionLayout *description) = 0;
        virtual HResult ResizeBuffers(std::uint32_t bufferCount,
                                      std::uint32_t width,
                                      std::uint32_t height,
                                      std::uint32_t newFormat,
                                      std::uint32_t flags) = 0;
        virtual HResult ResizeTarget(
            const DxgiModeDescriptionLayout *targetParameters) = 0;
        virtual HResult GetContainingOutput(IEmptyOutput **output) = 0;
        virtual HResult GetFrameStatistics(
            DxgiFrameStatisticsLayout *statistics) = 0;
        virtual HResult GetLastPresentCount(std::uint32_t *count) = 0;
        virtual HResult GetDesc1(
            DxgiSwapChainDescription1Layout *description) = 0;
        virtual HResult GetFullscreenDesc(
            DxgiSwapChainFullscreenDescriptionLayout *description) = 0;
        virtual HResult GetHwnd(void **window) = 0;
        virtual HResult GetCoreWindow(const GuidLayout &interfaceId,
                                      void **window) = 0;
        virtual HResult Present1(
            std::uint32_t syncInterval, std::uint32_t flags,
            const DxgiPresentParametersLayout *parameters) = 0;
        virtual Bool32 IsTemporaryMonoSupported() = 0;
        virtual HResult GetRestrictToOutput(IEmptyOutput **output) = 0;
        virtual HResult SetBackgroundColor(const DxgiRgbaLayout *color) = 0;
        virtual HResult GetBackgroundColor(DxgiRgbaLayout *color) = 0;
        virtual HResult SetRotation(std::uint32_t rotation) = 0;
        virtual HResult GetRotation(std::uint32_t *rotation) = 0;
        virtual HResult SetSourceSize(std::uint32_t width,
                                      std::uint32_t height) = 0;
        virtual HResult GetSourceSize(std::uint32_t *width,
                                      std::uint32_t *height) = 0;
        virtual HResult SetMaximumFrameLatency(std::uint32_t maxLatency) = 0;
        virtual HResult GetMaximumFrameLatency(std::uint32_t *maxLatency) = 0;
        virtual void *GetFrameLatencyWaitableObject() = 0;
        virtual HResult SetMatrixTransform(
            const DxgiMatrix3x2Layout *matrix) = 0;
        virtual HResult GetMatrixTransform(DxgiMatrix3x2Layout *matrix) = 0;
        virtual std::uint32_t GetCurrentBackBufferIndex() = 0;
        virtual HResult CheckColorSpaceSupport(std::uint32_t colorSpace,
                                               std::uint32_t *support) = 0;
        virtual HResult SetColorSpace1(std::uint32_t colorSpace) = 0;
        virtual HResult ResizeBuffers1(
            std::uint32_t bufferCount, std::uint32_t width,
            std::uint32_t height, std::uint32_t format,
            std::uint32_t flags, const std::uint32_t *creationNodeMasks,
            IUnknown *const *presentQueues) = 0;
    };

    /**
     * Common CryEngine GI adapter.
     *
     * QueryInterface/AddRef/Release prove the signed intrusive count at +0x08;
     * the recovered CCryDX12SwapChain constructor clears then increments it.
     */
    template <typename Interface>
    class CCryDX12GIObject : public Interface {
      public:
        std::int32_t GetReferenceCount() const { return m_referenceCount; }

      protected:
        std::int32_t m_referenceCount; // 0x08
        std::uint32_t m_padding000C;
    };

    class CCryDX12Device;

    /**
     * Engine-facing DXGI swap-chain adapter.
     *
     * RTTI gives the exact CCryDX12GIObject<IEmptySwapChain3> hierarchy. Its
     * allocation path requests 0x20 bytes, writes the device at +0x10 and the
     * native NCryDX12::CSwapChain at +0x18, and the destructor releases that
     * native wrapper before restoring the base vtable.
     */
    class CCryDX12SwapChain
        : public CCryDX12GIObject<IEmptySwapChain3> {
      public:
        ~CCryDX12SwapChain() override = default;

        CCryDX12Device *GetCryDevice() const { return m_pDevice; }
        NCryDX12::CSwapChain *GetDX12SwapChain() const {
            return m_pDX12SwapChain;
        }

      protected:
        CCryDX12Device *m_pDevice;               // 0x10
        NCryDX12::CSwapChain *m_pDX12SwapChain;  // 0x18
    };

    static_assert(sizeof(GuidLayout) == 0x10, "GUID layout mismatch");
    static_assert(sizeof(DxgiModeDescriptionLayout) == 0x1C,
                  "DXGI mode description layout mismatch");
    static_assert(sizeof(DxgiFrameStatisticsLayout) == 0x20,
                  "DXGI frame statistics layout mismatch");
    static_assert(sizeof(DxgiSwapChainDescription1Layout) == 0x30,
                  "DXGI swap-chain description 1 layout mismatch");
    static_assert(sizeof(DxgiSwapChainFullscreenDescriptionLayout) == 0x14,
                  "DXGI fullscreen description layout mismatch");
    static_assert(sizeof(DxgiPresentParametersLayout) == 0x20,
                  "DXGI present parameters layout mismatch");
    static_assert(sizeof(DxgiMatrix3x2Layout) == 0x18,
                  "DXGI matrix layout mismatch");
    static_assert(sizeof(CCryDX12GIObject<IEmptySwapChain3>) == 0x10,
                  "CCryDX12GIObject<IEmptySwapChain3> size mismatch");
    static_assert(sizeof(CCryDX12SwapChain) == 0x20,
                  "CCryDX12SwapChain size mismatch");
} // namespace SDK
