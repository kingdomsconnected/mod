#pragma once

#include "sdk/cryengine/action/i_player_profile.h"
#include "sdk/cryengine/action/i_player_profile_listener.h"
#include "sdk/wh/guimodule/c_ui_menu.h"

#include <voice/client/voice_client.h>
#include <voice/voice_config.h>

#include <cstdint>
#include <string>
#include <vector>

namespace KCDC::Features::Social {
    /**
     * The player's voice settings, kept where the game keeps its own.
     *
     * The values live in the player profile -- `attributes.xml`, beside
     * `sound_master` and `keybinds_settings` -- through the same
     * IPlayerProfileListener seam C_UISettings and C_Keybinds use, so they
     * are loaded when the game loads its settings and written when it
     * writes them. The rows live on the game's own Sound settings page,
     * emitted by the natives the page emits its volume sliders with, and
     * push-to-talk is a native keybind under the controls menu's
     * Multiplayer group. Nothing of this is a screen or a file of the mod's.
     *
     * The framework's VoiceClient stays the one source of truth while the
     * game runs: this class holds only what the profile holds, pushes it into
     * the client, and redraws the rows from the client -- so a script that
     * changed a setting is what the page shows.
     *
     * Game thread only.
     */
    class VoiceSettings final: public SDK::IPlayerProfileListener {
      public:
        /**
         * Registers with the profile manager once it exists, saves a change
         * once the player stops moving the slider, and animates the level
         * meter while the Sound page is up.
         */
        void Update();

        /**
         * Leaves the profile manager. Must run before the mod unloads: the
         * game saves the profile on the way out, and a listener it still
         * holds would be called through a vtable that is gone.
         */
        void Shutdown();

        /** Pushes the stored values into the voice client, again. For a source or sink just installed. */
        void Apply() const;

        /**
         * The voice tier the player switched to, kept for the next session.
         * Only the player's own choice lands here: a tier the server imposes
         * is not a preference.
         */
        void SetTier(Framework::Voice::VoiceTier tier);

        /** Whether voice stays audible while the game has muffled itself for losing focus. */
        bool KeepsVoiceInBackground() const {
            return _background;
        }

        // --- SDK::IPlayerProfileListener ------------------------------------

        void SaveToProfile(SDK::IPlayerProfile *profile, bool online, unsigned int reason) override;
        void LoadFromProfile(SDK::IPlayerProfile *profile, bool online, unsigned int reason) override;

        // --- The Sound settings page ------------------------------------------

        /** The Sound page is being built; the rows go in when it is shown. */
        void ArmSoundPage() {
            _soundPageArmed = true;
        }

        /**
         * Called from C_UIMenu::ShowPage, the last thing every page does.
         * Appends the voice rows when the Sound page is the one being shown
         * and was built this time round, so a re-show never doubles them.
         */
        void OnShowPage(SDK::wh::guimodule::C_UIMenu &menu);

        /** A slider moved. False for an id that is not one of these rows. */
        bool OnValue(const char *id, float value);

        /** A choice changed. False for an id that is not one of these rows. */
        bool OnChoice(const char *id, unsigned int index);

      private:
        void AppendRows(SDK::wh::guimodule::C_UISettings &page);
        void AddToggle(SDK::wh::guimodule::C_UISettings &page, const char *id, const char *label, const char *tooltip, bool on);
        void UpdateMeter();

        void Changed();
        void SaveNow();

        /** Registers once the framework and the profile manager are both up. */
        void EnsureListening();

        /** The page's 0..1 slider position for an RMS level, on the dB scale the threshold uses. */
        static float LevelToSlider(float level);
        static float SliderToLevel(float slider);

        // What the profile holds. Defaults are the framework's.
        bool _enabled                        = true;
        float _volume                        = 1.0f;
        Framework::Voice::TransmitMode _mode = Framework::Voice::TransmitMode::PushToTalk;
        float _threshold                     = Framework::Voice::kDefaultVoiceActivationThreshold;
        bool _noiseSuppression               = false;
        bool _background                     = true;
        Framework::Voice::VoiceTier _tier    = Framework::Voice::VoiceTier::Normal;
        std::string _device;

        bool _listening = false;

        // A change waits for the player to stop dragging before the profile is
        // written: a slider fires on every step.
        bool _dirty               = false;
        std::int64_t _changedAtMs = 0;

        bool _soundPageArmed    = false;
        bool _rowsShown         = false;
        std::int64_t _meterAtMs = 0;

        // The options the device row was built with, index for index.
        std::vector<std::string> _deviceChoices;
    };
} // namespace KCDC::Features::Social
