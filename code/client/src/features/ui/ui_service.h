#pragma once

#include "sdk/types.h"

#include <array>
#include <cstddef>
#include <mutex>
#include <string>
#include <functional>
#include <map>

namespace KCDC::Features::UI {
    /**
     * What the mod's UI keeps that is not about one piece of UI.
     *
     * The ring of strings the localization detour answers the mod's own '@'
     * keys out of, and the names the server gives the game's item name keys.
     * The native UI takes localization keys rather than text, so everything
     * the mod puts on a native screen passes through here first, and the
     * detour that resolves a key runs on whichever thread the engine asked
     * on -- which is why each table carries its own mutex rather than
     * relying on the game thread.
     *
     * A ring rather than a growing map: the engine resolves a published
     * string on the frame it is handed the key, and nothing should be holding
     * one afterwards.
     *
     * Not a session participant. It holds state and nothing ticks it, so it is
     * deliberately absent from `core/feature_registry.cpp`: an entry there
     * would add an `Update()` sweep this feature does not have. The item names
     * need no session reset either: every server sends its whole table on
     * connect, which replaces the last one's.
     */
    class UiService final {
      public:
        /** How many published labels the ring holds before it wraps. */
        static constexpr std::size_t kPublishedSlots = 64;

        /** Takes the next slot for `text`, and answers which slot that was. */
        std::size_t PublishText(std::string text);

        /**
         * Assigns what `slot` publishes to `out`.
         *
         * A slot nothing has published into yet holds the empty string, which
         * is what a key naming it resolves to.
         */
        void AssignPublishedText(std::size_t slot, SDK::CryStringT<char> &out) const;

        /** Registers the item name RPC. Call once at startup. */
        static void Install();

        /**
         * Assigns the server's name for an item name key, `@ui_nm_...` as the
         * engine asks for it. False when the server left the key to the game.
         */
        bool ResolveItemName(const char *label, SDK::CryStringT<char> &out) const;

      private:
        mutable std::mutex _publishedMutex;
        std::array<std::string, kPublishedSlots> _published;
        std::size_t _publishedCursor = 0;

        // Lowercase key without the '@', as the server sends it. Ordered with a
        // transparent comparator, so the detour looks a key up without
        // building a string for it.
        mutable std::mutex _itemNamesMutex;
        std::map<std::string, std::string, std::less<>> _itemNames;
    };
} // namespace KCDC::Features::UI
