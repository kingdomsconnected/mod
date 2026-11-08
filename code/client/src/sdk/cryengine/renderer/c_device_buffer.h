#pragma once

#include "c_device_resource.h"

#include <cstdint>

namespace SDK {
    /**
     * High-level render-device buffer.
     *
     * Its sole construction path allocates exactly 0x48 bytes, initializes
     * the complete CDeviceResource base, assigns resource type 6, and clears
     * the only derived qword at +0x40. The matching deleting destructor enters
     * CDeviceResource teardown directly before returning the allocation to
     * the renderer's tracked allocator.
     */
    class CDeviceBuffer : public CDeviceResource {
      public:
        ~CDeviceBuffer() override = default;

        std::uint64_t GetBaseAllocatedSize() const {
            return m_baseAllocatedSize0040;
        }

      protected:
        std::uint64_t m_baseAllocatedSize0040; // 0x40
    };

    static_assert(sizeof(CDeviceBuffer) == 0x48,
                  "CDeviceBuffer size mismatch");
} // namespace SDK
