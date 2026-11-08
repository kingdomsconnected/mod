#pragma once

#include "sdk/cryengine/action/i_player_profile.h"
#include "sdk/cryengine/action/i_player_profile_listener.h"
#include "sdk/wh/guimodule/c_ui_menu.h"

#include <cstdint>

namespace KCDC::Core::StreamedAssets {
    /**
     * How much of a server's asset paks may download before the player is asked, stored where the
     * game keeps its own settings.
     *
     * A profile attribute in `attributes.xml`, read and written through the IPlayerProfileListener
     * seam the game's own settings use, and a row on the game's Game settings page, emitted by the
     * natives that page emits its own rows with. It pushes into the Framework client's asset consent
     * threshold, which stays the one that acts on it.
     *
     * Game thread only.
     */
    class StreamedAssetSettings final: public SDK::IPlayerProfileListener {
      public:
        /** The threshold before the profile is read: ask above 500 MB. */
        static std::uint64_t DefaultConsentThreshold();

        /** Registers with the profile manager once it exists, and saves a change once it settles. */
        void Update();

        /** Leaves the profile manager; the game saves the profile on the way out. */
        void Shutdown();

        void SaveToProfile(SDK::IPlayerProfile *profile, bool online, unsigned int reason) override;
        void LoadFromProfile(SDK::IPlayerProfile *profile, bool online, unsigned int reason) override;

        /** The Game page is being built; the rows go in when it is shown. */
        void ArmGamePage() {
            _gamePageArmed = true;
        }

        /** Called from C_UIMenu::ShowPage: appends the rows to a Game page built this time round. */
        void OnShowPage(SDK::wh::guimodule::C_UIMenu &menu);

        /** A choice changed. False for an id that is not one of these rows. */
        bool OnChoice(const char *id, unsigned int index);

      private:
        void Apply() const;
        void SaveNow();

        // Index into the row's option table. A value out of range is a hand-edited profile and
        // reads as the default.
        int _consent = 2;

        bool _listening           = false;
        bool _dirty               = false;
        std::int64_t _changedAtMs = 0;
        bool _gamePageArmed       = false;
    };
} // namespace KCDC::Core::StreamedAssets
