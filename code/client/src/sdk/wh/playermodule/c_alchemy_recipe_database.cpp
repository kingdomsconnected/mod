#include "c_alchemy_recipe_database.h"

#include "../../patterns.h"

#include <utils/hooking/hooking.h>

namespace SDK::wh::playermodule {
    C_AlchemyRecipeDatabase *C_AlchemyRecipeDatabase::GetInstance() {
        return reinterpret_cast<C_AlchemyRecipeDatabase *>(gPatterns.C_AlchemyRecipeDatabase__Instance);
    }

    const C_AlchemyRecipe *C_AlchemyRecipeDatabase::FindRecipe(std::int32_t recipeId) const {
        return hook::this_call<const C_AlchemyRecipe *>(gPatterns.C_AlchemyRecipeDatabase__FindRecipeById, this, &recipeId);
    }
} // namespace SDK::wh::playermodule
