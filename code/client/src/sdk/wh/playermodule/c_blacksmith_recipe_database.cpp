#include "c_blacksmith_recipe_database.h"

#include "../../patterns.h"

#include <utils/hooking/hooking.h>

namespace SDK::wh::playermodule {
    C_BlacksmithRecipeDatabase *C_BlacksmithRecipeDatabase::GetInstance() {
        return reinterpret_cast<C_BlacksmithRecipeDatabase *>(gPatterns.C_BlacksmithRecipeDatabase__Instance);
    }

    const C_BlacksmithRecipe *C_BlacksmithRecipeDatabase::FindRecipe(const char *id) const {
        // It compares through the string's character pointer alone, so the engine's own string carries the key.
        const ScopedCryString key(id);
        return hook::this_call<const C_BlacksmithRecipe *>(gPatterns.C_BlacksmithRecipeDatabase__FindRecipeById, this, &key.Get());
    }
} // namespace SDK::wh::playermodule
