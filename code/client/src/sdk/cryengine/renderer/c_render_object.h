#pragma once

#include "../3dengine/i_render_node.h"

#include <cstddef>

namespace SDK {
    /** Borrowed prefix only; never construct or allocate this incomplete native layout. */
    struct CRenderObject {
        std::byte unknown0000[0x30];
        // Brush render 1806BAE24 stores its node here. DuplicateRenderObject
        // 18049DA64 calls copy 180D5DAC8, which preserves it at 180D5DB76.
        IRenderNode *renderNode;
    };

    static_assert(offsetof(CRenderObject, renderNode) == 0x30);
} // namespace SDK
