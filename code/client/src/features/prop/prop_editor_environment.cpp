#include "prop_editor_environment.h"
#include "prop_editor.h"

#include <imgui.h>

#include "core/application.h"
#include "features/world/world_environment_service.h"

#include <utils/string_utils.h>

#include <algorithm>
#include <cstdio>
#include <iterator>

namespace KCDC::Features::Prop {
    namespace {
        /** One labelled value of the hour or rain shortcut row. */
        struct EnvironmentShortcut {
            const char *label;
            float value;
        };

        constexpr EnvironmentShortcut kHourShortcuts[] = {
            {"Dawn", 5.5F},
            {"Morning", 8.0F},
            {"Noon", 12.0F},
            {"Afternoon", 15.5F},
            {"Evening", 18.0F},
            {"Dusk", 20.5F},
            {"Night", 23.5F},
        };

        constexpr EnvironmentShortcut kRainShortcuts[] = {
            {"Dry", 0.0F},
            {"Drizzle", 0.15F},
            {"Rain", 0.5F},
            {"Downpour", 1.0F},
        };

        const ImVec4 kEnvironmentWarnColour {0.9F, 0.8F, 0.4F, 1.0F};
        const ImVec4 kEnvironmentGoodColour {0.6F, 0.9F, 0.6F, 1.0F};

        // Day zero of the world clock is a Monday by this panel's convention
        // only; the game's own day of week is the same plain day % 7.
        constexpr const char *kDayNames[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};

        constexpr std::uint32_t kNoPreset = 0xFFFFFFFFu;

        KCDC::Features::World::WorldEnvironmentService *Service() {
            return Core::gApplication != nullptr ? &Core::gApplication->GetWorldEnvironmentService() : nullptr;
        }

        /** An hours-since-midnight float as HH:MM. */
        std::string FormatClock(float hours) {
            char buffer[16];
            const int totalMinutes = static_cast<int>(hours * 60.0F + 0.5F);
            std::snprintf(buffer, sizeof(buffer), "%02d:%02d", (totalMinutes / 60) % 24, totalMinutes % 60);
            return buffer;
        }

        /** A weather-profile condition range. Unused conditions are left NaN rather than cleared. */
        std::string FormatRange(const KCDC::Features::World::ConditionRange &range) {
            if (std::isnan(range.minimum) || std::isnan(range.maximum)) {
                return "-";
            }
            char buffer[48];
            std::snprintf(buffer, sizeof(buffer), "%.2f - %.2f", range.minimum, range.maximum);
            return buffer;
        }

        /**
         * A slider whose commit reaches the engine, so it is dragged against
         * a draft and committed when released. While it is idle the draft
         * follows `committed`, which is how a project open or an MCP patch
         * shows up under the cursor.
         */
        bool SliderCommit(const char *label, float &draft, float &committed, float low, float high, const char *format, ImGuiSliderFlags flags = ImGuiSliderFlags_AlwaysClamp) {
            ImGui::SliderFloat(label, &draft, low, high, format, flags);
            const bool commit = ImGui::IsItemDeactivatedAfterEdit() && draft != committed;
            if (commit) {
                committed = draft;
            }
            else if (!ImGui::IsItemActive()) {
                // Reseeded for the next frame rather than this one: only
                // after drawing does ImGui know whether this is the control
                // under the cursor.
                draft = committed;
            }
            return commit;
        }
    } // namespace

    void EditorEnvironment::Adopt(const EnvironmentPreview &preview) {
        _preview = preview.IsValid() ? preview : EnvironmentPreview {};
        // A project naming a sky expects to see it, even when that is the sky
        // this level already stands under.
        if (!_preview.preset.empty()) {
            ++_presetRequest;
        }
    }

    void EditorEnvironment::Synchronize(bool offline) {
        auto *service = Service();
        if (service == nullptr) {
            return;
        }

        const bool release = _releaseRequested.exchange(false, std::memory_order_acq_rel);
        if (release || !offline) {
            Release();
            return;
        }

        if (!_profilesRequested) {
            _profilesRequested = true;
            service->QueueRefreshProfiles();
        }

        Apply();
    }

