#pragma once

#include <cstddef>
#include <cstdint>

namespace SDK::wh::playermodule {
    /**
     * One row of the alchemy recipe database, `Libs/Tables/minigame/AlchemyRecipe.xml`:
     * RTTI `C_AlchemyRecipe : I_AlchemyRecipe` at offset 0, members as the RTTR
     * registration (0x18077C204) names them.
     *
     * Rows live by value in the database's vector, 0x80 apart (the
     * `RebuildIndex` sort 0x1819ED774 strides `>> 7` and moves them), so a
     * pointer to one is borrowed until the next append. Never construct, copy
     * or retain one in mod code.
     */
    class C_AlchemyRecipe {
      public:
        std::int32_t GetRecipeId() const {
            return m_recipeId;
        }

      private:
        const void *m_pVTable;         // 0x00, I_AlchemyRecipe; slot 1 answers the RecipeId
        std::int32_t m_recipeId;       // 0x08, RecipeId, the key
        std::byte m_unknown000C[0x74]; // 0x0C, BaseMaterial, UIName, UIUsage, UIIcon, BaseQualityRewardXP, the ingredient, product and step vectors, DlcId

        static void AssertLayout() {
            static_assert(offsetof(C_AlchemyRecipe, m_recipeId) == 0x08, "C_AlchemyRecipe::m_recipeId offset mismatch");
        }
    };

    static_assert(sizeof(C_AlchemyRecipe) == 0x80, "C_AlchemyRecipe size mismatch");
} // namespace SDK::wh::playermodule
