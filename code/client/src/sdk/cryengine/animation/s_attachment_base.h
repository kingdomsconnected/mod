#pragma once

#include "../../types.h"
#include "c_attachment_manager.h"
#include "i_attachment_object.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    /**
     * Shared non-polymorphic state embedded by skinned attachments.
     *
     * RTTI identifies this global-namespace type as a struct and places it at
     * C_AttachmentSkinBase+0x10. Its default constructor initializes exactly
     * 0x28 bytes before the derived class's first member.
     *
     * Members follow CryAnimation's SAttachmentBase order exactly; the
     * constructor at 0x180976058 zeroes +0x00, +0x10, +0x18 and +0x20,
     * interlocked-zeroes +0x14 and points +0x08 at the empty CryStringT.
     * CAttachmentManager::CreateAttachment writes the manager at +0x20 and
     * assigns the socket name at +0x08.
     */
    struct alignas(8) SAttachmentBase {
        std::uint32_t m_AttFlags;                 // 0x00
        CryStringT<char> m_strSocketName;         // 0x08
        std::uint32_t m_nSocketCRC32;             // 0x10
        std::int32_t m_nRefCounter;               // 0x14, interlocked
        IAttachmentObject *m_pIAttachmentObject;  // 0x18
        CAttachmentManager *m_pAttachmentManager; // 0x20
    };

    static_assert(sizeof(SAttachmentBase) == 0x28,
                  "SAttachmentBase size mismatch");
    static_assert(offsetof(SAttachmentBase, m_strSocketName) == 0x08, "SAttachmentBase::m_strSocketName offset mismatch");
    static_assert(offsetof(SAttachmentBase, m_nSocketCRC32) == 0x10, "SAttachmentBase::m_nSocketCRC32 offset mismatch");
    static_assert(offsetof(SAttachmentBase, m_pIAttachmentObject) == 0x18, "SAttachmentBase::m_pIAttachmentObject offset mismatch");
    static_assert(offsetof(SAttachmentBase, m_pAttachmentManager) == 0x20, "SAttachmentBase::m_pAttachmentManager offset mismatch");
    static_assert(alignof(SAttachmentBase) == 0x8,
                  "SAttachmentBase alignment mismatch");
} // namespace SDK