    void EditorEnvironment::Apply() {
        auto *service = Service();
        if (service == nullptr || (_everSent && _sent == _preview && _presetSent == _presetRequest)) {
            return;
        }

        // The sky first, then the preset, then rain, and the hold last: the
        // hold suspends the weather system, and a suspended system applies
        // nothing that arrives after it.
        if (!_everSent || _sent.pinSky != _preview.pinSky || (_preview.pinSky && _sent.skyHour != _preview.skyHour)) {
            if (_preview.pinSky) {
                service->QueueSetFakeTimeOfDay(_preview.skyHour);
            }
            else if (_everSent) {
                service->QueueClearFakeTimeOfDay();
            }
        }

        if (!_everSent || _sent.clockSpeed != _preview.clockSpeed) {
            service->QueueSetWorldTimeRatio(_preview.clockSpeed);
        }

        // The blend length is not itself a reason to blend again: it is how
        // long the next one takes.
        if (!_preview.preset.empty() && (_sent.preset != _preview.preset || _presetSent != _presetRequest)) {
            service->QueueBlendPreset(_preview.preset.c_str(), _preview.EffectiveBlendSeconds(), true);
        }

        if (!_everSent || _sent.overrideRain != _preview.overrideRain || (_preview.overrideRain && _sent.rainIntensity != _preview.rainIntensity)) {
            service->QueueSetRainOverride(_preview.overrideRain ? _preview.rainIntensity : -1.0F);
        }

        if (!_everSent || _sent.holdWeather != _preview.holdWeather) {
            service->QueueSetWeatherUpdateEnabled(!_preview.holdWeather);
        }

        _sent       = _preview;
        _presetSent = _presetRequest;
        _everSent   = true;
    }

    void EditorEnvironment::Release() {
        auto *service = Service();
        if (service == nullptr || !_everSent) {
            _everSent = false;
            return;
        }

        // Only what this panel changed. The blended preset is not restored:
        // a blend cannot be taken back, and the weather system rolls its own
        // next sky as soon as it is running again.
        if (_sent.pinSky) {
            service->QueueClearFakeTimeOfDay();
        }
        if (_sent.clockSpeed != EnvironmentPreview::kDefaultClockSpeed) {
            service->QueueSetWorldTimeRatio(EnvironmentPreview::kDefaultClockSpeed);
        }
        if (_sent.overrideRain) {
            service->QueueSetRainOverride(-1.0F);
        }
        if (_sent.holdWeather) {
            service->QueueSetWeatherUpdateEnabled(true);
        }

        _sent              = EnvironmentPreview {};
        _everSent          = false;
        _presetSent        = _presetRequest;
        _profilesRequested = false;
    }

    bool EditorEnvironment::Patch(const nlohmann::json &settings, std::string &error) {
        auto merged = _preview.ToJson();
        if (!settings.is_object()) {
            error = "Environment settings must be an object.";
            return false;
        }
        merged.update(settings);
        EnvironmentPreview next;
        if (!EnvironmentPreview::FromJson(merged, next, error)) {
            return false;
        }
        const bool repeatPreset = !next.preset.empty() && next.preset == _preview.preset;
        _preview                = next;
        if (repeatPreset) {
            ++_presetRequest;
        }
        return true;
    }

