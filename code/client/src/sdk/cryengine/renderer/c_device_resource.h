#pragma once

#include <cstddef>
#include <cstdint>

namespace SDK {
    namespace NCryDX12 {
        class CView;
    }

    /**
     * Sorted resource-view cache entry retained by CDeviceResource.
     *
     * Resource initialization constructs packed 64-bit view keys, reserves
     * this 0x10-stride vector, and inserts key/null-view pairs under the
     * renderer's resource-view lock. Lazy view creation stores an intrusive
     * native NCryDX12::CView reference in the second qword; teardown releases
     * that same reference.
     */
    struct CDeviceResourceViewRecord {
        std::uint64_t m_viewKey;       // 0x00, packed renderer view key
        NCryDX12::CView *m_pView;      // 0x08, intrusive reference
    };

    /**
     * API-independent render-device resource base.
     *
     * The scalar deleting destructor frees exactly 0x40 bytes. All three
     * resource-view pointers at +0x28 are cleared by the non-deleting
     * destructor, while the second virtual slot first retires those views and
     * releases the native resource at +0x08. Construction paths for both
     * textures and buffers establish the intervening format, flags, element
     * count, subresource counts, resource type, and packed capability flags.
     *
     * The cleanup slot remains opaque because its exact stripped source name
     * is not proven, even though its behavior is understood.
     */
    class CDeviceResource {
      public:
        virtual ~CDeviceResource() = default;
        virtual void Opaque01() {}

        void *GetNativeResource() const { return m_pNativeResource; }
        std::uint32_t GetNativeFormat() const { return m_nativeFormat0010; }
        std::uint32_t GetFlags() const { return m_flags0014; }
        std::uint32_t GetResourceElements() const {
            return m_resourceElements0018;
        }
        std::uint16_t GetMipCount() const { return m_subResources001C[0]; }
        std::uint16_t GetSliceCount() const { return m_subResources001C[1]; }
        std::uint8_t GetTextureType() const { return m_textureType0020; }
        std::uint8_t GetCapabilityFlagsRaw() const {
            return m_capabilityFlags0021;
        }
        bool IsFilterable() const { return (m_capabilityFlags0021 & 0x01U) != 0; }
        bool IsSrgb() const { return (m_capabilityFlags0021 & 0x02U) != 0; }
        bool AllowsSrgb() const { return (m_capabilityFlags0021 & 0x04U) != 0; }
        bool IsMultisampled() const {
            return (m_capabilityFlags0021 & 0x08U) != 0;
        }
        std::size_t GetResourceViewCount() const {
            return m_pResourceViewsBegin == nullptr
                       ? 0
                       : static_cast<std::size_t>(m_pResourceViewsEnd -
                                                  m_pResourceViewsBegin);
        }
        const CDeviceResourceViewRecord *GetResourceViews() const {
            return m_pResourceViewsBegin;
        }

      protected:
        void *m_pNativeResource;                             // 0x08
        std::uint32_t m_nativeFormat0010;                    // 0x10
        std::uint32_t m_flags0014;                           // 0x14
        std::uint32_t m_resourceElements0018;                // 0x18
        std::uint16_t m_subResources001C[2];                 // 0x1C
        std::uint8_t m_textureType0020;                      // 0x20
        std::uint8_t m_capabilityFlags0021;                  // 0x21
        std::byte m_padding0022[0x6];                        // 0x22
        CDeviceResourceViewRecord *m_pResourceViewsBegin;    // 0x28
        CDeviceResourceViewRecord *m_pResourceViewsEnd;      // 0x30
        CDeviceResourceViewRecord *m_pResourceViewsCapacity; // 0x38
    };

    static_assert(sizeof(CDeviceResourceViewRecord) == 0x10,
                  "CDeviceResourceViewRecord size mismatch");
    static_assert(sizeof(CDeviceResource) == 0x40,
                  "CDeviceResource size mismatch");
} // namespace SDK
