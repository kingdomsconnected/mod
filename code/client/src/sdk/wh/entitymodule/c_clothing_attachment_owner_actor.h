#pragma once

#include "c_actor.h"
#include "i_clothing_attachment_owner.h"

namespace SDK::wh::entitymodule {
    /**
     * Clothing attachment owner backed by a C_Actor.
     *
     * The allocation path proves a 0x10-byte object containing the shared
     * 14-slot owner interface and the actor pointer at +0x08.
     */
    class C_ClothingAttachmentOwnerActor : public I_ClothingAttachmentOwner {
      public:
        C_Actor *GetActor() const { return m_actor; }

      private:
        C_Actor *m_actor; // +0x08
    };

    static_assert(sizeof(C_ClothingAttachmentOwnerActor) == 0x10, "C_ClothingAttachmentOwnerActor size mismatch");
} // namespace SDK::wh::entitymodule
