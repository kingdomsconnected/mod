#pragma once

#include <cstdint>

namespace SDK {
    /**
     * CrySystem's system-wide event ids, as KCD2 numbers them.
     *
     * `CFlashUI::OnSystemEvent` (0x1807A2DC8) uses events 28 and 33 for
     * its loading state machine.
     */
    enum ESystemEvent : std::int32_t {
        /**
         * A Warhorse insert: the load is complete and ready to be shown.
         * Queued by the save manager's "AfterGameLoad" readiness observer
         * (0x181E1F300) once every task in mask 0x3F93 is ready, and raised
         * directly on a failed savegame load. `C_UILoadingScreen` stops the
         * loading screen on it, and outside a level switch also queues
         * gameplay start.
         */
        ESYSTEM_EVENT_LEVEL_LOADING_SCREEN_END = 21,
        /**
         * The level finished loading. Unlike stock CryEngine, KCD2's FlashUI
         * does not raise ESYSTEM_EVENT_LEVEL_GAMEPLAY_START from this one.
         */
        ESYSTEM_EVENT_LEVEL_LOAD_END = 28,

        /**
         * The loading screen went down and the player can play.
         *
         * One producer only, `wh::guimodule::RaiseLevelGameplayStart`
         * (0x181F96510), which sends this with no parameters. It is queued by
         * `C_UILoadingScreen::OnSystemEvent` on event 21 when no level switch
         * is in progress (the CSystem+0x2BE1 flag, not the editor), and on
         * 4114 at the end of one.
         */
        ESYSTEM_EVENT_LEVEL_GAMEPLAY_START = 33,

        /**
         * The level is gone. Raised last by `CLevelSystem::UnLoadLevel` (0x180A5EB80), after
         * `C3DEngine::UnloadLevel` and `CLevelInfo::CloseLevelPak`.
         * `CSystemEventListener_Animation::OnSystemEvent` (0x1813501B8) deletes the
         * CharacterManager on it and `CSystemEventListener_Action` (0x1813C0878) unloads every
         * Mannequin database, so nothing the old level loaded is still referenced.
         */
        ESYSTEM_EVENT_LEVEL_POST_UNLOAD = 35,

        /**
         * The level switch ended (0x180FC3C08), behind
         * `C_Game::OnLevelSwitchLoaded`'s 0x3F93 readiness wait.
         * `C_UILoadingScreen` posts gameplay start on it.
         */
        ESYSTEM_EVENT_LEVEL_SWITCH_END = 4114,
    };
} // namespace SDK