    nlohmann::json EditorEnvironment::Describe() const {
        nlohmann::json result {{"settings", _preview.ToJson()}, {"applied", _everSent}, {"warnings", _preview.Warnings()}};

        auto *service = Service();
        if (service == nullptr) {
            return result;
        }

        const KCDC::Features::World::EnvironmentState state = service->GetState();
        result["clock"]                                     = {{"available", state.calendarAvailable}, {"worldDay", state.worldDay}, {"dayOfWeek", state.dayOfWeek}, {"hourOfDay", state.hourOfDay}, {"speed", state.worldTimeRatio}, {"heldByGame", state.worldTimePaused}};
        result["sky"]             = {{"available", state.timeOfDayAvailable}, {"hour", state.timeOfDayFaked ? state.fakeTimeOfDayHour : state.time}, {"pinned", state.timeOfDayFaked}, {"sunLatitude", state.sunLatitude}, {"sunLongitude", state.sunLongitude},
            {"sunLinkedToClock", state.sunLinkedToTime}};
        result["weather"]         = {{"available", state.weatherAvailable}, {"updating", state.weatherUpdateEnabled}, {"rainIntensity", state.rainIntensity}, {"rainAmount", state.rainAmount},
            {"rainOverride", state.rainIntensityOverride >= 0.0F ? nlohmann::json(state.rainIntensityOverride) : nlohmann::json(nullptr)}, {"windSpeed", state.windSpeed}, {"presetCount", state.presetNames.size()},
            {"currentPreset", state.currentPreset < state.presetNames.size() ? nlohmann::json(state.presetNames[state.currentPreset]) : nlohmann::json(nullptr)},
            {"blendTarget", state.blendTargetPreset != kNoPreset && state.blendTargetPreset < state.presetNames.size() ? nlohmann::json(state.presetNames[state.blendTargetPreset]) : nlohmann::json(nullptr)}, {"blendFactor", state.presetBlendFactor}};
        result["pendingCommands"] = state.pendingCommands;
        result["lastAction"]      = service->GetLastAction();
        if (!_preview.preset.empty() && std::find(state.presetNames.begin(), state.presetNames.end(), _preview.preset) == state.presetNames.end()) {
            result["warnings"].push_back("This level has no preset named " + _preview.preset + "; wb_environment_presets_query lists the ones it loaded.");
        }
        return result;
    }

    nlohmann::json EditorEnvironment::DescribePresets(const std::string &query, std::size_t offset, std::size_t limit) const {
        nlohmann::json result {{"presets", nlohmann::json::array()}, {"profiles", nlohmann::json::array()}, {"total", 0}};

        auto *service = Service();
        if (service == nullptr) {
            return result;
        }

        const KCDC::Features::World::EnvironmentState state = service->GetState();
        std::size_t total                                   = 0;
        for (std::size_t index = 0; index < state.presetNames.size(); ++index) {
            const std::string &name = state.presetNames[index];
            if (name.empty() || !Framework::Utils::StringUtils::ContainsIgnoreCase(name, query)) {
                continue;
            }
            if (total >= offset && result["presets"].size() < limit) {
                result["presets"].push_back({{"name", name}, {"current", index == state.currentPreset}});
            }
            ++total;
        }
        result["total"]       = total;
        const std::size_t end = std::min(offset, total) + result["presets"].size();
        result["nextOffset"]  = end < total ? nlohmann::json(end) : nlohmann::json(nullptr);

        // The profile rows are what the weather system schedules presets
        // from. Read-only here: a profile name is not a preset name, and
        // feeding one to a blend finds nothing. `offset` pages the presets;
        // the profiles are a bounded list beside them.
        const auto range = [](const KCDC::Features::World::ConditionRange &value) {
            return std::isnan(value.minimum) || std::isnan(value.maximum) ? nlohmann::json(nullptr) : nlohmann::json {{"min", value.minimum}, {"max", value.maximum}};
        };
        std::size_t profiles = 0;
        for (const auto &profile : service->GetProfiles()) {
            if (!Framework::Utils::StringUtils::ContainsIgnoreCase(profile.name, query)) {
                continue;
            }
            ++profiles;
            if (result["profiles"].size() >= limit) {
                continue;
            }
            result["profiles"].push_back({{"name", profile.name}, {"temperature", range(profile.temperature)}, {"rainIntensity", range(profile.rainIntensity)}, {"windVelocity", range(profile.windVelocity)}, {"timeOfDayIntervals", profile.timeOfDayIntervals}});
        }
        result["profileTotal"] = profiles;
        return result;
    }

