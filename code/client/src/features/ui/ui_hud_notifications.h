#pragma once

#include "shared/item_stack.h"

#include <string>

namespace KCDC::Features::UI {
    /**
     * The game's own HUD notification slots.
     *
     * Nothing here is replicated: the HUD draws on this client alone, so a
     * message everyone should see is sent to everyone and shown by each.
     *
     * The slots take localization keys rather than text, and the engine
     * '@'-prefixes every word of what it is handed before resolving it, so
     * text is published under a key here rather than at the call site. Which
     * arguments are text and which are the game's own identifiers is what the
     * parameter names say: `message` and `name` are text, `iconName`, `stat`
     * and `tutorialName` go through untouched.
     *
     * Every verb returns whether the HUD was there to take it. Both receivers
     * hang off the GUI module and do not exist until the world is loaded, so a
     * call from the main menu is a legitimate miss.
     *
     * Game thread only.
     */
    class Hud final {
      public:
        static bool ShowNotification(const std::string &message);

        /** Native item icon, localized name and quantity; positive means received. */
        static bool ShowItemTransfer(const KCDC::Shared::RPC::ItemClassId &itemClass, int amount);

        /** The line above the HUD, held for `durationMs`. `priority` decides what an already-showing message does to this one. */
        static bool ShowInfoText(const std::string &message, int durationMs, int priority);
        static bool HideInfoText();

        static bool ShowPerkUsed(const std::string &iconName, const std::string &name);
        static bool ShowPerkGained(const std::string &iconName, const std::string &name);
        static bool ShowComboLearned(const std::string &iconName, const std::string &name);

        static bool ShowReputationChanged(int eventId, const std::string &message);
        static bool ShowRandomEventResult(int type, const std::string &result, const std::string &name);
        static bool ShowXpGain(const std::string &stat, const std::string &statName, int type);

        /** Drives the HUD slot directly, which is past whatever the tutorial hook suppresses at the source. */
        static bool ShowTutorial(const std::string &tutorialName);
        static bool HideTutorial(const std::string &tutorialName);
        static bool HideCurrentTutorial();

        static bool HideCodexActionHint();

        /** Empties the queue the ActionScript side holds, which is the only state the HUD exposes over it. */
        static bool ClearNotificationQueue();
    };
} // namespace KCDC::Features::UI
