#pragma once

#include "i_attachment_object.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    /**
     * Concrete CryAnimation light attachment payload.
     *
     * RTTI identifies this global-namespace type as a struct and proves the
     * hierarchy CLightAttachment -> IAttachmentObject. Its table at
     * 0x183ECE988 implements exactly the 22 inherited entries and adds no
     * operations. Matching allocation and sized-deletion paths prove the
     * complete 0x10-byte layout.
     */
    struct CLightAttachment : IAttachmentObject {
        ~CLightAttachment() override = default;

      private:
        void *m_pLightSource; // 0x08, ILightSource *; the only member of the 0x10 object

        static void AssertLayout() { static_assert(offsetof(CLightAttachment, m_pLightSource) == 0x08, "CLightAttachment::m_pLightSource offset mismatch"); }
    };

    static_assert(sizeof(CLightAttachment) == 0x10,
                  "CLightAttachment size mismatch");
} // namespace SDK
