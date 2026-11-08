#pragma once

#include "i_attachment.h"
#include "c_pendula_row.h"
#include "s_attachment_base.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    /**
     * Concrete CryAnimation row/pendulum attachment.
     *
     * RTTI proves the global-namespace hierarchy with IAttachment at +0x00
     * and SAttachmentBase at +0x08. Its table at 0x184797A50 contains the 45
     * inherited entries plus one operation. Factory allocation and sized
     * deletion prove the complete 0x100-byte layout. The inlined constructor
     * in CAttachmentManager::CreateAttachment (0x1820ED500, type 4) builds
     * the row joint name at +0x30, a CPendulaRow at +0x38 and stores -1 at
     * +0xF8; CryEngine's int32 m_nRowJointID moved there as an int16.
     */
    class CAttachmentPROW : public IAttachment, public SAttachmentBase {
      public:
        ~CAttachmentPROW() override = default;

        virtual void unk_fn0045() = 0;

      private:
        CryStringT<char> m_strRowJointName; // 0x30
        CPendulaRow m_rowparams;            // 0x38
        std::int16_t m_nRowJointID;         // 0xF8, -1 in the constructor
        std::byte m_pad00FA[0x6];           // 0xFA

        static void AssertLayout() {
            static_assert(offsetof(CAttachmentPROW, m_strRowJointName) == 0x30, "CAttachmentPROW::m_strRowJointName offset mismatch");
            static_assert(offsetof(CAttachmentPROW, m_rowparams) == 0x38, "CAttachmentPROW::m_rowparams offset mismatch");
            static_assert(offsetof(CAttachmentPROW, m_nRowJointID) == 0xF8, "CAttachmentPROW::m_nRowJointID offset mismatch");
        }
    };

    static_assert(sizeof(CAttachmentPROW) == 0x100,
                  "CAttachmentPROW size mismatch");
} // namespace SDK
