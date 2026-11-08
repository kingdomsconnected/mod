#pragma once

#include "../../types.h"
#include "i_attachment.h"
#include "s_attachment_base.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    /**
     * Concrete CryAnimation bone attachment.
     *
     * RTTI proves the global hierarchy with IAttachment at +0x00 and
     * SAttachmentBase at +0x08. Its table at 0x183A657C0 implements exactly the 45 inherited entries and
     * adds no virtual operations. The attachment factory's exact 0x130-byte
     * allocation and the this-only constructor prove the complete layout.
     *
     * The constructor at 0x180975F08 (??0CAttachmentBONE@@QEAA@XZ) opens
     * with CryEngine's two default QuatTs (w = 1.0 at +0x3C and +0x58),
     * then diverges: KCD2 drops m_AttModelRelative and m_addTransformation,
     * puts the joint name at +0x68 and an int16 joint id (-1) at +0x78, and
     * replaces CSimulation with two 0x58-byte runs each built by
     * 0x180975F98 as QuatT, uint16, uint8, QuatT, QuatT.
     */
    class CAttachmentBONE : public IAttachment, public SAttachmentBase {
      public:
        ~CAttachmentBONE() override = default;

      private:
        QuatT m_AttRelativeDefault;         // 0x030
        QuatT m_AttAbsoluteDefault;         // 0x04C
        CryStringT<char> m_strJointName;    // 0x068
        void *m_pointer0070;                // 0x070, null in the constructor
        std::int16_t m_nJointID;            // 0x078, -1 in the constructor
        std::uint8_t m_state007A;           // 0x07A, zero in the constructor
        std::byte m_pad007B[0x1];           // 0x07B
        QuatT m_location007C;               // 0x07C, identity
        std::uint16_t m_state0098;          // 0x098, zero
        std::uint8_t m_state009A;           // 0x09A, zero
        std::byte m_pad009B[0x1];           // 0x09B
        QuatT m_location009C;               // 0x09C, identity
        QuatT m_location00B8;               // 0x0B8, identity
        QuatT m_location00D4;               // 0x0D4, identity
        std::uint16_t m_state00F0;          // 0x0F0, zero
        std::uint8_t m_state00F2;           // 0x0F2, zero
        std::byte m_pad00F3[0x1];           // 0x0F3
        QuatT m_location00F4;               // 0x0F4, identity
        QuatT m_location0110;               // 0x110, identity
        std::byte m_pad012C[0x4];           // 0x12C

        static void AssertLayout() {
            static_assert(offsetof(CAttachmentBONE, m_AttRelativeDefault) == 0x30, "CAttachmentBONE::m_AttRelativeDefault offset mismatch");
            static_assert(offsetof(CAttachmentBONE, m_AttAbsoluteDefault) == 0x4C, "CAttachmentBONE::m_AttAbsoluteDefault offset mismatch");
            static_assert(offsetof(CAttachmentBONE, m_strJointName) == 0x68, "CAttachmentBONE::m_strJointName offset mismatch");
            static_assert(offsetof(CAttachmentBONE, m_nJointID) == 0x78, "CAttachmentBONE::m_nJointID offset mismatch");
            static_assert(offsetof(CAttachmentBONE, m_location007C) == 0x7C, "CAttachmentBONE::m_location007C offset mismatch");
            static_assert(offsetof(CAttachmentBONE, m_location00D4) == 0xD4, "CAttachmentBONE::m_location00D4 offset mismatch");
            static_assert(offsetof(CAttachmentBONE, m_location0110) == 0x110, "CAttachmentBONE::m_location0110 offset mismatch");
        }
    };

    static_assert(sizeof(CAttachmentBONE) == 0x130,
                  "CAttachmentBONE size mismatch");
} // namespace SDK
