#pragma once

#include "s_resource_binding.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    /**
     * API-independent collection of resources bound to a device resource set.
     *
     * Construction and copying prove that the leading container is a sorted
     * vector of 0x20-byte binding records. Clear() walks those records,
     * unregisters every live resource from the inline invalidation callback,
     * then ORs both resource and sampler dirty bits into +0x60. The callback's
     * concrete functor representation is kept opaque because its private Cry
     * template type is not required to inspect the stable layout.
     */
    class CDeviceResourceSetDesc {
      public:
        static constexpr std::size_t ResourceBindingStride = 0x20;

        std::size_t GetResourceBindingCount() const {
            return m_resourceBindingsBegin
                       ? static_cast<std::size_t>(m_resourceBindingsEnd -
                                                  m_resourceBindingsBegin)
                       : 0;
        }
        std::uint32_t GetDirtyFlags() const { return m_dirtyFlags0060; }
        void *GetInvalidationCallbackOwner() const {
            return m_pInvalidationCallbackOwner0058;
        }

      protected:
        ResourceBindingPairLayout *m_resourceBindingsBegin;    // 0x00
        ResourceBindingPairLayout *m_resourceBindingsEnd;      // 0x08
        ResourceBindingPairLayout *m_resourceBindingsCapacity; // 0x10
        std::byte m_invalidationCallback0018[0x38]; // 0x18
        void *m_pInvalidationCallbackTarget0050;  // 0x50
        void *m_pInvalidationCallbackOwner0058;   // 0x58
        std::uint32_t m_dirtyFlags0060;            // 0x60
        std::byte m_padding0064[0x4];              // 0x64
    };

    /**
     * API-independent render-device resource-set base.
     *
     * KCD2's RTTI gives this class one direct derived implementation on the
     * active renderer path, CDeviceResourceSet_DX12. Its deleting destructor
     * proves the 0x18-byte extent. Update() dispatches the sole non-destructor
     * virtual with the resource description and its dirty flags, identifying
     * that slot as UpdateImpl.
     */
    class CDeviceResourceSet {
      public:
        virtual ~CDeviceResourceSet() = default;

        bool IsValid() const { return m_valid0012; }
        std::uint8_t GetFlagsRaw() const { return m_flags0010; }
        std::int32_t GetReferenceCount() const { return m_referenceCount0014; }

      protected:
        virtual bool UpdateImpl(const CDeviceResourceSetDesc &description,
                                std::uint32_t dirtyFlags) = 0;

        CDeviceResourceSetDesc *m_pResourceDescription; // 0x08
        std::uint8_t m_flags0010;                        // 0x10
        std::uint8_t m_state0011;                        // 0x11
        bool m_valid0012;                                // 0x12
        std::byte m_padding0013;                         // 0x13
        std::int32_t m_referenceCount0014;               // 0x14
    };

    namespace NCryDX12 {
        class CResource;

        /** Native DX12 descriptor allocation retained by a resource set. */
        struct DescriptorBlockLayout {
            void *heap;            // 0x00
            std::uint32_t offset;  // 0x08
            std::uint32_t size;    // 0x0C
        };

        /** One resolved native resource retained by a DX12 resource set. */
        struct DeviceResourceSetBindingLayout {
            SResourceBindPoint m_bindPoint; // 0x00
            std::uint32_t m_resourceState;  // 0x10, D3D12_RESOURCE_STATES
            std::uint32_t m_padding0014;
            CResource *m_pResource;         // 0x18, intrusive reference
        };
    } // namespace NCryDX12

    /**
     * DX12 resource-set implementation used by the device object factory.
     *
     * Both inlined construction sites allocate exactly 0x40 bytes. The
     * derived destructor releases the descriptor block at +0x18, then walks
     * and frees the 0x20-stride resource-binding vector at +0x28. UpdateImpl
     * copies each front-end bind point into the native record, records the
     * resolved D3D12 resource state at +0x10, and transfers an intrusive
     * NCryDX12::CResource reference into +0x18.
     */
    class CDeviceResourceSet_DX12 : public CDeviceResourceSet {
      public:
        ~CDeviceResourceSet_DX12() override = default;

        const NCryDX12::DescriptorBlockLayout &GetDescriptorBlock() const {
            return m_descriptorBlockDX12;
        }

        const NCryDX12::DeviceResourceSetBindingLayout *GetResourceBindings()
            const {
            return m_resourceBindingsBegin;
        }

        std::size_t GetResourceBindingCount() const {
            return m_resourceBindingsBegin
                       ? static_cast<std::size_t>(m_resourceBindingsEnd -
                                                  m_resourceBindingsBegin)
                       : 0;
        }

      protected:
        bool UpdateImpl(const CDeviceResourceSetDesc &description,
                        std::uint32_t dirtyFlags) override;

        NCryDX12::DescriptorBlockLayout m_descriptorBlockDX12; // 0x18
        NCryDX12::DeviceResourceSetBindingLayout
            *m_resourceBindingsBegin;                           // 0x28
        NCryDX12::DeviceResourceSetBindingLayout
            *m_resourceBindingsEnd;                             // 0x30
        NCryDX12::DeviceResourceSetBindingLayout
            *m_resourceBindingsCapacity;                        // 0x38
    };

    static_assert(sizeof(CDeviceResourceSetDesc) == 0x68,
                  "CDeviceResourceSetDesc size mismatch");
    static_assert(sizeof(CDeviceResourceSet) == 0x18,
                  "CDeviceResourceSet size mismatch");
    static_assert(sizeof(NCryDX12::DescriptorBlockLayout) == 0x10,
                  "NCryDX12::DescriptorBlockLayout size mismatch");
    static_assert(sizeof(NCryDX12::DeviceResourceSetBindingLayout) == 0x20,
                  "NCryDX12::DeviceResourceSetBindingLayout size mismatch");
    static_assert(sizeof(CDeviceResourceSet_DX12) == 0x40,
                  "CDeviceResourceSet_DX12 size mismatch");
} // namespace SDK
