#pragma once

#include "i_clothing_attachment_owner.h"

#include <cstddef>

namespace SDK {
    struct IAnimatedCharacter;
}

namespace SDK::wh::entitymodule {
    /**
     * Clothing attachment owner backed by an animated character.
     *
     * The allocation path proves a 0x70-byte object containing the shared
     * 14-slot owner interface, an IAnimatedCharacter pointer at +0x08, and
     * 0x60 bytes of copied adapter state.
     */
    class C_ClothingAttachmentOwnerAnimChar : public I_ClothingAttachmentOwner {
      public:
        SDK::IAnimatedCharacter *GetAnimatedCharacter() const { return m_animatedCharacter; }

      private:
        SDK::IAnimatedCharacter *m_animatedCharacter; // +0x08
        std::byte m_opaque0010[0x60];
    };

    static_assert(sizeof(C_ClothingAttachmentOwnerAnimChar) == 0x70, "C_ClothingAttachmentOwnerAnimChar size mismatch");
} // namespace SDK::wh::entitymodule
