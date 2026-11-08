#pragma once

#include "core/feature_registry.h"
#include "sdk/types.h"
#include "shared/game/custom_recipes.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace KCDC::Features::Crafting {
    /**
     * The recipes scripts register on the server (`Crafting.registerRecipe`),
     * put into the game's own recipe tables, so its recipe book, smithy and
     * minigames list and run them as they do the shipped ones. A streamed
     * mod's recipe tables need none of this: the game's loader reads them.
     *
     * Rows are appended from this module's tick, followed at once by the
     * table's re-sort, never from inside a native: nothing in the game keeps a
     * row across frames, only its id (see `C_AlchemyRecipeDatabase`). No row
     * is ever removed. The tables keep a session's rows until they are read
     * again, which drops every appended row: at a change of table patches,
     * when this module appends this session's again, and at the level edge
     * of the next join, which this module asks for when the session before
     * left rows behind.
     *
     * The game shows a recipe's texts through localization keys; a script's
     * texts go out under keys this module answers (`@kcdc_rc_<n>_<field>`).
     */
    class CustomRecipeModule final: public KCDC::Core::IFeatureService {
      public:
        static CustomRecipeModule &Get();
        static void Install();

        void Update() override;
        void Reset() override;

        /** The recipe tables were read again: none of the rows appended survived. */
        void OnRecipeTablesReloaded();
        /** An ended session's rows are still in the tables, for the next level edge to drop. */
        bool WantsReload() const;

        bool ResolveLabel(const char *label, SDK::CryStringT<char> &out) const;
        /** The RecipeId whose book pictures a recipe's page shows: the one it borrows, else its own. Any thread. */
        std::string BookPicturesOf(std::string_view recipeId) const;
        static void InstallHooks();

      private:
        void Receive(const std::string &body);
        /** Appends the `index`th definition's row; false when its table refused it. */
        bool Append(const KCDC::Shared::Game::CustomRecipeDefinition &definition, std::size_t index);
        /** A text's key for the row: empty for no text. Records the text for `ResolveLabel`. */
        std::string Label(std::size_t index, const char *field, const std::string &text);
        /** Puts a definition's picture where the UI loads `path` from, fitted onto a square of `side`. */
        void ServePicture(const KCDC::Shared::Game::CustomRecipeDefinition &definition, const std::string &png, const std::string &path, std::uint32_t side);

        mutable std::mutex _mutex;
        // Bodies off the network thread, read on the next tick.
        std::vector<std::string> _incoming;
        std::map<std::string, std::string, std::less<>> _labels;
        // By RecipeId, the game recipe whose book pictures it shows.
        std::map<std::string, std::string, std::less<>> _bookPictures;
        // The UI paths this session's pictures are served at, withdrawn when it ends. Game thread.
        std::set<std::string> _pictures;

        // This session's recipes, in the server's order, and how many of them are in the tables.
        std::vector<KCDC::Shared::Game::CustomRecipeDefinition> _definitions;
        std::size_t _appended = 0;
        // Rows this process appended are in the tables now; they are an ended session's, which the
        // next level edge's reload drops.
        bool _rowsInTables = false;
        bool _stale        = false;
    };
} // namespace KCDC::Features::Crafting
