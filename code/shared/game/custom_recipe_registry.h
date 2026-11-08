#pragma once

#include "custom_recipes.h"

#include <function2/function2.hpp>

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace KCDC::Shared::Game {
    /** Where a recipe comes from, as `Crafting.recipes` reports it. */
    enum class RecipeSource : std::uint8_t {
        Game,
        Mod,
        Script
    };

    /**
     * Every crafting recipe the session knows: the compiled catalog, the rows
     * streamed mods' table patches add, and the recipes scripts register.
     *
     * A mod's row that names a shipped recipe replaces it, as the game's own
     * loader merges a patch row by row. Any other id is a new recipe. Between
     * two mods, the resource whose name sorts first keeps an id, as it does
     * an item class; a script's recipe keeps its id against any mod, and is
     * refused one a mod or the game already uses. A script's recipe is fixed
     * for the session; the same definition again is accepted, a different one
     * is not. A mod's rows leave with its resource.
     *
     * Every recipe's items must be classes the item catalog knows when it is
     * added, and a smithing product one the native outcome can make. A
     * recipe that leaves stays allocated, so a pointer taken from `Find` holds
     * for the session.
     *
     * Main thread only.
     */
    class CustomRecipeRegistry final {
      public:
        static CustomRecipeRegistry &Get();

        /** Replaces what a mod's tables bring; the reason each row left out was refused. */
        std::vector<std::string> ReplaceTables(const std::string &source, std::vector<std::shared_ptr<const CustomRecipe>> recipes);
        void RemoveTables(const std::string &source);
        /** Adds a script's recipe; false with `error` set for a conflict or an unknown item. */
        bool Register(std::shared_ptr<const CustomRecipe> recipe, std::string &error);
        void Reset();

        /** A recipe of any source; a mod's replacement over the shipped row. */
        const CraftRecipe *Find(CraftKind kind, std::string_view id) const;
        /** Every recipe, shipped ones first, each with where it comes from. */
        void ForEach(const fu2::function<void(const CraftRecipe &, RecipeSource) const> &visit) const;
        /**
         * The alchemy programs in the order the game's matcher tries them, by
         * RecipeId, a mod's replacement in the place of the row it replaces.
         * `visit` returns true to stop.
         */
        void ForEachProgram(const fu2::function<bool(const AlchemyProgram &, const CraftRecipe &) const> &visit) const;
        /** The scripts' recipes, in the order they were registered. */
        const std::vector<std::shared_ptr<const CustomRecipe>> &Registered() const;
        /** Advances on every change. */
        std::uint64_t Revision() const {
            return _revision;
        }

        /** How smithing settles a material: the compiled rule, else one derived from the item catalog. */
        std::optional<SmithingMaterialRule> MaterialRule(std::string_view item) const;
        /** How the native outcome treats a product: the compiled class, else one derived from the item catalog. */
        std::optional<SmithingProductClass> ProductClass(std::string_view item) const;

      private:
        struct Entry {
            std::shared_ptr<const CustomRecipe> recipe;
            // The mod's resource, or empty for a script's.
            std::string source;
            // Its id names a shipped recipe, which it replaces.
            bool replaces = false;
        };

        const Entry *FindEntry(CraftKind kind, std::string_view id) const;
        bool Admit(const CustomRecipe &recipe, bool script, std::string &error) const;
        /** Settles `_entries` from the sources: mods by name, then scripts. The reasons `source`'s rows were refused. */
        std::vector<std::string> Rebuild(std::string_view source);
        void Retire(std::vector<std::shared_ptr<const CustomRecipe>> &recipes);

        // What each mod's tables bring, by resource name, and what scripts registered, in order.
        std::map<std::string, std::vector<std::shared_ptr<const CustomRecipe>>, std::less<>> _tables;
        std::vector<std::shared_ptr<const CustomRecipe>> _scripts;
        // The recipes in effect.
        std::vector<Entry> _entries;
        // Recipes that left: kept so the views anyone still holds stay valid.
        std::vector<std::shared_ptr<const CustomRecipe>> _retired;
        std::uint64_t _revision = 0;
    };
} // namespace KCDC::Shared::Game
