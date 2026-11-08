#pragma once

#include "../databasemodule/c_object_database_key_indexed.h"
#include "../databasemodule/c_object_tree_database.h"
#include "c_blacksmith_recipe.h"

#include <cstddef>
#include <vector>

namespace SDK::wh::playermodule {
    /**
     * The game's smithing recipes, table `minigame` / `BlacksmithRecipes`:
     * one process-global object (0x185332C90, built by 0x1819DE450).
     *
     * Rows live by value in the vector at +0x30, sorted by Id. No reader
     * keeps a row across frames: the minigame keeps the id in its state and
     * finds the row again on every call (`C_Blacksmithing::GetCurrentRecipe`
     * 0x180857D44), so `AppendFromXml` followed at once by `RebuildIndex`,
     * from the mod's own tick, may grow the vector. A row's WorkpieceId is
     * resolved to a `BlacksmithWorkpieces` row as it is read; an id that
     * names none leaves it null, and the anvil dereferences it. `Load`
     * (slot 9, 0x181715B3C) destroys every row before it reads the tables
     * back, appended ones included.
     */
    class C_BlacksmithRecipeDatabase: public databasemodule::C_ObjectDatabaseKeyIndexed<databasemodule::C_ObjectTreeDatabase<C_BlacksmithRecipe, std::vector>, databasemodule::C_ObjectDatabaseDefaultKeyExtractor<C_BlacksmithRecipe>> {
      public:
        ~C_BlacksmithRecipeDatabase() override = default;

        static C_BlacksmithRecipeDatabase *GetInstance();

        /** `FindRecipeById` (0x18085930C): the row keyed `id`, or null. Borrowed: an append may move it. Right only while the rows are sorted. */
        const C_BlacksmithRecipe *FindRecipe(const char *id) const;

      private:
        // The secondary base I_BlacksmithRecipeDatabase (GetAllRecipes, GetRecipesWithOwnedPerk, FindRecipe).
        const void *m_pBlacksmithRecipeDatabaseVTable; // 0x78

        static void AssertLayout() {
            static_assert(offsetof(C_BlacksmithRecipeDatabase, m_pBlacksmithRecipeDatabaseVTable) == 0x78, "C_BlacksmithRecipeDatabase::m_pBlacksmithRecipeDatabaseVTable offset mismatch");
        }
    };

    static_assert(sizeof(C_BlacksmithRecipeDatabase) == 0x80, "C_BlacksmithRecipeDatabase size mismatch");
} // namespace SDK::wh::playermodule
