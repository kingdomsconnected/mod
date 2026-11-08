#pragma once

#include "../../types.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    /**
     * One `<Image Page="N" Image="name"/>` of a document's content: RTTR
     * `wh::entitymodule::Image` (registration 0x1813A439C). `C_UIBook::SetFullBook`
     * walks the vector in 16-byte steps and appends `page` as a uint and
     * `image` as a string.
     */
    struct S_DocumentImage {
        std::uint32_t page;
        std::byte m_padding0004[0x4];
        CryStringT<char> image; // 0x08

        static void AssertLayout() {
            static_assert(offsetof(S_DocumentImage, page) == 0x0, "S_DocumentImage::page offset mismatch");
            static_assert(offsetof(S_DocumentImage, image) == 0x8, "S_DocumentImage::image offset mismatch");
        }
    };

    static_assert(sizeof(S_DocumentImage) == 0x10, "S_DocumentImage size mismatch");

    /**
     * A document's text and pictures: RTTR `wh::entitymodule::DocumentContent`,
     * held by `S_DocumentItemClass` at +0xC0. 0x30 bytes, allocated and zeroed
     * by the RTTR factory at 0x1813B1BD0.
     *
     * `parts` is the item row's `DocumentContent Parts="k1 k2 ..."` split on
     * spaces (0x1806B7500): one localization key per chunk of text. Pages are
     * not a property of the data; the book movie splits the concatenated
     * chunks on `<newpage/>`.
     */
    struct S_DocumentContent {
        StdVector<CryStringT<char>> parts; // 0x00
        StdVector<S_DocumentImage> images; // 0x18

        static void AssertLayout() {
            static_assert(offsetof(S_DocumentContent, parts) == 0x0, "S_DocumentContent::parts offset mismatch");
            static_assert(offsetof(S_DocumentContent, images) == 0x18, "S_DocumentContent::images offset mismatch");
        }
    };

    static_assert(sizeof(S_DocumentContent) == 0x30, "S_DocumentContent size mismatch");
} // namespace SDK::wh::entitymodule
