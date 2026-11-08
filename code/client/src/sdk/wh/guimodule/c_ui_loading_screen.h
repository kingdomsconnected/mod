#pragma once

#include "c_ui_flash_base.h"

#include "../../cryengine/system/i_system_event_listener.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::guimodule {
    /**
     * The loading screen, and the main-menu backdrop it plays.
     *
     * Only the C_UIFlashBase prefix, the listener and the state byte are
     * mapped. The constructor (0x18187663C) registers the ISystemEventListener
     * at +0x58 with the system event dispatcher; its OnSystemEvent
     * (0x1808AE710) is what takes the loading screen down and starts
     * gameplay.
     */
    class C_UILoadingScreen final : public C_UIFlashBase, public ISystemEventListener {
      public:
        ~C_UILoadingScreen() override = default;

        /**
         * Closes the current page and re-enters the main-menu state, which
         * restarts the looping backdrop. The game's own way back: it runs
         * this when a savegame load fails mid-session.
         *
         * A way back, not a way to stay. It stops whatever video is playing
         * before opening the next one, and the main-menu branch is the one
         * state with no loading-screen fallback when that open fails, so a
         * backdrop that is already up is left black. Ask IsMainMenuState.
         */
        void ReturnToMainMenuState();

        /** Stops playback and releases the loading filter without posting gameplay start. */
        void StopVideoOrLoadingScreen();

        /**
         * What the screen is up for, as `SetState` (0x1809AEE5C) stamps it
         * into the byte at +0x7C.
         *
         * Every value below is named for the loading context that state asks
         * the game modules for, which is the one thing `SetState` does
         * differently per state and the game's own word for it. `MainMenu`
         * asks for none: it is the only state that returns early through
         * `PlayVideoOrLoadingScreen`, and the only one with no loading-screen
         * fallback when the video fails to open.
         *
         * `SetState` also has a branch on 8, so states above these exist; they
         * select no context and have no proven meaning.
         */
        enum ELoadingScreenState : std::uint8_t {
            eLoadingScreenState_MainMenu = 1,
            /** Context "new_game". Tears the main-menu backdrop down first. */
            eLoadingScreenState_NewGame = 2,
            /** Context "level_switching_game_loading", tearing that backdrop down as NewGame does. */
            eLoadingScreenState_LevelSwitchingAndLoading = 3,
            /** Context "game_loading". */
            eLoadingScreenState_GameLoading = 4,
            /** Context "level_switching". The other state that plays a video rather than a loading screen. */
            eLoadingScreenState_LevelSwitching = 5,
            /** Context "level_switching_game_loading" again, without that teardown. */
            eLoadingScreenState_LevelSwitchingAndLoadingResumed = 6,
        };

        /**
         * True while the backdrop is the screen's state. The Flash loading
         * stop path clears this byte through ResetLoadingState (0x1809AEDC0);
         * video playback can retain it until its own completion callback.
         */
        bool IsMainMenuState() const {
            return m_state == eLoadingScreenState_MainMenu;
        }

      private:
        // Layout, not API. The C_UIFlashBase prefix occupies the first 0x58
        // and the listener's vtable pointer the next 8.
        std::byte m_padding0060[0x1C];
        ELoadingScreenState m_state; // 0x7C

        static void AssertLayout() {
            static_assert(offsetof(C_UILoadingScreen, m_state) == 0x7C, "C_UILoadingScreen state offset mismatch");
        }
    };
} // namespace SDK::wh::guimodule
