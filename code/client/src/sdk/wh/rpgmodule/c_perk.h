#pragma once

#include "../../types.h"

#include <cstddef>

namespace SDK::wh::rpgmodule {
    /**
     * One perk a soul owns or could own. `C_PerkList::AddPerk` writes the
     * definition GUID at +0x10 of the object it allocates and runs slot 9 on
     * it; `RemovePerk` compares the same 16 bytes. Which subclass a perk
     * becomes -- `C_BuffPerk`, `C_ScriptPerk`, ... -- depends on the perk's
     * side-table rows, so nothing past the GUID is common to all of them.
     *
     * Slots proven off `C_PerkList::AddPerk`/`RemovePerk`: 0 deleting
     * destructor, 1 `Apply(sourcePerk)`, 2 `Remove`, 3 `IsApplicable`; slot
     * 5 off `C_Soul::HasPerk`'s walk (0x181FDC220).
     *
     * The extent is proven only through the GUID. This type is only ever
     * reached through a pointer the game handed out; never allocate or embed
     * one.
     */
    class C_Perk {
      public:
        virtual ~C_Perk()                      = default;
        virtual void Apply(C_Perk *sourcePerk) = 0; // slot 1, 0x18047077C on the base
        virtual void Remove()                  = 0; // slot 2
        virtual bool IsApplicable()            = 0; // slot 3
        virtual void unk_fn0004()              = 0; // slot 4

        /**
         * Slot 5: the perks this one holds, which are owned with it. A
         * `C_MetaPerk` answers its vector at +0x48 (0x181A73940), the children
         * its `Apply` made from the rows that name it as their metaperk
         * (0x180A1FFD4); every other perk answers an empty one (0x180468784).
         * `C_Soul::HasPerk` walks them too (0x181FDC220). The game's own
         * vector, read in place.
         */
        virtual const StdVector<C_Perk *> *GetChildPerks() = 0;

        const CryGUID &GetPerkId() const {
            return m_perkId;
        }

      private:
        std::byte m_opaque0008[0x8];
        CryGUID m_perkId; // 0x10

        static void AssertLayout() {
            static_assert(offsetof(C_Perk, m_perkId) == 0x10, "C_Perk::m_perkId offset mismatch");
        }
    };

    static_assert(sizeof(C_Perk) == 0x20, "C_Perk size mismatch");
} // namespace SDK::wh::rpgmodule
