#pragma once

#include "i_attachment.h"
#include "closest_tri.h"
#include "s_attachment_base.h"

#include <cstddef>

namespace SDK {
    /**
     * Concrete CryAnimation face attachment.
     *
     * RTTI proves the global-namespace hierarchy with IAttachment at +0x00
     * and SAttachmentBase at +0x08. Its table at 0x184797BC8 implements
     * exactly IAttachment's 45 slots and adds no operations. Default
     * construction, factory allocation, and sized deletion prove the complete
     * 0x120-byte layout.
     *
     * The constructor at 0x183743A30 initializes four identity QuatTs
     * with w = 1.0 at +0x3C, +0x58, +0x74 and +0x90, and a ClosestTri at
     * +0xA0. The pointer at +0x118 starts null.
     */
    class CAttachmentFACE : public IAttachment, public SAttachmentBase {
      public:
        ~CAttachmentFACE() override = default;

      private:
        QuatT m_AttRelativeDefault; // 0x030
        QuatT m_AttAbsoluteDefault; // 0x04C
        QuatT m_AttModelRelative;   // 0x068
        QuatT m_addTransformation;  // 0x084
        ClosestTri m_Triangle;      // 0x0A0
        void *m_pointer0118;        // 0x118, null in the constructor

        static void AssertLayout() {
            static_assert(offsetof(CAttachmentFACE, m_AttRelativeDefault) == 0x30, "CAttachmentFACE::m_AttRelativeDefault offset mismatch");
            static_assert(offsetof(CAttachmentFACE, m_addTransformation) == 0x84, "CAttachmentFACE::m_addTransformation offset mismatch");
            static_assert(offsetof(CAttachmentFACE, m_Triangle) == 0xA0, "CAttachmentFACE::m_Triangle offset mismatch");
            static_assert(offsetof(CAttachmentFACE, m_pointer0118) == 0x118, "CAttachmentFACE::m_pointer0118 offset mismatch");
        }
    };

    static_assert(sizeof(CAttachmentFACE) == 0x120,
                  "CAttachmentFACE size mismatch");
} // namespace SDK
