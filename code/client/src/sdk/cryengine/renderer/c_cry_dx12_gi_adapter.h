#pragma once

#include "c_cry_dx12_gi_factory.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    struct DxgiAdapterDescriptionLayout {
        std::byte data[0x130];
    };

    struct DxgiAdapterDescription1Layout {
        std::byte data[0x138];
    };

    struct DxgiAdapterDescription2Layout {
        std::byte data[0x140];
    };

    struct DxgiQueryVideoMemoryInfoLayout {
        std::uint64_t budget;
        std::uint64_t currentUsage;
        std::uint64_t availableForReservation;
        std::uint64_t currentReservation;
    };

    class IEmptyOutput;

    /** Header-independent ABI form of CryEngine's IDXGIAdapter3 facade. */
    class IEmptyAdapter3 : public IEmptyGfxRefCountable {
      public:
        virtual HResult EnumOutputs(std::uint32_t output,
                                    IEmptyOutput **result) = 0;
        virtual HResult GetDesc(DxgiAdapterDescriptionLayout *description) = 0;
        virtual HResult CheckInterfaceSupport(
            const GuidLayout &interfaceId, std::int64_t *umdVersion) = 0;
        virtual HResult GetDesc1(
            DxgiAdapterDescription1Layout *description) = 0;
        virtual HResult GetDesc2(
            DxgiAdapterDescription2Layout *description) = 0;
        virtual HResult RegisterHardwareContentProtectionTeardownStatusEvent(
            void *event, std::uint32_t *cookie) = 0;
        virtual void UnregisterHardwareContentProtectionTeardownStatus(
            std::uint32_t cookie) = 0;
        virtual HResult QueryVideoMemoryInfo(
            std::uint32_t nodeIndex, std::uint32_t memorySegmentGroup,
            DxgiQueryVideoMemoryInfoLayout *memoryInfo) = 0;
        virtual HResult SetVideoMemoryReservation(
            std::uint32_t nodeIndex, std::uint32_t memorySegmentGroup,
            std::uint64_t reservation) = 0;
        virtual HResult RegisterVideoMemoryBudgetChangeNotificationEvent(
            void *event, std::uint32_t *cookie) = 0;
        virtual void UnregisterVideoMemoryBudgetChangeNotification(
            std::uint32_t cookie) = 0;
    };

    /**
     * Engine-facing DXGI adapter.
     *
     * Both allocation paths request exactly 0x20 bytes and initialize the
     * RTTI-proven CCryDX12GIObject<IEmptyAdapter3> base, factory at +0x10, and
     * queried native IDXGIAdapter3 at +0x18. The vtable has 19 slots through
     * IDXGIAdapter3; the wrapper itself does not add hidden storage.
     */
    class CCryDX12GIAdapter : public CCryDX12GIObject<IEmptyAdapter3> {
      public:
        ~CCryDX12GIAdapter() override = default;

        static CCryDX12GIAdapter *Create(CCryDX12GIFactory *factory,
                                         std::uint32_t adapter);

        CCryDX12GIFactory *GetFactory() const { return m_pFactory; }
        void *GetDXGIAdapter() const { return m_pDXGIAdapter3; }

      protected:
        CCryDX12GIFactory *m_pFactory; // 0x10
        void *m_pDXGIAdapter3;         // 0x18, IDXGIAdapter3
    };

    static_assert(sizeof(DxgiAdapterDescriptionLayout) == 0x130,
                  "DXGI adapter description layout mismatch");
    static_assert(sizeof(DxgiAdapterDescription1Layout) == 0x138,
                  "DXGI adapter description 1 layout mismatch");
    static_assert(sizeof(DxgiAdapterDescription2Layout) == 0x140,
                  "DXGI adapter description 2 layout mismatch");
    static_assert(sizeof(DxgiQueryVideoMemoryInfoLayout) == 0x20,
                  "DXGI video-memory info layout mismatch");
    static_assert(sizeof(CCryDX12GIObject<IEmptyAdapter3>) == 0x10,
                  "CCryDX12GIObject<IEmptyAdapter3> size mismatch");
    static_assert(sizeof(CCryDX12GIAdapter) == 0x20,
                  "CCryDX12GIAdapter size mismatch");
} // namespace SDK
