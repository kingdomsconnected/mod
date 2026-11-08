#pragma once

#include "../../types.h"
#include "i_physical_entity.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    /**
     * Concrete CryPhysics placeholder base, vtable 0x183A577C0.
     *
     * RTTI places IPhysicalEntity at +0x00. The table implements all 28
     * interface entries and appends three placeholder operations. Its vector
     * deleting destructor proves both a 0x50-byte element stride and scalar
     * allocation size.
     */
    class CPhysicalPlaceholder : public IPhysicalEntity {
      public:
        ~CPhysicalPlaceholder() override = default;

        virtual void unk_fn0028() = 0;
        virtual void unk_fn0029() = 0;
        virtual void unk_fn0030() = 0; // +0xF0

      protected:
        // CPhysicalPlaceholder occupies 0x50 bytes. The slot-1 query at
        // 0x1838CF788 reads the simulation class from the top byte of the id
        // word at +0x44.
        SDK::Vec3 m_BBox[2];             // 0x08
        void *m_pForeignData;            // 0x20
        std::uint32_t m_foreignDataAndFlags; // 0x28, m_iForeignData : 16, m_iForeignFlags : 16
        std::int16_t m_ig[2][2];         // 0x2C, the two grid cells, x and y each
        std::int32_t m_iGThunk0;         // 0x34
        CPhysicalPlaceholder *m_pEntBuddy; // 0x38
        volatile std::uint32_t m_bProcessed; // 0x40
        std::uint32_t m_idAndSimClass;   // 0x44, m_id : 23, m_bOBBThunks : 1, m_iSimClass : 8
        std::int32_t m_lockUpdate;       // 0x48
        std::uint32_t m_padding004C;

      private:
        static void AssertLayout() {
            static_assert(offsetof(CPhysicalPlaceholder, m_pForeignData) == 0x20, "CPhysicalPlaceholder::m_pForeignData offset mismatch");
            static_assert(offsetof(CPhysicalPlaceholder, m_pEntBuddy) == 0x38, "CPhysicalPlaceholder::m_pEntBuddy offset mismatch");
            static_assert(offsetof(CPhysicalPlaceholder, m_idAndSimClass) == 0x44, "CPhysicalPlaceholder::m_idAndSimClass offset mismatch");
        }
    };

    static_assert(sizeof(CPhysicalPlaceholder) == 0x50,
                  "CPhysicalPlaceholder size mismatch");
} // namespace SDK
