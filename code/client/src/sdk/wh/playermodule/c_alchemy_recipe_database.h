#pragma once

#include "../databasemodule/c_object_database_key_indexed.h"
#include "../databasemodule/c_object_tree_database.h"
#include "c_alchemy_recipe.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace SDK::wh::playermodule {
    /**
     * The game's alchemy recipes, table `minigame` / `AlchemyRecipe`: one
     * process-global object (0x185332AF0, built by 0x1819DE484).
     *
     * Rows live by value in the vector at +0x30, sorted by RecipeId. No
     * reader keeps a row across frames: the minigame, the recipe book and the
     * crafting screen hold the id and look the row up again, so
     * `AppendFromXml` followed at once by `RebuildIndex`, from the mod's own
     * tick, may grow the vector. `Load` (slot 9, 0x1817DE714) destroys every
     * row before it reads the tables back, appended ones included.
     */
    class C_AlchemyRecipeDatabase: public databasemodule::C_ObjectDatabaseKeyIndexed<databasemodule::C_ObjectTreeDatabase<C_AlchemyRecipe, std::vector>, databasemodule::C_ObjectDatabaseDefaultKeyExtractor<C_AlchemyRecipe>> {
      public:
        ~C_AlchemyRecipeDatabase() override = default;

        static C_AlchemyRecipeDatabase *GetInstance();

        /** `FindRecipeById` (0x1808C3C0C): the row keyed `recipeId`, or null. Borrowed: an append may move it. Right only while the rows are sorted. */
        const C_AlchemyRecipe *FindRecipe(std::int32_t recipeId) const;

      private:
        // The secondary base I_AlchemyRecipeDatabase (GetEnabledRecipes, GetKnownRecipes, FindRecipe).
        const void *m_pAlchemyRecipeDatabaseVTable; // 0x78

        static void AssertLayout() {
            static_assert(offsetof(C_AlchemyRecipeDatabase, m_pAlchemyRecipeDatabaseVTable) == 0x78, "C_AlchemyRecipeDatabase::m_pAlchemyRecipeDatabaseVTable offset mismatch");
        }
    };

    static_assert(sizeof(C_AlchemyRecipeDatabase) == 0x80, "C_AlchemyRecipeDatabase size mismatch");
} // namespace SDK::wh::playermodule
