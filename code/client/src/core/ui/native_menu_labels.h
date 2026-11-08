#pragma once

#include "sdk/types.h"

#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace KCDC::Core {
    /**
     * The text of the mod's rows on the game's own menus.
     *
     * Every row native (`C_UIMenuBase::AddChoicesButton`, `AddChoiceOption`, `AddValueButton`) runs
     * its label, tooltip and option text through `wh::framework::Localize` (0x1803C235C), which does
     * not translate: it lowercases the text and prefixes '@' to every word, so Scaleform resolves
     * each word as a localization key and plain text renders as `@Voice @chat`. A row therefore
     * passes one key, and the game's `LocalizeLabel` -- detoured in ui_localization_hooks.cpp --
     * answers it from here.
     *
     * Keys are handed out per text and never retired, so a menu redrawn at any point resolves.
     */
    class NativeMenuLabels final {
      public:
        static NativeMenuLabels &Get();

        /** The key that shows `text`; the same one every time for the same text. */
        const char *KeyFor(std::string_view text);

        /**
         * The same key already marked as one (`@kcdc_menu_text_3`), for `AddChoiceOption`: it
         * hands its option text to the movie as it is, so the caller does what Localize would.
         */
        const char *MarkedKeyFor(std::string_view text);

        /** Answers a key KeyFor handed out, as LocalizeLabel receives it. False for any other label. */
        bool Resolve(const char *label, SDK::CryStringT<char> &out) const;

      private:
        mutable std::mutex _mutex;
        struct Key {
            std::string key;
            std::string marked;
        };

        const Key &Lookup(std::string_view text);

        std::unordered_map<std::string, Key> _keyByText;
        std::unordered_map<std::string, std::string> _textByKey;
    };
} // namespace KCDC::Core
