#include <utils/safe_win32.h>

#include "streamed_asset_settings.h"

#include "core/application.h"
#include "core/ui/native_menu_labels.h"

#include "sdk/cryengine/action/i_game_framework.h"
#include "sdk/cryengine/action/i_player_profile_manager.h"
#include "sdk/types.h"
#include "sdk/wh/guimodule/c_ui_settings.h"
#include "sdk/wh/guimodule/e_menu_page.h"

#include <logging/logger.h>
#include <utils/time.h>

#include <array>
#include <cstring>

namespace KCDC::Core::StreamedAssets {
    namespace {
        constexpr const char *kAttrConsent = "kcdc_assets_consent";

        // Row ids, echoed back through OnInteractiveChoice; the game's own handler ignores them.
        constexpr const char *kRowPrefix  = "kcdc_assets_";
        constexpr const char *kRowConsent = "kcdc_assets_consent";

        constexpr std::uint64_t kMiB = 1024ull * 1024ull;
        constexpr std::uint64_t kGiB = 1024ull * kMiB;

        struct Option {
            const char *label;
            std::uint64_t bytes;
        };

        // What a server may download unasked. "Always ask" is any download at all.
        constexpr std::array<Option, 5> kConsentOptions = {{
            {"Always ask", 0},
            {"Above 100 MB", 100 * kMiB},
            {"Above 500 MB", 500 * kMiB},
            {"Above 2 GB", 2 * kGiB},
            {"Never ask", ~0ull},
        }};


        constexpr int kDefaultConsent = 2;

        // How long a change waits for the next before the profile is written.
        constexpr std::int64_t kSaveDelayMs = 1500;

        template <std::size_t N>
        int Clamped(int index, int fallback) {
            return index >= 0 && index < static_cast<int>(N) ? index : fallback;
        }

        SDK::IPlayerProfileManager *GetProfileManager() {
            SDK::IGameFramework *framework = SDK::IGameFramework::GetInstance();
            return framework != nullptr ? framework->GetIPlayerProfileManager() : nullptr;
        }

        template <std::size_t N>
        void AddChoices(SDK::wh::guimodule::C_UISettings &page, const char *id, const char *label, const char *tooltip, const std::array<Option, N> &options, int selected) {
            const SDK::ScopedCryString rowId(id);
            const SDK::ScopedCryString rowLabel(KCDC::Core::NativeMenuLabels::Get().KeyFor(label));
            const SDK::ScopedCryString noTooltip("");
            page.AddChoicesButton(rowId, rowLabel, true, KCDC::Core::NativeMenuLabels::Get().KeyFor(tooltip));
            for (std::size_t i = 0; i < N; ++i) {
                const SDK::ScopedCryString option(KCDC::Core::NativeMenuLabels::Get().MarkedKeyFor(options[i].label));
                page.AddChoiceOption(rowId, static_cast<int>(i), option, true, noTooltip);
            }
            page.SetChoice(rowId, selected);
        }
    } // namespace

    std::uint64_t StreamedAssetSettings::DefaultConsentThreshold() {
        return kConsentOptions[kDefaultConsent].bytes;
    }

    void StreamedAssetSettings::Update() {
        if (!_listening) {
            if (SDK::IPlayerProfileManager *manager = GetProfileManager()) {
                // updateNow: a profile already loaded is handed over at once.
                manager->AddListener(this, true);
                _listening = true;
            }
        }
        if (_dirty && Framework::Utils::Time::GetTime() - _changedAtMs >= kSaveDelayMs) {
            SaveNow();
        }
    }

    void StreamedAssetSettings::Shutdown() {
        if (!_listening) {
            return;
        }
        if (SDK::IPlayerProfileManager *manager = GetProfileManager()) {
            manager->RemoveListener(this);
        }
        _listening = false;
    }

    void StreamedAssetSettings::LoadFromProfile(SDK::IPlayerProfile *profile, bool, unsigned int) {
        if (profile == nullptr) {
            return;
        }
        // An absent attribute leaves the default: a profile that predates these settings has none.
        int consent = _consent;
        profile->GetIntAttribute(kAttrConsent, consent);
        _consent  = Clamped<kConsentOptions.size()>(consent, kDefaultConsent);
        Apply();
    }

    void StreamedAssetSettings::SaveToProfile(SDK::IPlayerProfile *profile, bool, unsigned int) {
        if (profile == nullptr) {
            return;
        }
        profile->SetIntAttribute(kAttrConsent, _consent);
        // Whoever triggered the save wrote these too.
        _dirty = false;
    }

    void StreamedAssetSettings::Apply() const {
        KCDC::Core::gApplication->SetAssetConsentThreshold(kConsentOptions[_consent].bytes);
    }

    void StreamedAssetSettings::SaveNow() {
        SDK::IPlayerProfileManager *manager = GetProfileManager();
        const char *user                    = manager != nullptr ? manager->GetCurrentUser() : nullptr;
        // No user logged in yet: kept dirty, tried again next tick.
        if (user == nullptr || user[0] == '\0') {
            return;
        }
        SDK::IPlayerProfileManager::EProfileOperationResult result = SDK::IPlayerProfileManager::ePOR_Success;
        if (!manager->SaveProfile(user, result, SDK::IPlayerProfileManager::ePR_Options)) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("Streamed assets: the profile would not save ({}); the settings apply until the game closes", static_cast<int>(result));
        }
        _dirty = false;
    }

    void StreamedAssetSettings::OnShowPage(SDK::wh::guimodule::C_UIMenu &menu) {
        if (menu.GetPage() != SDK::wh::guimodule::E_MenuPage::Type::GameSettings) {
            _gamePageArmed = false;
            return;
        }
        if (!_gamePageArmed) {
            return;
        }
        _gamePageArmed                         = false;
        SDK::wh::guimodule::C_UISettings &page = *menu.GetSettings();
        AddChoices(page, kRowConsent, "Ask before server downloads", "Servers can stream their own models, sounds and textures. A download larger than this waits on the joining screen for you to accept it.", kConsentOptions, _consent);
    }

    bool StreamedAssetSettings::OnChoice(const char *id, unsigned int index) {
        if (id == nullptr || std::strncmp(id, kRowPrefix, std::strlen(kRowPrefix)) != 0) {
            return false;
        }
        if (std::strcmp(id, kRowConsent) == 0) {
            _consent = Clamped<kConsentOptions.size()>(static_cast<int>(index), _consent);
        }
        else {
            return true;
        }
        Apply();
        _dirty       = true;
        _changedAtMs = Framework::Utils::Time::GetTime();
        return true;
    }
} // namespace KCDC::Core::StreamedAssets
