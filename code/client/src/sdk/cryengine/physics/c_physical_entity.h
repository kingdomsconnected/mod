#pragma once

#include "c_physical_placeholder.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    /**
     * Concrete CryPhysics entity base, vtable 0x183A578C0.
     *
     * RTTI proves the global-namespace hierarchy CPhysicalEntity ->
     * CPhysicalPlaceholder -> IPhysicalEntity, all at +0x00. The 75-entry
     * table implements the 31 inherited entries and appends 44 operations.
     * Matching factory allocation and sized deletion prove the 0x158-byte
     * object size.
     */
    class CPhysicalEntity : public CPhysicalPlaceholder {
      public:
        ~CPhysicalEntity() override = default;

        virtual void unk_fn0031() = 0;
        virtual void unk_fn0032() = 0;
        virtual void unk_fn0033() = 0;
        virtual void unk_fn0034() = 0;
        virtual void unk_fn0035() = 0;
        virtual void unk_fn0036() = 0;
        virtual void unk_fn0037() = 0;
        virtual void unk_fn0038() = 0;
        virtual void unk_fn0039() = 0;
        virtual void unk_fn0040() = 0;
        virtual void unk_fn0041() = 0;
        virtual void unk_fn0042() = 0;
        virtual void unk_fn0043() = 0;
        virtual void unk_fn0044() = 0;
        virtual void unk_fn0045() = 0;
        virtual void unk_fn0046() = 0;
        virtual void unk_fn0047() = 0;
        virtual void unk_fn0048() = 0;
        virtual void unk_fn0049() = 0;
        virtual void unk_fn0050() = 0;
        virtual void unk_fn0051() = 0;
        virtual void unk_fn0052() = 0;
        virtual void unk_fn0053() = 0;
        virtual void unk_fn0054() = 0;
        virtual void unk_fn0055() = 0;
        virtual void unk_fn0056() = 0;
        virtual void unk_fn0057() = 0;
        virtual void unk_fn0058() = 0;
        virtual void unk_fn0059() = 0;
        virtual void unk_fn0060() = 0;
        virtual void unk_fn0061() = 0;
        virtual void unk_fn0062() = 0;
        virtual void unk_fn0063() = 0;
        virtual void unk_fn0064() = 0;
        virtual void unk_fn0065() = 0;
        virtual void unk_fn0066() = 0;
        virtual void unk_fn0067() = 0;
        virtual void unk_fn0068() = 0;
        virtual void unk_fn0069() = 0;
        virtual void unk_fn0070() = 0;
        virtual void unk_fn0071() = 0;
        virtual void unk_fn0072() = 0;
        virtual void unk_fn0073() = 0;
        virtual void unk_fn0074() = 0; // +0x250

      protected:
        // CPhysicalEntity constructor 0x180845980 stores the world and heap,
        // sets the permanent bit, points m_pNewCoords at m_pos and m_parts at
        // the static default part. KCD2 adds a word at +0x60 and a run of
        // state before m_parts that its own code initializes.
        std::int32_t m_iDeletionTime;    // 0x50
        volatile std::int32_t m_nRefCount; // 0x54
        std::uint32_t m_flags;           // 0x58
        std::uint32_t m_padding005C;
        std::uint64_t m_state0060;       // 0x60
        void *m_pWorld;                  // 0x68, CPhysicalWorld
        void *m_pHeap;                   // 0x70, IGeneralMemoryHeap
        std::uint32_t m_podRefCountAndUpdate; // 0x78, m_nRefCountPOD : 16, m_iLastPODUpdate : 16
        std::uint32_t m_prevSimClassAndFlags; // 0x7C, m_iPrevSimClass : 16, m_bMoved : 8, m_bPermanent : 4, m_bPrevPermanent : 4
        std::int32_t m_iGroup;           // 0x80, -1 by default
        std::uint32_t m_state0084;       // 0x84, 0x8640 by default
        std::byte m_state0088[0x10];     // 0x88
        std::uint64_t m_state0098;       // 0x98
        SDK::Vec3 m_pos;                 // 0xA0
        SDK::Quat m_qrot;                // 0xAC, identity by default
        std::uint32_t m_padding00BC;
        void *m_pNewCoords;              // 0xC0, coord_block, pointing at m_pos
        void *m_pSyncCoords;             // 0xC8, coord_block
        std::byte m_state00D0[0x48];     // 0xD0
        void *m_parts;                   // 0x118, geom array, the static default part until physicalized
        std::byte m_state0120[0x38];     // 0x120

      private:
        static void AssertLayout() {
            static_assert(offsetof(CPhysicalEntity, m_pWorld) == 0x68, "CPhysicalEntity::m_pWorld offset mismatch");
            static_assert(offsetof(CPhysicalEntity, m_iGroup) == 0x80, "CPhysicalEntity::m_iGroup offset mismatch");
            static_assert(offsetof(CPhysicalEntity, m_pos) == 0xA0, "CPhysicalEntity::m_pos offset mismatch");
            static_assert(offsetof(CPhysicalEntity, m_pNewCoords) == 0xC0, "CPhysicalEntity::m_pNewCoords offset mismatch");
            static_assert(offsetof(CPhysicalEntity, m_parts) == 0x118, "CPhysicalEntity::m_parts offset mismatch");
        }
    };

    static_assert(sizeof(CPhysicalEntity) == 0x158,
                  "CPhysicalEntity size mismatch");
} // namespace SDK

