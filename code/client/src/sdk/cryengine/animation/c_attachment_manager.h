#pragma once

#include "i_attachment_manager.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    /**
     * Concrete CryAnimation character-attachment manager.
     *
     * RTTI proves the global hierarchy CAttachmentManager ->
     * IAttachmentManager. Its table at 0x184060548 implements all 45
     * interface entries and appends one concrete operation. Default
     * construction and sized scalar deletion prove the 0x100-byte layout;
     * CCharInstance constructs one in place at +0x08.
     *
     * Fields below are the writes of the constructor at
     * ??0CAttachmentManager@@QEAA@XZ. CryEngine's member list does not line
     * up past m_arrAttachments, so the rest carry offset names.
     * CreateAttachment (0x1820ED500) locks +0x18 and appends every new
     * attachment to the vector at +0x78, which the constructor reserves to
     * 32 entries.
     */
    class CAttachmentManager : public IAttachmentManager {
      public:
        ~CAttachmentManager() override = default;

        virtual void unk_fn0045() = 0; // +0x168

      private:
        void *m_pointer0008;          // 0x08, 0x185665430 in the constructor
        std::int16_t m_state0010;     // 0x10, -1
        std::int16_t m_state0012;     // 0x12, -1
        std::byte m_pad0014[0x4];     // 0x14
        std::uint32_t m_lock0018;     // 0x18, lock word (0x102 in the constructor) taken by CreateAttachment
        std::byte m_pad001C[0x4];     // 0x1C
        void *m_pointer0020;          // 0x20, null
        void *m_pointer0028;          // 0x28, null
        void *m_pointer0030[6];       // 0x30, zeroed
        std::int32_t m_state0060;     // 0x60, -1
        std::uint32_t m_state0064;    // 0x64, zero
        void *m_pointer0068;          // 0x68, null
        void *m_pointer0070;          // 0x70, null
        void *m_arrAttachments[3];    // 0x78, std::vector<_smart_ptr<IAttachment>>: first, last, end
        void *m_pointer0090;          // 0x90, 0x185675D50 in the constructor
        void *m_pointer0098;          // 0x98, 0x185675D40 in the constructor
        void *m_pointer00A0;          // 0xA0, null
        void *m_pointer00A8;          // 0xA8, null
        std::uint32_t m_state00B0;    // 0xB0, 1
        std::int32_t m_state00B4;     // 0xB4, -1
        std::byte m_state00B8[0x48];  // 0xB8, zeroed by memset

        static void AssertLayout() {
            static_assert(offsetof(CAttachmentManager, m_lock0018) == 0x18, "CAttachmentManager::m_lock0018 offset mismatch");
            static_assert(offsetof(CAttachmentManager, m_state0060) == 0x60, "CAttachmentManager::m_state0060 offset mismatch");
            static_assert(offsetof(CAttachmentManager, m_arrAttachments) == 0x78, "CAttachmentManager::m_arrAttachments offset mismatch");
            static_assert(offsetof(CAttachmentManager, m_state00B8) == 0xB8, "CAttachmentManager::m_state00B8 offset mismatch");
        }
    };

    static_assert(sizeof(CAttachmentManager) == 0x100,
                  "CAttachmentManager size mismatch");
} // namespace SDK
