#include <utils/safe_win32.h>

#include "social_voice_settings.h"

#include "core/ui/native_menu_labels.h"

#include "sdk/cryengine/action/i_game_framework.h"
#include "sdk/cryengine/action/i_player_profile_manager.h"
#include "sdk/types.h"
#include "sdk/wh/c_wh_global_environment.h"
#include "sdk/wh/guimodule/c_gui_module.h"
#include "sdk/wh/guimodule/e_menu_page.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <utils/time.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace KCDC::Features::Social {
    namespace {
        // Profile attributes, beside the game's own in attributes.xml.
        constexpr const char *kAttrEnabled          = "kcdc_voice_enabled";
        constexpr const char *kAttrVolume           = "kcdc_voice_volume";
        constexpr const char *kAttrMode             = "kcdc_voice_mode";
        constexpr const char *kAttrThreshold        = "kcdc_voice_threshold";
        constexpr const char *kAttrNoiseSuppression = "kcdc_voice_noise_suppression";
        constexpr const char *kAttrBackground       = "kcdc_voice_background";
        constexpr const char *kAttrDevice           = "kcdc_voice_device";
        constexpr const char *kAttrTier             = "kcdc_voice_tier";

        // Row ids. The movie echoes them back through OnInteractiveValue and
        // OnInteractiveChoice; the game's own handlers ignore them, since
        // E_ButtonId::FromString knows none of them.
        constexpr const char *kRowPrefix           = "kcdc_voice_";
        constexpr const char *kRowEnabled          = "kcdc_voice_enabled";
        constexpr const char *kRowVolume           = "kcdc_voice_volume";
        constexpr const char *kRowMode             = "kcdc_voice_mode";
        constexpr const char *kRowThreshold        = "kcdc_voice_threshold";
        constexpr const char *kRowMeter            = "kcdc_voice_meter";
        constexpr const char *kRowDevice           = "kcdc_voice_device";
        constexpr const char *kRowNoiseSuppression = "kcdc_voice_noise";
        constexpr const char *kRowBackground       = "kcdc_voice_background";

        // The volume slider runs 0..1 like the game's own and maps to 0..2 of
        // gain, so the middle is unity and a quiet talker can still be raised.
        constexpr float kVolumeSliderGain = 2.0f;

        // The activation slider and the meter share one dB scale, so the bar
        // crossing the threshold's position is exactly when the gate opens.
        constexpr float kSliderFloorDb   = -60.0f;
        constexpr float kSliderCeilingDb = -10.0f;

        // How long a change waits for the next before the profile is written.
        constexpr std::int64_t kSaveDelayMs = 1500;

        // The meter's refresh; the envelope under it moves no faster.
        constexpr std::int64_t kMeterIntervalMs = 50;

        bool IsRow(const char *id, const char *row) {
            return std::strcmp(id, row) == 0;
        }

        SDK::wh::guimodule::C_UIMenu *GetMenu() {
            auto *environment = SDK::wh::C_GlobalEnvironment::GetInstance();
            auto *gui         = environment != nullptr ? environment->GetGUIModule() : nullptr;
            return gui != nullptr ? gui->GetMenu() : nullptr;
        }

        SDK::IPlayerProfileManager *GetProfileManager() {
            SDK::IGameFramework *framework = SDK::IGameFramework::GetInstance();
            return framework != nullptr ? framework->GetIPlayerProfileManager() : nullptr;
        }
    } // namespace

    float VoiceSettings::LevelToSlider(float level) {
        if (!(level > 0.0f)) {
            return 0.0f;
        }
        const float db = 20.0f * std::log10(level);
        return std::clamp((db - kSliderFloorDb) / (kSliderCeilingDb - kSliderFloorDb), 0.0f, 1.0f);
    }

    float VoiceSettings::SliderToLevel(float slider) {
        const float db = kSliderFloorDb + std::clamp(slider, 0.0f, 1.0f) * (kSliderCeilingDb - kSliderFloorDb);
        return std::pow(10.0f, db / 20.0f);
    }

    void VoiceSettings::Update() {
        EnsureListening();

        const std::int64_t nowMs = Framework::Utils::Time::GetTime();
        if (_dirty && nowMs - _changedAtMs >= kSaveDelayMs) {
            SaveNow();
        }

        UpdateMeter();
    }

    void VoiceSettings::EnsureListening() {
        if (_listening || Framework::CoreModules::GetVoiceClient() == nullptr) {
            return;
        }

        SDK::IPlayerProfileManager *manager = GetProfileManager();
        if (manager == nullptr) {
            return;
        }

        // updateNow: a profile already loaded is handed over at once, so
        // registering late still starts from what was saved.
        manager->AddListener(this, true);
        _listening = true;
    }

    void VoiceSettings::Shutdown() {
        if (!_listening) {
            return;
        }

        if (SDK::IPlayerProfileManager *manager = GetProfileManager()) {
            manager->RemoveListener(this);
        }
        _listening = false;
    }

    void VoiceSettings::LoadFromProfile(SDK::IPlayerProfile *profile, bool, unsigned int) {
        if (profile == nullptr) {
            return;
        }

        // An absent attribute leaves the default: a profile that predates
        // these settings has none of them.
        int enabled = _enabled ? 1 : 0;
        profile->GetIntAttribute(kAttrEnabled, enabled);
        _enabled = enabled != 0;

        profile->GetFloatAttribute(kAttrVolume, _volume);

        int mode = static_cast<int>(_mode);
        profile->GetIntAttribute(kAttrMode, mode);
        _mode = mode == static_cast<int>(Framework::Voice::TransmitMode::VoiceActivity) ? Framework::Voice::TransmitMode::VoiceActivity : Framework::Voice::TransmitMode::PushToTalk;

        profile->GetFloatAttribute(kAttrThreshold, _threshold);

        int noiseSuppression = _noiseSuppression ? 1 : 0;
        profile->GetIntAttribute(kAttrNoiseSuppression, noiseSuppression);
        _noiseSuppression = noiseSuppression != 0;

        int background = _background ? 1 : 0;
        profile->GetIntAttribute(kAttrBackground, background);
        _background = background != 0;

        SDK::ScopedCryString device("");
        if (profile->GetStringAttribute(kAttrDevice, device)) {
            _device = device.c_str();
        }

        // A value out of range is a hand-edited profile, and reads as Normal.
        int tier = static_cast<int>(_tier);
        profile->GetIntAttribute(kAttrTier, tier);
        _tier = tier >= 0 && tier < static_cast<int>(Framework::Voice::VoiceTier::Count) ? static_cast<Framework::Voice::VoiceTier>(tier) : Framework::Voice::VoiceTier::Normal;

        Apply();
    }

    void VoiceSettings::SaveToProfile(SDK::IPlayerProfile *profile, bool, unsigned int) {
        if (profile == nullptr) {
            return;
        }

        profile->SetIntAttribute(kAttrEnabled, _enabled ? 1 : 0);
        profile->SetFloatAttribute(kAttrVolume, _volume);
        profile->SetIntAttribute(kAttrMode, static_cast<int>(_mode));
        profile->SetFloatAttribute(kAttrThreshold, _threshold);
        profile->SetIntAttribute(kAttrNoiseSuppression, _noiseSuppression ? 1 : 0);
        profile->SetIntAttribute(kAttrBackground, _background ? 1 : 0);
        profile->SetStringAttribute(kAttrDevice, _device.c_str());
        profile->SetIntAttribute(kAttrTier, static_cast<int>(_tier));

        // Whoever triggered the save wrote these too.
        _dirty = false;
    }

    void VoiceSettings::Apply() const {
        Framework::Voice::VoiceClient *voice = Framework::CoreModules::GetVoiceClient();
        if (voice == nullptr) {
            return;
        }

        voice->SetEnabled(_enabled);
        voice->SetMasterVolume(_volume);
        voice->SetTransmitMode(_mode);
        voice->SetVoiceActivationThreshold(_threshold);
        voice->SetNoiseSuppression(_noiseSuppression);
        voice->SetCaptureDevice(_device);
        voice->SetTier(_tier);
    }

    void VoiceSettings::SetTier(Framework::Voice::VoiceTier tier) {
        if (tier == _tier) {
            return;
        }

        _tier = tier;
        Changed();
    }

    void VoiceSettings::Changed() {
        _dirty       = true;
        _changedAtMs = Framework::Utils::Time::GetTime();
    }

    void VoiceSettings::SaveNow() {
        SDK::IPlayerProfileManager *manager = GetProfileManager();
        const char *user                    = manager != nullptr ? manager->GetCurrentUser() : nullptr;

        // No user logged in yet: kept dirty, tried again next tick.
        if (user == nullptr || user[0] == '\0') {
            return;
        }

        // The game's own options save. It broadcasts SaveToProfile, which is
        // where these values are written, then writes the file.
        SDK::IPlayerProfileManager::EProfileOperationResult result = SDK::IPlayerProfileManager::ePOR_Success;
        if (!manager->SaveProfile(user, result, SDK::IPlayerProfileManager::ePR_Options)) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("Voice: the profile would not save ({}); the settings apply until the game closes", static_cast<int>(result));
        }
        _dirty = false;
    }

    void VoiceSettings::OnShowPage(SDK::wh::guimodule::C_UIMenu &menu) {
        if (menu.GetPage() != SDK::wh::guimodule::E_MenuPage::Type::SoundSettings) {
            _soundPageArmed = false;
            _rowsShown      = false;
            return;
        }

        if (!_soundPageArmed) {
            return;
        }

        _soundPageArmed = false;
        AppendRows(*menu.GetSettings());
        _rowsShown = true;
    }

    void VoiceSettings::AddToggle(SDK::wh::guimodule::C_UISettings &page, const char *id, const char *label, const char *tooltip, bool on) {
        const SDK::ScopedCryString rowId(id);
        const SDK::ScopedCryString rowLabel(KCDC::Core::NativeMenuLabels::Get().KeyFor(label));
        const SDK::ScopedCryString off(KCDC::Core::NativeMenuLabels::Get().MarkedKeyFor("Off"));
        const SDK::ScopedCryString onLabel(KCDC::Core::NativeMenuLabels::Get().MarkedKeyFor("On"));
        const SDK::ScopedCryString noTooltip("");

        page.AddChoicesButton(rowId, rowLabel, true, KCDC::Core::NativeMenuLabels::Get().KeyFor(tooltip));
        page.AddChoiceOption(rowId, 0, off, true, noTooltip);
        page.AddChoiceOption(rowId, 1, onLabel, true, noTooltip);
        page.SetChoice(rowId, on ? 1 : 0);
    }

    void VoiceSettings::AppendRows(SDK::wh::guimodule::C_UISettings &page) {
        Framework::Voice::VoiceClient *voice = Framework::CoreModules::GetVoiceClient();
        if (voice == nullptr) {
            return;
        }

        const SDK::ScopedCryString noTooltip("");

        // Drawn from the client rather than from the stored copy: a script
        // may have changed any of these since the profile was read.
        AddToggle(page, kRowEnabled, "Voice chat", "Turns proximity voice chat off for this client: nothing is sent, nothing is heard.", voice->IsEnabled());

        {
            const SDK::ScopedCryString id(kRowVolume);
            const SDK::ScopedCryString label(KCDC::Core::NativeMenuLabels::Get().KeyFor("Voice chat volume"));
            const float slider = std::clamp(voice->GetMasterVolume() / kVolumeSliderGain, 0.0f, 1.0f);
            page.AddValueButton(id, label, true, slider, 0.0f, 1.0f, KCDC::Core::NativeMenuLabels::Get().KeyFor("How loud other players are. The middle is their own level; above it raises quiet speakers."));
            page.SetValue(id, slider);
        }

        {
            const SDK::ScopedCryString id(kRowMode);
            const SDK::ScopedCryString label(KCDC::Core::NativeMenuLabels::Get().KeyFor("Voice transmission"));
            const SDK::ScopedCryString pushToTalk(KCDC::Core::NativeMenuLabels::Get().MarkedKeyFor("Push to talk"));
            const SDK::ScopedCryString voiceActivated(KCDC::Core::NativeMenuLabels::Get().MarkedKeyFor("Voice activated"));
            page.AddChoicesButton(id, label, true, KCDC::Core::NativeMenuLabels::Get().KeyFor("Push to talk sends while the key is held (Controls > Keybinds > Multiplayer). Voice activated sends whenever you speak louder than the activation level."));
            page.AddChoiceOption(id, 0, pushToTalk, true, noTooltip);
            page.AddChoiceOption(id, 1, voiceActivated, true, noTooltip);
            page.SetChoice(id, voice->GetTransmitMode() == Framework::Voice::TransmitMode::VoiceActivity ? 1 : 0);
        }

        {
            const SDK::ScopedCryString id(kRowThreshold);
            const SDK::ScopedCryString label(KCDC::Core::NativeMenuLabels::Get().KeyFor("Voice activation level"));
            const float slider = LevelToSlider(voice->GetVoiceActivationThreshold());
            page.AddValueButton(id, label, true, slider, 0.0f, 1.0f, KCDC::Core::NativeMenuLabels::Get().KeyFor("How loud you must be for voice activation to send. Speak normally and set it just below where the microphone level reaches."));
            page.SetValue(id, slider);
        }

        {
            const SDK::ScopedCryString id(kRowMeter);
            const SDK::ScopedCryString label(KCDC::Core::NativeMenuLabels::Get().KeyFor("Microphone level"));
            page.AddValueButton(id, label, false, 0.0f, 0.0f, 1.0f, KCDC::Core::NativeMenuLabels::Get().KeyFor("What the microphone hears right now, on the same scale as the activation level."));
        }

        {
            const SDK::ScopedCryString id(kRowDevice);
            const SDK::ScopedCryString label(KCDC::Core::NativeMenuLabels::Get().KeyFor("Microphone"));
            const SDK::ScopedCryString systemDefault(KCDC::Core::NativeMenuLabels::Get().MarkedKeyFor("System default"));
            page.AddChoicesButton(id, label, true, KCDC::Core::NativeMenuLabels::Get().KeyFor("The device voice records from."));
            page.AddChoiceOption(id, 0, systemDefault, true, noTooltip);

            // Index 0 is the default; the list is kept so a choice maps back
            // to the name it was drawn with even if devices change meanwhile.
            _deviceChoices            = voice->ListCaptureDevices();
            const std::string current = voice->GetCaptureDevice();
            int selected              = 0;
            for (std::size_t i = 0; i < _deviceChoices.size(); i++) {
                const SDK::ScopedCryString option(KCDC::Core::NativeMenuLabels::Get().MarkedKeyFor(_deviceChoices[i]));
                page.AddChoiceOption(id, static_cast<int>(i + 1), option, true, noTooltip);
                if (_deviceChoices[i] == current) {
                    selected = static_cast<int>(i + 1);
                }
            }
            page.SetChoice(id, selected);
        }

        AddToggle(page, kRowNoiseSuppression, "Noise suppression", "Filters steady background noise -- fans, hum, keyboard -- out of your microphone before it is sent.", voice->IsNoiseSuppressionEnabled());
        AddToggle(page, kRowBackground, "Voice chat in background", "Keeps other players audible while the game window is not focused, when the game mutes its own sound.", _background);
    }

    void VoiceSettings::UpdateMeter() {
        if (!_rowsShown) {
            return;
        }

        SDK::wh::guimodule::C_UIMenu *menu = GetMenu();
        if (menu == nullptr || menu->GetPage() != SDK::wh::guimodule::E_MenuPage::Type::SoundSettings) {
            _rowsShown = false;
            return;
        }

        const std::int64_t nowMs = Framework::Utils::Time::GetTime();
        if (nowMs - _meterAtMs < kMeterIntervalMs) {
            return;
        }
        _meterAtMs = nowMs;

        Framework::Voice::VoiceClient *voice = Framework::CoreModules::GetVoiceClient();
        const SDK::ScopedCryString id(kRowMeter);
        menu->GetSettings()->SetValue(id, voice != nullptr ? LevelToSlider(voice->GetInputLevel()) : 0.0f);
    }

    bool VoiceSettings::OnValue(const char *id, float value) {
        if (id == nullptr || std::strncmp(id, kRowPrefix, std::strlen(kRowPrefix)) != 0) {
            return false;
        }

        Framework::Voice::VoiceClient *voice = Framework::CoreModules::GetVoiceClient();
        if (voice == nullptr) {
            return true;
        }

        if (IsRow(id, kRowVolume)) {
            _volume = std::clamp(value, 0.0f, 1.0f) * kVolumeSliderGain;
            voice->SetMasterVolume(_volume);
            Changed();
        }
        else if (IsRow(id, kRowThreshold)) {
            _threshold = SliderToLevel(value);
            voice->SetVoiceActivationThreshold(_threshold);
            Changed();
        }
        return true;
    }

    bool VoiceSettings::OnChoice(const char *id, unsigned int index) {
        if (id == nullptr || std::strncmp(id, kRowPrefix, std::strlen(kRowPrefix)) != 0) {
            return false;
        }

        Framework::Voice::VoiceClient *voice = Framework::CoreModules::GetVoiceClient();
        if (voice == nullptr) {
            return true;
        }

        const bool on = index != 0;
        if (IsRow(id, kRowEnabled)) {
            _enabled = on;
            voice->SetEnabled(_enabled);
        }
        else if (IsRow(id, kRowMode)) {
            _mode = on ? Framework::Voice::TransmitMode::VoiceActivity : Framework::Voice::TransmitMode::PushToTalk;
            voice->SetTransmitMode(_mode);
        }
        else if (IsRow(id, kRowDevice)) {
            _device = index > 0 && index <= _deviceChoices.size() ? _deviceChoices[index - 1] : std::string {};
            voice->SetCaptureDevice(_device);
        }
        else if (IsRow(id, kRowNoiseSuppression)) {
            _noiseSuppression = on;
            voice->SetNoiseSuppression(_noiseSuppression);
        }
        else if (IsRow(id, kRowBackground)) {
            _background = on;
        }
        else {
            return true;
        }

        Changed();
        return true;
    }
} // namespace KCDC::Features::Social
