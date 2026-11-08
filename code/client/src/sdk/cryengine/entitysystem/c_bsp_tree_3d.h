#pragma once

#include "i_bsp_tree_3d.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    /**
     * Concrete BSP volume created by IEntitySystem.
     *
     * RTTI places IBSPTree3D at +0x00. The factory allocates exactly 0x10
     * bytes and construction stores the compiled, header-counted BSP record
     * payload at +0x08. Buffer serialization proves each retained record is
     * exactly 0x18 bytes; its stripped internal type remains opaque.
     */
    class CBSPTree3D : public IBSPTree3D {
      public:
        ~CBSPTree3D() override = default;

        const void *GetRecordStorage() const { return m_pRecords; }

        std::uint32_t GetRecordCount() const {
            if (m_pRecords == nullptr) {
                return 0;
            }

            return *reinterpret_cast<const std::uint32_t *>(
                reinterpret_cast<const std::byte *>(m_pRecords) - 0x4);
        }

      private:
        void *m_pRecords; // 0x08, header-counted array of 0x18-byte records
    };

    static_assert(sizeof(CBSPTree3D) == 0x10, "CBSPTree3D size mismatch");
} // namespace SDK
