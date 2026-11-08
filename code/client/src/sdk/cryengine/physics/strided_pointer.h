#pragma once

#include <cstddef>
#include <cstdint>

namespace SDK {
    /** CreateMesh takes these aggregates by value; MS x64 passes their 16-byte temporaries by address (0x180992DEC). */
    template <typename Element>
    struct strided_pointer {
        Element *data;
        std::int32_t stride = sizeof(Element);
        std::byte m_opaque000C[4] {};

      private:
        static void AssertLayout() {
            static_assert(offsetof(strided_pointer, data) == 0x00);
            static_assert(offsetof(strided_pointer, stride) == 0x08);
        }
    };

    static_assert(sizeof(strided_pointer<unsigned short>) == 0x10);
} // namespace SDK
