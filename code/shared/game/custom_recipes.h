#pragma once

#include "alchemy_catalog.h"
#include "crafting_catalog.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace KCDC::Shared::Game {
    // Where the game draws a recipe's pictures, each a square of the size it ships: the icon in
    // the recipe lists (Libs/UI/Textures/Icons/Items/<UIIcon>_icon.dds, as every item icon, 64),
    // and in the alchemy book the product beside the description
    // (Libs/UI/Textures/Books/Products/<RecipeId>_ui.dds, 256) and the recipe picture
    // (Libs/UI/Textures/Books/Recipes/<RecipeId>_ui.dds, 1024).
    inline constexpr std::uint32_t kRecipeIconSide         = 64;
    inline constexpr std::uint32_t kRecipeProductImageSide = 256;
    inline constexpr std::uint32_t kRecipeImageSide        = 1024;
    // A recipe travels in one message with its pictures, so they share one PNG allowance.
    inline constexpr std::size_t kMaximumRecipeImageBytes = 256U * 1024U;

    /** One alchemy step as authored. The condition keeps the game's own syntax, which the native book and minigame read too. */
    struct CustomAlchemyStep {
        std::uint32_t id = 0;
        // An AlchemyRecipeStepType: Trivial, Easy or Hard.
        std::string type;
        std::uint32_t feedbackPriority = 0;
        std::string text;
        std::string condition;
    };

    struct CustomRecipeMaterial {
        std::string item;
        std::uint32_t amount = 0;
    };

    struct CustomRecipeProduct {
        std::string item;
        // Alchemy: the potion grade, 1 to 4. Smithing: the minimal quality, 0 to 100.
        std::uint32_t threshold = 0;
        std::uint32_t maxYield  = 1;
    };

    /**
     * A crafting recipe the compiled catalog does not carry, field for field
     * the row of `AlchemyRecipe.xml` or `BlacksmithRecipes.xml` it stands for:
     * a streamed mod's table patch row, or a script's definition.
     */
    struct CustomRecipeDefinition {
        CraftKind kind = CraftKind::Alchemy;
        // An alchemy RecipeId, in decimal; a smithing Id.
        std::string id;
        // UIName: a table row's localization key, a script's text as written.
        std::string name;
        std::string icon;
        std::uint8_t dlc       = 0;
        std::uint32_t minSkill = 0;
        // The perk that teaches a smithing recipe; none for one every smith knows.
        std::string perk;
        std::vector<CustomRecipeMaterial> ingredients;
        std::vector<CustomRecipeProduct> products;

        // Alchemy: the BaseMaterial (Water, Wine, Oil or Spiritus), the UIUsage text and the
        // BaseQualityRewardXP.
        std::string base;
        std::string usage;
        std::uint32_t baseXP = 0;
        std::vector<CustomAlchemyStep> steps;

        // Smithing: the recipe screen's Category, the anvil's WorkpieceId and the UIDesc text.
        std::string category;
        std::string workpiece;
        std::string description;

        // A script's own pictures, each a base64 PNG as it gave it, empty for none, and its bytes:
        // the icon in place of `icon`, and for alchemy the book's product and recipe pictures.
        std::string iconImage;
        std::string productImage;
        std::string recipeImage;
        std::string iconPng;
        std::string productPng;
        std::string recipePng;
        // Alchemy: the RecipeId, in decimal, of a game recipe whose two book pictures the book
        // shows for this one, in place of productImage and recipeImage. Empty for none.
        std::string bookPicturesFrom;
    };

    /** The UIIcon a recipe with its own icon carries: content-addressed, as a custom item's IconId is. */
    std::string RecipeIconId(const CustomRecipeDefinition &definition);

    /** The XP a step type gives, from `AlchemyRecipeStepType.xml`; false for a type it does not list. */
    bool AlchemyStepTypeXP(std::string_view type, std::uint32_t &xp);

    /**
     * Compiles one condition of the game's grammar into the predicates the
     * server's matcher runs, by the rules of `tools/generate_alchemy_catalog.py`:
     * a conjunction (`AND`) of `PotionBase()`, `IngredientCount('<guid>') == n`
     * or `>= n`, `IsMilled('<guid>', 0|1)`, `WeakBoilingTime('<guid>', t)`,
     * `StrongBoilingTime('<guid>', t)`, `IsBaseDistilled(0|1)` and
     * `IsResultMilled(0|1)`. Anything else is refused rather than guessed at.
     * Each predicate's item points into `items`.
     */
    bool ParseAlchemyCondition(std::string_view condition, std::deque<std::string> &items, std::vector<AlchemyPredicate> &out, std::string &error);

    /**
     * Reads the recipe rows of a table patch: `<AlchemyRecipes>` of an
     * `AlchemyRecipe__<id>.xml`, `<BlacksmithRecipes>` of a
     * `BlacksmithRecipes__<id>.xml`. A row the reader cannot follow is left out
     * and named in `problems`; false only for a document that is not that table.
     */
    bool ParseRecipeTable(std::string_view xml, CraftKind kind, std::vector<CustomRecipeDefinition> &out, std::vector<std::string> &problems, std::string &error);

    /** Reads a script's definition; see `Crafting.registerRecipe` for the fields. */
    bool CustomRecipeFromJson(const nlohmann::json &value, CustomRecipeDefinition &out, std::string &error);

    /** A definition as a script reads it back: the shape `CustomRecipeFromJson` takes. */
    nlohmann::json CustomRecipeToJson(const CustomRecipeDefinition &definition);

    /**
     * A definition compiled to the views the crafting rules read. Built once
     * on the heap and never moved, so the views stay valid for as long as
     * anyone holds it.
     */
    class CustomRecipe final {
      public:
        /** Checks the definition the way the shipped catalog's generators check theirs; null with `error` set when it fails. */
        static std::shared_ptr<const CustomRecipe> Compile(CustomRecipeDefinition definition, std::string &error);

        CustomRecipe(const CustomRecipe &)            = delete;
        CustomRecipe &operator=(const CustomRecipe &) = delete;

        const CustomRecipeDefinition &Definition() const {
            return _definition;
        }

        const CraftRecipe &Recipe() const {
            return _recipe;
        }

        /** The steps the matcher runs; none for a smithing recipe. */
        const AlchemyProgram &Program() const {
            return _program;
        }

      private:
        CustomRecipe() = default;

        CustomRecipeDefinition _definition;
        CraftRecipe _recipe {};
        // The items the predicates name, which the views point into.
        std::deque<std::string> _predicateItems;
        std::vector<AlchemyPredicate> _predicates;
        std::vector<AlchemyStep> _steps;
        AlchemyProgram _program {};
    };
} // namespace KCDC::Shared::Game
