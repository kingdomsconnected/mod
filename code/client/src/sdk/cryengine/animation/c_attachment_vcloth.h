#pragma once

#include "c_attachment_skin_base.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    /**
     * Concrete CryAnimation vertex-cloth attachment.
     *
     * RTTI proves the global-namespace hierarchy through
     * C_AttachmentSkinBase. Its primary table at 0x183A8F798 contains the 49
     * base entries plus one operation; its secondary IAttachmentSkin table at
     * 0x183A8F6E8 adds nothing. Default construction and factory allocation
     * prove the complete 0x118-byte layout. Fields below are the writes of
     * the constructor at 0x180CBEB30.
     */
    class CAttachmentVCLOTH : public C_AttachmentSkinBase {
      public:
        ~CAttachmentVCLOTH() override = default;

        virtual void unk_fn0049() = 0;

      private:
        void *m_array0058;          // 0x058, points at the shared empty sentinel 0x185665C00
        std::uint32_t m_count0060;    // 0x060, zero
        std::uint16_t m_flags0064;   // 0x064, 0x0
        std::byte m_pad0066[0x2];    // 0x066
        void *m_array0068;          // 0x068, points at the shared empty sentinel 0x185665C00
        std::uint32_t m_count0070;    // 0x070, zero
        std::uint16_t m_flags0074;   // 0x074, 0x100
        std::byte m_pad0076[0x2];    // 0x076
        void *m_array0078;          // 0x078, points at the shared empty sentinel 0x185665C00
        std::uint32_t m_count0080;    // 0x080, zero
        std::uint16_t m_flags0084;   // 0x084, 0x200
        std::byte m_pad0086[0x2];    // 0x086
        void *m_array0088;          // 0x088, points at the shared empty sentinel 0x185665C00
        std::uint32_t m_count0090;    // 0x090, zero
        std::uint16_t m_flags0094;   // 0x094, 0x300
        std::byte m_pad0096[0x2];    // 0x096
        void *m_pointer0098;                  // 0x098, null; released through 0x1809780E8 when set
        void *m_pointer00A0;                  // 0x0A0, null
        void *m_pointer00A8;                  // 0x0A8, null
        void *m_pointer00B0;                  // 0x0B0, null
        void *m_pointer00B8;                  // 0x0B8, null
        void *m_pointer00C0;                  // 0x0C0, null
        std::int64_t m_state00C8;             // 0x0C8, interlocked -1
        std::byte m_state00D0[0x30];          // 0x0D0, zeroed
        void *m_pointer0100;                  // 0x100, null
        void *m_pointer0108;                  // 0x108, null
        std::uint32_t m_state0110;            // 0x110, zero
        std::byte m_pad0114[0x4];             // 0x114

        static void AssertLayout() {
            static_assert(offsetof(CAttachmentVCLOTH, m_flags0094) == 0x94, "CAttachmentVCLOTH::m_flags0094 offset mismatch");
            static_assert(offsetof(CAttachmentVCLOTH, m_state00C8) == 0xC8, "CAttachmentVCLOTH::m_state00C8 offset mismatch");
            static_assert(offsetof(CAttachmentVCLOTH, m_state0110) == 0x110, "CAttachmentVCLOTH::m_state0110 offset mismatch");
        }
    };

    static_assert(sizeof(CAttachmentVCLOTH) == 0x118,
                  "CAttachmentVCLOTH size mismatch");
} // namespace SDK
