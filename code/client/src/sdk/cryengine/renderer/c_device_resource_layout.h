#pragma once

#include "../../ncrydx12/c_device_object_dx12.h"
#include "s_resource_binding.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    /**
     * Resource bindings assigned to one root-signature layout bind point.
     *
     * The nested bind point has a yasli-proven two-byte extent. The complete
     * vector element has a 0x20-byte stride and pairs it with a 0x18-byte
     * VectorMap<SResourceBindPoint, SResourceBinding>. Their private packed
     * payloads remain opaque until individual bit meanings are proven.
     */
    struct DeviceResourceLayoutBindingLayout;

    /**
     * Device root-signature resource layout description.
     *
     * yasli registers the exact 0x20-byte type. Its serializer proves a
     * vector of 0x20-byte layout-binding records at +0x00 and m_Flags at
     * +0x18; the DX12 layout initializer consumes the same vector directly.
     */
    struct SDeviceResourceLayoutDesc {
        struct SLayoutBindPoint {
            std::byte data[0x2];
        };

        std::size_t GetLayoutBindingCount() const;
        std::uint32_t GetFlagsRaw() const { return m_flags0018; }

        DeviceResourceLayoutBindingLayout *m_layoutBindingsBegin;    // 0x00
        DeviceResourceLayoutBindingLayout *m_layoutBindingsEnd;      // 0x08
        DeviceResourceLayoutBindingLayout *m_layoutBindingsCapacity; // 0x10
        std::uint32_t m_flags0018;                                   // 0x18
        std::uint32_t m_padding001C;                                 // 0x1C
    };

    struct DeviceResourceLayoutBindingLayout {
        SDeviceResourceLayoutDesc::SLayoutBindPoint bindPoint; // 0x00
        std::byte padding0002[0x6];                            // 0x02
        ResourceBindingPairLayout *m_resourceBindingsBegin;    // 0x08
        ResourceBindingPairLayout *m_resourceBindingsEnd;      // 0x10
        ResourceBindingPairLayout *m_resourceBindingsCapacity; // 0x18

        std::size_t GetResourceBindingCount() const {
            return m_resourceBindingsBegin
                       ? static_cast<std::size_t>(m_resourceBindingsEnd -
                                                  m_resourceBindingsBegin)
                       : 0;
        }
    };

    inline std::size_t
    SDeviceResourceLayoutDesc::GetLayoutBindingCount() const {
        return m_layoutBindingsBegin
                   ? static_cast<std::size_t>(m_layoutBindingsEnd -
                                              m_layoutBindingsBegin)
                   : 0;
    }

    /**
     * API-independent resource-binding layout.
     *
     * The base scalar deleting destructor proves a 0x18-byte extent. Factory
     * construction derives the required-slot mask from each 0x20-byte layout
     * description entry and stores it at +0x10. Successful DX12 Init computes
     * and stores the complete layout-description hash at +0x08.
     */
    class CDeviceResourceLayout {
      public:
        virtual ~CDeviceResourceLayout() = default;

        std::uint64_t GetLayoutHash() const { return m_layoutHash; }
        std::uint32_t GetRequiredResourceBindingsRaw() const {
            return m_requiredResourceBindings0010;
        }
        std::int32_t GetReferenceCount() const { return m_referenceCount0014; }

      protected:
        std::uint64_t m_layoutHash;                   // 0x08
        std::uint32_t m_requiredResourceBindings0010; // 0x10
        std::int32_t m_referenceCount0014;            // 0x14
    };

    /**
     * Native DX12 resource layout.
     *
     * Its factory allocates exactly 0x28 bytes, stores the intrusive reference
     * count at +0x14 and NCryDX12::CDevice at +0x18, then Init builds the
     * retained NCryDX12::CRootSignature at +0x20. Destruction releases that
     * root signature before sized deletion.
     */
    class CDeviceResourceLayout_DX12 : public CDeviceResourceLayout {
      public:
        ~CDeviceResourceLayout_DX12() override = default;

        bool Init(const SDeviceResourceLayoutDesc &description);

        NCryDX12::CDevice *GetDevice() const { return m_pDevice; }
        NCryDX12::CRootSignature *GetRootSignature() const {
            return m_pRootSignature;
        }

      protected:
        NCryDX12::CDevice *m_pDevice;               // 0x18
        NCryDX12::CRootSignature *m_pRootSignature; // 0x20
    };

    static_assert(
        sizeof(SDeviceResourceLayoutDesc::SLayoutBindPoint) == 0x2,
        "SDeviceResourceLayoutDesc::SLayoutBindPoint size mismatch");
    static_assert(sizeof(DeviceResourceLayoutBindingLayout) == 0x20,
                  "DeviceResourceLayoutBindingLayout size mismatch");
    static_assert(sizeof(SDeviceResourceLayoutDesc) == 0x20,
                  "SDeviceResourceLayoutDesc size mismatch");
    static_assert(sizeof(CDeviceResourceLayout) == 0x18,
                  "CDeviceResourceLayout size mismatch");
    static_assert(sizeof(CDeviceResourceLayout_DX12) == 0x28,
                  "CDeviceResourceLayout_DX12 size mismatch");
} // namespace SDK
