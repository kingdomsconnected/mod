#pragma once

#include "c_device_resource.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    /**
     * High-level render-device texture.
     *
     * All observed creation paths allocate and initialize exactly 0x88 bytes,
     * matching the scalar deleting destructor's sized free. The derived tail
     * retains the base allocation size, packed texture state, two staging
     * resources with their mapped-memory and fence companions, and optional
     * render-target data. Destruction releases both staging resources, both
     * fences, and the render-target data before entering CDeviceResource's
     * teardown.
     */
    class CDeviceTexture : public CDeviceResource {
      public:
        ~CDeviceTexture() override = default;

        std::uint64_t GetBaseAllocatedSize() const {
            return m_baseAllocatedSize0040;
        }
        std::uint8_t GetTextureFlagsRaw() const { return m_textureFlags0048; }
        bool IsCube() const { return (m_textureFlags0048 & 0x01U) != 0; }
        void *GetStagingResource(std::size_t index) const {
            return index < 2 ? m_pStagingResources[index] : nullptr;
        }
        void *GetStagingMemory(std::size_t index) const {
            return index < 2 ? m_pStagingMemory[index] : nullptr;
        }
        void *GetStagingFence(std::size_t index) const {
            return index < 2 ? m_pStagingFences[index] : nullptr;
        }
        void *GetRenderTargetData() const { return m_pRenderTargetData; }

      protected:
        std::uint64_t m_baseAllocatedSize0040; // 0x40
        std::uint8_t m_textureFlags0048;       // 0x48
        std::byte m_padding0049[0x7];          // 0x49
        void *m_pStagingResources[2];          // 0x50
        void *m_pStagingMemory[2];             // 0x60
        void *m_pStagingFences[2];             // 0x70
        void *m_pRenderTargetData;              // 0x80
    };

    static_assert(sizeof(CDeviceTexture) == 0x88,
                  "CDeviceTexture size mismatch");
} // namespace SDK
