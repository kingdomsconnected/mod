#pragma once

#include <nlohmann/json.hpp>

#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace KCDC::Features::Prop {
    struct EditorSnapshot;

    /**
     * The conditions a project is previewed under: the hour its light comes
     * from, the sky standing over it, and whether it is raining.
     *
     * Project-owned, like the viewport, and a preview rather than authored
     * content: it is outside undo history and outside every export, because
     * nothing a server reads back depends on it.
     */
    struct EnvironmentPreview {
        /** The game's own clock speed, in game milliseconds per real millisecond. */
        static constexpr float kDefaultClockSpeed     = 15.0F;
        static constexpr float kMaxClockSpeed         = 200.0F;
        static constexpr int kMaxBlendSeconds         = 3600;
        static constexpr std::size_t kMaxPresetLength = 128;

        /**
         * The calendar's fake time of day: the sky alone, leaving the world
         * clock where it stands. 24 and 0 are the same midnight, so the
         * slider is closed at both ends.
         */
        bool pinSky   = false;
        float skyHour = 12.0F;

        /** 0 stops the clock, which is what holds the light still while a scene is built. */
        float clockSpeed = kDefaultClockSpeed;

        /**
         * A `CTimeOfDay` preset name -- what KCD2 calls weather. Empty leaves
         * the level with whatever sky it chose for itself. The names are the
         * level's own, so a project carried to another level finds none of
         * them and the preview simply stays unset.
         */
        std::string preset;

        /** Blend length in game seconds; 0 applies the preset at once. */
        int blendSeconds = 0;

        /**
         * Suspends the weather system so the preset above stands, instead of
         * being blended away once its six game hours are up. The same switch
         * stops rain submission, so rain and a held sky cannot both run --
         * `Warnings` says so rather than the panel pretending otherwise.
         */
        bool holdWeather = false;

        /** Rain by hand. Off restores the weather system's own calculation. */
        bool overrideRain   = false;
        float rainIntensity = 0.0F;

        bool operator==(const EnvironmentPreview &other) const = default;

        bool IsValid() const {
            return std::isfinite(skyHour) && skyHour >= 0.0F && skyHour <= 24.0F && std::isfinite(clockSpeed) && clockSpeed >= 0.0F && clockSpeed <= kMaxClockSpeed && std::isfinite(rainIntensity) && rainIntensity >= 0.0F && rainIntensity <= 1.0F && blendSeconds >= 0
                   && blendSeconds <= kMaxBlendSeconds && preset.size() <= kMaxPresetLength;
        }

        bool IsDefault() const {
            return *this == EnvironmentPreview {};
        }

        /** What the engine cannot do as asked, in the order a reader should hear it. */
        std::vector<std::string> Warnings() const {
            std::vector<std::string> warnings;
            if (holdWeather && overrideRain) {
                warnings.push_back("Holding the sky suspends the weather system, which also stops rain being submitted. Turn the hold off to see rain.");
            }
            if (holdWeather && blendSeconds > 0) {
                warnings.push_back("A held sky cannot run a timed blend, so the preset is applied at once.");
            }
            if (!holdWeather && !preset.empty()) {
                warnings.push_back("The weather system replaces a blended preset after six game hours. Hold the sky to keep it.");
            }
            if (blendSeconds > 0 && clockSpeed <= 0.0F) {
                warnings.push_back("Blend length is measured in game time, which a stopped clock never spends. Use an instant blend or start the clock.");
            }
            return warnings;
        }

        /** The blend length actually sent, which a held sky forces to instant. */
        int EffectiveBlendSeconds() const {
            return holdWeather ? 0 : blendSeconds;
        }

        nlohmann::json ToJson() const {
            return {{"pinSky", pinSky}, {"skyHour", skyHour}, {"clockSpeed", clockSpeed}, {"preset", preset}, {"blendSeconds", blendSeconds}, {"holdWeather", holdWeather}, {"overrideRain", overrideRain}, {"rainIntensity", rainIntensity}};
        }

        static bool FromJson(const nlohmann::json &json, EnvironmentPreview &out, std::string &error) {
            try {
                if (!json.is_object()) {
                    throw std::invalid_argument("Environment must be an object.");
                }
                EnvironmentPreview next;
                next.pinSky        = json.value("pinSky", false);
                next.skyHour       = json.value("skyHour", 12.0F);
                next.clockSpeed    = json.value("clockSpeed", kDefaultClockSpeed);
                next.preset        = json.value("preset", std::string {});
                next.blendSeconds  = json.value("blendSeconds", 0);
                next.holdWeather   = json.value("holdWeather", false);
                next.overrideRain  = json.value("overrideRain", false);
                next.rainIntensity = json.value("rainIntensity", 0.0F);
                if (!next.IsValid()) {
                    throw std::invalid_argument("Environment hour, clock speed, blend length, rain intensity or preset name is out of range.");
                }
                out = next;
                return true;
            }
            catch (const std::exception &exception) {
                error = exception.what();
                return false;
            }
        }

        /** The schema of the patch `FromJson` accepts, so discovery and the parser cannot drift apart. */
        static nlohmann::json Schema() {
            const nlohmann::json boolean {{"type", "boolean"}};
            return {{"type", "object"},
                {"properties", {{"pinSky", boolean}, {"skyHour", {{"type", "number"}, {"minimum", 0}, {"maximum", 24}}}, {"clockSpeed", {{"type", "number"}, {"minimum", 0}, {"maximum", kMaxClockSpeed}}}, {"preset", {{"type", "string"}, {"maxLength", kMaxPresetLength}}},
                                   {"blendSeconds", {{"type", "integer"}, {"minimum", 0}, {"maximum", kMaxBlendSeconds}}}, {"holdWeather", boolean}, {"overrideRain", boolean}, {"rainIntensity", {{"type", "number"}, {"minimum", 0}, {"maximum", 1}}}}},
                {"required", nlohmann::json::array()}, {"additionalProperties", false}};
        }
    };

    /**
     * The World Builder's Environment tab: the hour, the sky preset and the
     * rain a project is previewed under.
     *
     * Render thread, with one exception -- the game thread asks for a release
     * through `RequestRelease`, which only touches an atomic the panel reads
     * on its next frame. Everything that reaches the engine goes through
     * `World::WorldEnvironmentService`, whose queue is thread-safe and whose
     * every command is refused while a session is connected. That makes this
     * an offline tool by construction rather than by a check here: a client
     * in multiplayer would be changing a sky only it can see.
     */
    class EditorEnvironment final {
      public:
        /**
         * Reconciles the engine with the preview, and answers a queued
         * release. Called every frame, and gated on the offline session
         * rather than on the panel being drawn: F7 hands the controls back
         * so the level can be walked in the conditions it was built for,
         * and an MCP client edits them with the viewport closed.
         */
        void Synchronize(bool offline);

        void RenderTab(const EditorSnapshot &snapshot, std::string &notice);

        /** Game thread: asks the next frame to give the sky and the weather back. */
        void RequestRelease() {
            _releaseRequested.store(true, std::memory_order_release);
        }

        const EnvironmentPreview &Preview() const {
            return _preview;
        }

        /** Opening a project: adopted as the target, applied by the next `Synchronize`. */
        void Adopt(const EnvironmentPreview &preview);

        // --- MCP ---

        /** Merges `settings` over the current preview. Rejects the whole patch on any invalid field. */
        bool Patch(const nlohmann::json &settings, std::string &error);

        /** The preview, what the engine currently reports, and the warnings the two produce. */
        nlohmann::json Describe() const;

        /** The level's loaded sky presets and the weather-profile rows behind them, paged. */
        nlohmann::json DescribePresets(const std::string &query, std::size_t offset, std::size_t limit) const;

        /** A one-shot: `forceUpdate`, `rebuildClouds`, `refreshProfiles`, `skipHours` or `setWorldHour`. */
        bool RunAction(const std::string &action, float value, std::string &error);

      private:
        /** Queues whatever differs between `_preview` and `_sent`. */
        void Apply();

        /** Queues the restore of everything `_sent` changed, and forgets it. */
        void Release();

        EnvironmentPreview _preview;
        EnvironmentPreview _sent;
        bool _everSent = false;

        // Non-zero asks for the preset to be blended again even though its
        // name has not changed -- the sky drifted, or the level reloaded its
        // preset table. Never persisted: it is a button, not a setting.
        std::uint64_t _presetRequest = 0;
        std::uint64_t _presetSent    = 0;

        bool _profilesRequested = false;

        std::atomic_bool _releaseRequested = false;

        // Drafts for the controls whose commit costs a native call: dragged
        // here, committed when the control is released.
        float _skyHourDraft       = 12.0F;
        float _clockSpeedDraft    = EnvironmentPreview::kDefaultClockSpeed;
        float _rainIntensityDraft = 0.0F;

        // The world clock is a target and a button, not a setting: it moves
        // gameplay time, so nothing here sends it on its own.
        float _worldHourDraft = 12.0F;
        bool _worldHourEdited = false;
        float _skipHoursDraft = 1.0F;

        char _presetFilter[64]  = {};
        char _profileFilter[64] = {};
    };
} // namespace KCDC::Features::Prop