    bool EditorEnvironment::RunAction(const std::string &action, float value, std::string &error) {
        auto *service = Service();
        if (service == nullptr) {
            error = "The client is not ready.";
            return false;
        }

        if (action == "forceUpdate") {
            service->QueueForceWeatherUpdate();
            return true;
        }
        if (action == "rebuildClouds") {
            service->QueueRebuildClouds();
            return true;
        }
        if (action == "refreshProfiles") {
            service->QueueRefreshProfiles();
            return true;
        }
        if (action == "skipHours") {
            if (!std::isfinite(value) || value < 0.0F || value > 72.0F) {
                error = "Hours must be between 0 and 72.";
                return false;
            }
            service->QueueSkipHours(value);
            return true;
        }
        if (action == "setWorldHour") {
            if (!std::isfinite(value) || value < 0.0F || value > 24.0F) {
                error = "The hour must be between 0 and 24.";
                return false;
            }
            service->QueueAdoptHourOfDay(value);
            return true;
        }
        error = "Unknown environment action: " + action;
        return false;
    }

    void EditorEnvironment::RenderTab(const EditorSnapshot &snapshot, std::string &notice) {
        ImGui::Spacing();

        auto *service = Service();
        if (service == nullptr) {
            ImGui::TextDisabled("The client is not ready.");
            return;
        }

        if (!snapshot.offline) {
            ImGui::TextWrapped("The environment belongs to the session while you are connected: a sky changed here would be yours alone, and the server owns the clock and the weather. Use the server's own `/world` commands instead.");
            return;
        }

        const KCDC::Features::World::EnvironmentState state = service->GetState();

        ImGui::TextWrapped("Build under the light the scene is meant for. These are preview conditions: they are saved with the project and reach nothing a server reads back.");
        ImGui::Separator();

        // --- The sky ------------------------------------------------------

        ImGui::SeparatorText("Time of day");
        if (!state.calendarAvailable) {
            ImGui::TextDisabled("Waiting for the level's clock.");
        }
        else {
            ImGui::Text("World  day %lld (%s), %s", static_cast<long long>(state.worldDay), kDayNames[(state.dayOfWeek % 7 + 7) % 7], FormatClock(state.hourOfDay).c_str());
            ImGui::Text("Sky    %s", FormatClock(state.timeOfDayFaked ? state.fakeTimeOfDayHour : state.time).c_str());
            ImGui::SameLine();
            if (state.timeOfDayFaked) {
                ImGui::TextColored(kEnvironmentGoodColour, "pinned");
            }
            else {
                ImGui::TextDisabled("following the clock");
            }
        }

        bool pinSky = _preview.pinSky;
        if (ImGui::Checkbox("Pin the sky to one hour", &pinSky)) {
            _preview.pinSky = pinSky;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Lights the level from this hour without moving the world clock, so nothing the level schedules by time of day moves either.");
        }

        ImGui::BeginDisabled(!_preview.pinSky);
        SliderCommit("Hour", _skyHourDraft, _preview.skyHour, 0.0F, 24.0F, "%.2f h");
        for (std::size_t index = 0; index < std::size(kHourShortcuts); ++index) {
            if (index != 0) {
                ImGui::SameLine();
            }
            if (ImGui::SmallButton(kHourShortcuts[index].label)) {
                _preview.skyHour = kHourShortcuts[index].value;
                _preview.pinSky  = true;
            }
        }
        ImGui::EndDisabled();
        if (!_preview.pinSky) {
            ImGui::TextDisabled("Pin the sky to choose an hour, or move the world clock below.");
        }

        // The label follows the draft, not the commit, so dragging off zero
        // stops reading "stopped" before it is released.
        SliderCommit("Clock speed", _clockSpeedDraft, _preview.clockSpeed, 0.0F, EnvironmentPreview::kMaxClockSpeed, _clockSpeedDraft <= 0.0F ? "stopped" : "x%.2f");
        if (ImGui::SmallButton("Stop the clock")) {
            _preview.clockSpeed = 0.0F;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Normal (x15)")) {
            _preview.clockSpeed = EnvironmentPreview::kDefaultClockSpeed;
        }
        if (state.calendarAvailable && state.worldTimePaused) {
            ImGui::TextColored(kEnvironmentWarnColour, "The game is holding the clock as well; changing the speed does not lift that hold.");
        }

        if (ImGui::TreeNode("Move the world clock")) {
            ImGui::TextWrapped("Moves gameplay time itself, which is what the level's own schedules read. Offline the clock may be set back as well as forward.");
            if (!_worldHourEdited && state.calendarAvailable) {
                _worldHourDraft = state.hourOfDay;
            }
            if (ImGui::SliderFloat("Set to", &_worldHourDraft, 0.0F, 24.0F, "%.2f h", ImGuiSliderFlags_AlwaysClamp)) {
                _worldHourEdited = true;
            }
            ImGui::BeginDisabled(!state.calendarAvailable);
            if (ImGui::Button("Set the world clock")) {
                std::string error;
                if (!RunAction("setWorldHour", _worldHourDraft, error)) {
                    notice = error;
                }
                _worldHourEdited = false;
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(140.0F);
            ImGui::DragFloat("##kcdc_env_skip", &_skipHoursDraft, 0.25F, 0.0F, 72.0F, "%.2f h", ImGuiSliderFlags_AlwaysClamp);
            ImGui::SameLine();
            if (ImGui::Button("Skip ahead")) {
                std::string error;
                if (!RunAction("skipHours", _skipHoursDraft, error)) {
                    notice = error;
                }
            }
            ImGui::EndDisabled();
            ImGui::TreePop();
        }

        // --- The weather --------------------------------------------------

        ImGui::SeparatorText("Weather");
        if (!state.weatherAvailable) {
            ImGui::TextColored(kEnvironmentWarnColour, "Waiting for the level's weather system.");
        }
        else {
            ImGui::Text("Rain %.3f", state.rainIntensity);
            ImGui::SameLine();
            ImGui::TextDisabled("(volume %.3f, wind %.2f)", state.rainAmount, state.windSpeed);
            const char *current = state.currentPreset < state.presetNames.size() ? state.presetNames[state.currentPreset].c_str() : "none";
            ImGui::Text("Sky preset  %s", current);
            if (state.blendTargetPreset != kNoPreset && state.blendTargetPreset < state.presetNames.size()) {
                ImGui::SameLine();
                ImGui::TextDisabled("-> %s (%.0f%%)", state.presetNames[state.blendTargetPreset].c_str(), state.presetBlendFactor * 100.0F);
            }
        }

        bool holdWeather = _preview.holdWeather;
        if (ImGui::Checkbox("Hold this sky", &holdWeather)) {
            _preview.holdWeather = holdWeather;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Suspends the weather system so the chosen preset stands. The same switch stops rain being submitted, so rain and a held sky cannot both run.");
        }

        bool overrideRain = _preview.overrideRain;
        if (ImGui::Checkbox("Rain by hand", &overrideRain)) {
            _preview.overrideRain = overrideRain;
        }
        ImGui::BeginDisabled(!_preview.overrideRain);
        SliderCommit("Rain##kcdc_env_rain_intensity", _rainIntensityDraft, _preview.rainIntensity, 0.0F, 1.0F, "%.3f");
        for (std::size_t index = 0; index < std::size(kRainShortcuts); ++index) {
            if (index != 0) {
                ImGui::SameLine();
            }
            if (ImGui::SmallButton(kRainShortcuts[index].label)) {
                _preview.rainIntensity = kRainShortcuts[index].value;
                _preview.overrideRain  = true;
            }
        }
        ImGui::EndDisabled();

        ImGui::SetNextItemWidth(140.0F);
        int blendSeconds = _preview.blendSeconds;
        if (ImGui::SliderInt("Blend", &blendSeconds, 0, EnvironmentPreview::kMaxBlendSeconds, blendSeconds == 0 ? "instant" : "%d game s", ImGuiSliderFlags_AlwaysClamp)) {
            _preview.blendSeconds = blendSeconds;
        }
        ImGui::SameLine();
        if (ImGui::Button("Apply again")) {
            ++_presetRequest;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Blends the chosen preset in again, for when the weather system or a level reload has moved the sky since.");
        }

        if (ImGui::SmallButton("Force an update")) {
            std::string error;
            if (!RunAction("forceUpdate", 0.0F, error)) {
                notice = error;
            }
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Rebuild the clouds")) {
            std::string error;
            if (!RunAction("rebuildClouds", 0.0F, error)) {
                notice = error;
            }
        }

        for (const auto &warning : _preview.Warnings()) {
            ImGui::TextColored(kEnvironmentWarnColour, "%s", warning.c_str());
        }

        // --- The level's presets ------------------------------------------

        if (_preview.preset.empty()) {
            ImGui::TextDisabled("Chosen preset: none, so the level keeps its own.");
        }
        else {
            ImGui::TextColored(kEnvironmentGoodColour, "Chosen preset: %s", _preview.preset.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Clear##kcdc_env_preset")) {
                _preview.preset.clear();
            }
        }
        const bool presetLoaded = std::find(state.presetNames.begin(), state.presetNames.end(), _preview.preset) != state.presetNames.end();
        if (!_preview.preset.empty() && !presetLoaded) {
            ImGui::TextColored(kEnvironmentWarnColour, "This level does not load that preset. Choose one below; preset names belong to the level, not to the project.");
        }

        ImGui::SetNextItemWidth(-1.0F);
        ImGui::InputTextWithHint("##kcdc_env_preset_filter", "filter presets", _presetFilter, sizeof(_presetFilter));
        if (ImGui::BeginChild("##kcdc_env_presets", ImVec2(0.0F, 160.0F), ImGuiChildFlags_Borders)) {
            if (state.presetNames.empty()) {
                ImGui::TextDisabled("No sky presets are loaded yet.");
            }
            for (std::size_t index = 0; index < state.presetNames.size(); ++index) {
                const std::string &name = state.presetNames[index];
                if (name.empty() || !Framework::Utils::StringUtils::ContainsIgnoreCase(name, _presetFilter)) {
                    continue;
                }
                ImGui::PushID(static_cast<int>(index));
                const std::string label = index == state.currentPreset ? name + "   (standing)" : name;
                if (ImGui::Selectable(label.c_str(), name == _preview.preset)) {
                    // Choosing the preset already chosen is a request to
                    // blend it in again rather than a no-op.
                    if (_preview.preset == name) {
                        ++_presetRequest;
                    }
                    _preview.preset = name;
                }
                ImGui::PopID();
            }
        }
        ImGui::EndChild();

        if (ImGui::TreeNode("Weather profiles the level schedules")) {
            ImGui::TextDisabled("Read-only. A profile picks presets by temperature, rain and wind; its name is not a preset name.");
            ImGui::SetNextItemWidth(-1.0F);
            ImGui::InputTextWithHint("##kcdc_env_profile_filter", "filter profiles", _profileFilter, sizeof(_profileFilter));
            const std::vector<KCDC::Features::World::WeatherProfile> profiles = service->GetProfiles();
            if (profiles.empty()) {
                ImGui::TextDisabled("No weather profiles loaded yet.");
            }
            else if (ImGui::BeginTable("##kcdc_env_profiles", 5, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY, ImVec2(0.0F, 160.0F))) {
                ImGui::TableSetupColumn("Profile");
                ImGui::TableSetupColumn("Temperature");
                ImGui::TableSetupColumn("Rain");
                ImGui::TableSetupColumn("Wind");
                ImGui::TableSetupColumn("Windows", ImGuiTableColumnFlags_WidthFixed, 60.0F);
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableHeadersRow();
                int row = 0;
                for (const auto &profile : profiles) {
                    if (!Framework::Utils::StringUtils::ContainsIgnoreCase(profile.name, _profileFilter)) {
                        continue;
                    }
                    ImGui::PushID(row++);
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(profile.name.c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(FormatRange(profile.temperature).c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(FormatRange(profile.rainIntensity).c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(FormatRange(profile.windVelocity).c_str());
                    ImGui::TableNextColumn();
                    ImGui::Text("%d", static_cast<int>(profile.timeOfDayIntervals));
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            ImGui::TreePop();
        }

        if (ImGui::Button("Back to the level's own conditions", ImVec2(-1.0F, 0.0F))) {
            _preview = EnvironmentPreview {};
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Hands the sky, the clock speed and the rain back to the game. A preset already blended in stands until the weather system rolls its next one.");
        }

        const std::string lastAction = service->GetLastAction();
        if (!lastAction.empty()) {
            ImGui::TextDisabled("Last: %s", lastAction.c_str());
        }
    }
} // namespace KCDC::Features::Prop
