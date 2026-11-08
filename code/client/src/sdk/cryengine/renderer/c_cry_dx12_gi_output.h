#pragma once

#include "c_cry_dx12_gi_adapter.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    struct DxgiOutputDescriptionLayout {
        std::byte data[0x60];
    };

    struct DxgiOutputDescription1Layout {
        std::byte data[0x98];
    };

    struct DxgiModeDescription1Layout {
        std::uint32_t width;
        std::uint32_t height;
        std::uint32_t refreshRateNumerator;
        std::uint32_t refreshRateDenominator;
        std::uint32_t format;
        std::uint32_t scanlineOrdering;
        std::uint32_t scaling;
        Bool32 stereo;
    };

    struct DxgiGammaControlCapabilitiesLayout {
        std::byte data[0x1014];
    };

    struct DxgiGammaControlLayout {
        std::byte data[0x3024];
    };

    class IEmptySurface;
    class IEmptyResource;
    class IEmptyOutputDuplication;

    /**
     * Header-independent ABI form of KCD2's IEmptyOutput4.
     *
     * Unlike the older CryEngine declaration, KCD2 inserts the native
     * IDXGIOutput6 GetDesc1 operation immediately after GetDesc. The remaining
     * methods retain IDXGIOutput through IDXGIOutput4 order, producing 28
     * total slots after CryEngine's additional slot-three destructor.
     */
    class IEmptyOutput4 : public IEmptyGfxRefCountable {
      public:
        virtual HResult GetDesc(DxgiOutputDescriptionLayout *description) = 0;
        virtual HResult GetDesc1(
            DxgiOutputDescription1Layout *description) = 0;
        virtual HResult GetDisplayModeList(
            std::uint32_t format, std::uint32_t flags,
            std::uint32_t *modeCount,
            DxgiModeDescriptionLayout *descriptions) = 0;
        virtual HResult FindClosestMatchingMode(
            const DxgiModeDescriptionLayout *modeToMatch,
            DxgiModeDescriptionLayout *closestMatch,
            IUnknown *concernedDevice) = 0;
        virtual HResult WaitForVBlank() = 0;
        virtual HResult TakeOwnership(IUnknown *device, Bool32 exclusive) = 0;
        virtual void ReleaseOwnership() = 0;
        virtual HResult GetGammaControlCapabilities(
            DxgiGammaControlCapabilitiesLayout *capabilities) = 0;
        virtual HResult SetGammaControl(
            const DxgiGammaControlLayout *gammaControl) = 0;
        virtual HResult GetGammaControl(
            DxgiGammaControlLayout *gammaControl) = 0;
        virtual HResult SetDisplaySurface(IEmptySurface *surface) = 0;
        virtual HResult GetDisplaySurfaceData(IEmptySurface *destination) = 0;
        virtual HResult GetFrameStatistics(
            DxgiFrameStatisticsLayout *statistics) = 0;
        virtual HResult GetDisplayModeList1(
            std::uint32_t format, std::uint32_t flags,
            std::uint32_t *modeCount,
            DxgiModeDescription1Layout *descriptions) = 0;
        virtual HResult FindClosestMatchingMode1(
            const DxgiModeDescription1Layout *modeToMatch,
            DxgiModeDescription1Layout *closestMatch,
            IUnknown *concernedDevice) = 0;
        virtual HResult GetDisplaySurfaceData1(
            IEmptyResource *destination) = 0;
        virtual HResult DuplicateOutput(
            IUnknown *device, IEmptyOutputDuplication **duplication) = 0;
        virtual Bool32 SupportsOverlays() = 0;
        virtual HResult CheckOverlaySupport(
            std::uint32_t format, IUnknown *concernedDevice,
            std::uint32_t *flags) = 0;
        virtual HResult CheckOverlayColorSpaceSupport(
            std::uint32_t format, std::uint32_t colorSpace,
            IUnknown *concernedDevice, std::uint32_t *flags) = 0;
    };

    /**
     * Engine-facing DXGI output.
     *
     * Creation enumerates an output through the adapter at +0x18, queries the
     * IEmptyOutput4-compatible native interface, and allocates exactly 0x20
     * bytes. Construction writes the adapter at +0x10 and retained native
     * output at +0x18; destruction releases the latter before sized deletion.
     */
    class CCryDX12GIOutput : public CCryDX12GIObject<IEmptyOutput4> {
      public:
        ~CCryDX12GIOutput() override = default;

        static CCryDX12GIOutput *Create(CCryDX12GIAdapter *adapter,
                                        std::uint32_t output);

        CCryDX12GIAdapter *GetAdapter() const { return m_pAdapter; }
        void *GetDXGIOutput() const { return m_pDXGIOutput4; }

      protected:
        CCryDX12GIAdapter *m_pAdapter; // 0x10
        void *m_pDXGIOutput4;          // 0x18, retained native output
    };

    static_assert(sizeof(DxgiOutputDescriptionLayout) == 0x60,
                  "DXGI output description layout mismatch");
    static_assert(sizeof(DxgiOutputDescription1Layout) == 0x98,
                  "DXGI output description 1 layout mismatch");
    static_assert(sizeof(DxgiModeDescription1Layout) == 0x20,
                  "DXGI mode description 1 layout mismatch");
    static_assert(sizeof(DxgiGammaControlCapabilitiesLayout) == 0x1014,
                  "DXGI gamma capabilities layout mismatch");
    static_assert(sizeof(DxgiGammaControlLayout) == 0x3024,
                  "DXGI gamma-control layout mismatch");
    static_assert(sizeof(CCryDX12GIObject<IEmptyOutput4>) == 0x10,
                  "CCryDX12GIObject<IEmptyOutput4> size mismatch");
    static_assert(sizeof(CCryDX12GIOutput) == 0x20,
                  "CCryDX12GIOutput size mismatch");
} // namespace SDK
