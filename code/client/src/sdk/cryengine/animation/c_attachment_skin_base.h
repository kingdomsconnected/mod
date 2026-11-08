#pragma once

#include "i_attachment.h"
#include "i_attachment_skin.h"
#include "s_attachment_base.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    /**
     * Shared polymorphic base for CryAnimation skinned attachments.
     *
     * RTTI proves the global-namespace hierarchy IAttachment at +0x00,
     * IAttachmentSkin at +0x08, and SAttachmentBase at +0x10. The primary
     * table at 0x18405FA78 extends IAttachment by four operations; the
     * secondary table at 0x18405FC08 implements exactly IAttachmentSkin's 21
     * slots. Sized deletion and derived construction prove the 0x58 layout.
     * The constructor at 0x1809769FC writes everything after the base.
     */
    class C_AttachmentSkinBase : public IAttachment,
                                 public IAttachmentSkin,
                                 public SAttachmentBase {
      public:
        ~C_AttachmentSkinBase() override = default;

        virtual void unk_fn0045() = 0;
        virtual void unk_fn0046() = 0;
        virtual void unk_fn0047() = 0;
        virtual void unk_fn0048() = 0;

      private:
        void *m_pointer0038;          // 0x38, null in the constructor
        void *m_pointer0040;          // 0x40, null in the constructor
        void *m_lock0048;             // 0x48, SRWLOCK (InitializeSRWLock in the constructor)
        std::uint8_t m_state0050;     // 0x50, zero in the constructor
        std::byte m_pad0051[0x7];     // 0x51

        static void AssertLayout() {
            static_assert(offsetof(C_AttachmentSkinBase, m_pointer0038) == 0x38, "C_AttachmentSkinBase::m_pointer0038 offset mismatch");
            static_assert(offsetof(C_AttachmentSkinBase, m_lock0048) == 0x48, "C_AttachmentSkinBase::m_lock0048 offset mismatch");
            static_assert(offsetof(C_AttachmentSkinBase, m_state0050) == 0x50, "C_AttachmentSkinBase::m_state0050 offset mismatch");
        }
    };

    static_assert(sizeof(C_AttachmentSkinBase) == 0x58,
                  "C_AttachmentSkinBase size mismatch");
} // namespace SDK
