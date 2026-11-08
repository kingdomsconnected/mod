#pragma once

#include "../../types.h"

#include <cstddef>

namespace SDK::wh::playermodule {
    /**
     * One row of the smithing recipe database, `Libs/Tables/minigame/BlacksmithRecipes.xml`:
     * RTTI `C_BlacksmithRecipe : I_BlacksmithRecipe` at offset 0, members as
     * the RTTR registration (0x181511734) names them. Its first virtual is
     * the id getter, not a destructor.
     *
     * Rows live by value in the database's vector, 0x98 apart (the
     * `RebuildIndex` sort 0x1819ED704 divides by it and moves them), so a
     * pointer to one is borrowed until the next append. Never construct, copy
     * or retain one in mod code.
     */
    class C_BlacksmithRecipe {
      public:
        const char *GetId() const {
            return m_id.m_pData;
        }

      private:
        const void *m_pVTable;         // 0x00, I_BlacksmithRecipe; slot 0 answers the Id
        CryStringT<char> m_id;         // 0x08, Id, the key
        std::byte m_unknown0010[0x88]; // 0x10, UIName, UIIcon, UIDesc, Category, MinSkillLevel, PerkId, the ingredient vector, the workpiece row, the product vector, DlcId

        static void AssertLayout() {
            static_assert(offsetof(C_BlacksmithRecipe, m_id) == 0x08, "C_BlacksmithRecipe::m_id offset mismatch");
        }
    };

    static_assert(sizeof(C_BlacksmithRecipe) == 0x98, "C_BlacksmithRecipe size mismatch");
} // namespace SDK::wh::playermodule
