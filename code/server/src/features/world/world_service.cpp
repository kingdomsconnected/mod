#include "world_service.h"

#include "features/world/world_scripting.h"

#include <core_modules.h>
#include <fmt/format.h>
#include <mafianet/get_time.h>
#include <networking/replication/replication_manager.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace KCDC::Features::World {
    namespace {
        constexpr std::uint64_t kSnapshotIntervalMs = 1000;
        constexpr std::int64_t kMaxDay              = KCDC::Shared::Entities::WorldEntity::kMaxWorldTime / KCDC::Shared::Entities::WorldEntity::kWorldDay;
        constexpr double kMaxBlendSeconds           = 21600;

        std::uint64_t Now() {
            return MafiaNet::GetTimeUS() / 1000;
        }

        double ParseNumber(const std::string &text) {
            std::size_t consumed = 0;
            const double value   = std::stod(text, &consumed);

            if (consumed != text.size() || !std::isfinite(value)) {
                throw std::invalid_argument("number");
            }

            return value;
        }

        bool ValidHour(double hour) {
            return std::isfinite(hour) && hour >= 0 && hour < 24;
        }
    } // namespace

    void WorldService::Install() {
        if (_entity != nullptr) {
            return;
        }

        _entity = Framework::CoreModules::GetReplication()->CreateEntity<KCDC::Shared::Entities::WorldEntity>();

        if (_entity == nullptr) {
            throw std::runtime_error("Could not allocate WorldEntity");
        }

        _entity->SetOwner(MafiaNet::UNASSIGNED_PEER_GUID);
        _entity->streaming.alwaysVisible = true;
        _entity->SetVirtualWorld(MafiaNet::VIRTUAL_WORLD_GLOBAL);
        _entity->generation = MafiaNet::GetTimeUS() + 1;

        _worldTimeMs = _entity->worldTimeMs;
        _realTimeMs  = Now();

        Publish();
    }

    void WorldService::Tick(std::uint64_t now) {
        if (now <= _realTimeMs) {
            return;
        }

        const std::int64_t previousDay = GetDay();
        const auto elapsed             = now - _realTimeMs;
        const double delta             = static_cast<double>(elapsed) * _entity->timeScale + _fraction;
        const double bounded           = std::min(delta, static_cast<double>(KCDC::Shared::Entities::WorldEntity::kMaxWorldTime - _worldTimeMs));
        const auto whole               = static_cast<std::int64_t>(bounded);

        _worldTimeMs += whole;
        _fraction   = _worldTimeMs == KCDC::Shared::Entities::WorldEntity::kMaxWorldTime ? 0 : bounded - static_cast<double>(whole);
        _realTimeMs = now;

        if (_worldTimeMs == KCDC::Shared::Entities::WorldEntity::kMaxWorldTime && _entity->timeScale != 0) {
            _entity->timeScale = 0;
            ++_entity->clockRevision;
        }

        const double seconds    = static_cast<double>(elapsed) / 1000.0;
        const bool raining      = _entity->rainIntensity > KCDC::Shared::Entities::WorldEntity::kWetThreshold;
        const double phaseEnd   = static_cast<double>(_rainPhaseSeconds) + seconds;
        const double afterDelay = std::max(0.0, phaseEnd - KCDC::Shared::Entities::WorldEntity::kPuddleDelay) - std::max(0.0, static_cast<double>(_rainPhaseSeconds) - KCDC::Shared::Entities::WorldEntity::kPuddleDelay);

        _wetness          = static_cast<float>(std::clamp(_wetness + seconds * (raining ? KCDC::Shared::Entities::WorldEntity::kWetCreation : -KCDC::Shared::Entities::WorldEntity::kWetDrying), 0.0, 3.0));
        _puddles          = static_cast<float>(std::clamp(_puddles + afterDelay * (raining ? KCDC::Shared::Entities::WorldEntity::kPuddleCreation : -KCDC::Shared::Entities::WorldEntity::kPuddleDrying), 0.0, 1.0));
        _rainPhaseSeconds = static_cast<float>(std::min(phaseEnd, 86400.0));

        ReportDayChange(previousDay);
    }

    bool WorldService::AdvanceTo(std::int64_t target) {
        if (target < _worldTimeMs || target > KCDC::Shared::Entities::WorldEntity::kMaxWorldTime) {
            return false;
        }

        _worldTimeMs = target;
        _fraction    = 0;

        return true;
    }

    void WorldService::Publish() {
        _entity->worldTimeMs      = _worldTimeMs;
        _entity->realTimeMs       = _realTimeMs;
        _entity->wetness          = _wetness;
        _entity->puddles          = _puddles;
        _entity->rainPhaseSeconds = _rainPhaseSeconds;
        ++_entity->sequence;
        _lastPublish = _realTimeMs;
    }

    void WorldService::ReportDayChange(std::int64_t previousDay) const {
        if (GetDay() != previousDay) {
            KCDC::Scripting::Events::WorldDayChanged(GetDay());
        }
    }

    void WorldService::Update() {
        Tick(Now());

        if (_realTimeMs - _lastPublish >= kSnapshotIntervalMs) {
            Publish();
        }
    }

    std::int64_t WorldService::GetDay() const {
        return _worldTimeMs / KCDC::Shared::Entities::WorldEntity::kWorldDay;
    }

    double WorldService::GetHour() const {
        return static_cast<double>(_worldTimeMs % KCDC::Shared::Entities::WorldEntity::kWorldDay) / KCDC::Shared::Entities::WorldEntity::kWorldHour;
    }

    float WorldService::GetTimeScale() const {
        return _entity->timeScale;
    }

    const std::string &WorldService::GetWeather() const {
        return _entity->targetPreset;
    }

    const std::string &WorldService::GetPreviousWeather() const {
        return _entity->sourcePreset;
    }

    bool WorldService::IsWeatherBlending() const {
        return _worldTimeMs < _entity->weatherStartMs + _entity->weatherDurationMs;
    }

    double WorldService::GetWeatherRemaining() const {
        const std::int64_t settlesAt = _entity->weatherStartMs + _entity->weatherDurationMs;

        return settlesAt > _worldTimeMs ? static_cast<double>(settlesAt - _worldTimeMs) / 1000.0 : 0.0;
    }

    float WorldService::GetRainIntensity() const {
        return _entity->rainIntensity;
    }

    float WorldService::GetRainAmount() const {
        return _entity->rainAmount;
    }

    const glm::vec3 &WorldService::GetWind() const {
        return _entity->wind;
    }

    float WorldService::GetWindSpeed() const {
        return glm::length(_entity->wind);
    }

    float WorldService::GetWetness() const {
        return _wetness;
    }

    float WorldService::GetPuddles() const {
        return _puddles;
    }

    bool WorldService::SetTimeScale(float scale) {
        Tick(Now());

        if (!KCDC::Shared::Entities::WorldEntity::ValidWorldScale(scale)) {
            return false;
        }

        _entity->timeScale = scale;
        ++_entity->clockRevision;
        Publish();

        return true;
    }

    bool WorldService::SetHour(double hour) {
        Tick(Now());

        if (!ValidHour(hour)) {
            return false;
        }

        auto target = _worldTimeMs / KCDC::Shared::Entities::WorldEntity::kWorldDay * KCDC::Shared::Entities::WorldEntity::kWorldDay + static_cast<std::int64_t>(hour * KCDC::Shared::Entities::WorldEntity::kWorldHour);

        if (target < _worldTimeMs) {
            target += KCDC::Shared::Entities::WorldEntity::kWorldDay;
        }

        const std::int64_t previousDay = GetDay();

        if (!AdvanceTo(target)) {
            return false;
        }

        ++_entity->clockRevision;
        Publish();
        ReportDayChange(previousDay);

        return true;
    }

    bool WorldService::SetTime(std::int64_t day, double hour) {
        Tick(Now());

        if (day < 0 || day > kMaxDay || !ValidHour(hour)) {
            return false;
        }

        const std::int64_t previousDay = GetDay();

        if (!AdvanceTo(day * KCDC::Shared::Entities::WorldEntity::kWorldDay + static_cast<std::int64_t>(hour * KCDC::Shared::Entities::WorldEntity::kWorldHour))) {
            return false;
        }

        ++_entity->clockRevision;
        Publish();
        ReportDayChange(previousDay);

        return true;
    }

    bool WorldService::SetWeather(const std::string &preset, double seconds) {
        Tick(Now());

        // An unfinished blend has no single source preset.
        if (!KCDC::Shared::Entities::WorldEntity::ValidPreset(preset) || !std::isfinite(seconds) || seconds < 0 || seconds > kMaxBlendSeconds || IsWeatherBlending()) {
            return false;
        }

        const std::string previous = _entity->targetPreset;

        _entity->sourcePreset      = _entity->targetPreset;
        _entity->targetPreset      = preset;
        _entity->weatherStartMs    = _worldTimeMs;
        _entity->weatherDurationMs = static_cast<std::int64_t>(seconds * 1000);
        ++_entity->weatherRevision;
        Publish();

        KCDC::Scripting::Events::WorldWeatherChanged(preset, previous, seconds);

        return true;
    }

    bool WorldService::SetRain(float intensity, float amount) {
        Tick(Now());

        if (!KCDC::Shared::Entities::WorldEntity::ValidUnit(intensity) || !KCDC::Shared::Entities::WorldEntity::ValidUnit(amount)) {
            return false;
        }

        if ((intensity > KCDC::Shared::Entities::WorldEntity::kWetThreshold) != (_entity->rainIntensity > KCDC::Shared::Entities::WorldEntity::kWetThreshold)) {
            _rainPhaseSeconds = 0;
        }

        _entity->rainIntensity = intensity;
        _entity->rainAmount    = amount;
        ++_entity->rainRevision;
        Publish();

        return true;
    }

    bool WorldService::SetWind(const glm::vec3 &wind) {
        Tick(Now());

        if (!KCDC::Shared::Entities::WorldEntity::ValidWind(wind)) {
            return false;
        }

        _entity->wind = wind;
        ++_entity->windRevision;
        Publish();

        return true;
    }

    std::string WorldService::Describe() const {
        return fmt::format("World day {}, hour {:.6f}, scale {:.6f}, weather {}, rain {:.6f}, wind {:.6f} {:.6f} {:.6f}", GetDay(), GetHour(), _entity->timeScale, _entity->targetPreset, _entity->rainIntensity, _entity->wind.x, _entity->wind.y, _entity->wind.z);
    }

    std::string WorldService::ExecuteCommand(const std::vector<std::string> &args) {
        constexpr auto usage = "Usage: /world info | /world scale 0..200 | /world hour 0..<24 | /world day <day> <hour> | /world weather <native-preset> [game-seconds 0..21600] | /world rain 0..1 [amount 0..1] | /world wind <x> <y> [z]";

        if (args.size() == 1 && args[0] == "help") {
            return usage;
        }

        Tick(Now());

        if (args.empty() || (args.size() == 1 && args[0] == "info")) {
            return Describe();
        }

        bool accepted = false;

        try {
            if (args[0] == "scale" && args.size() == 2) {
                accepted = SetTimeScale(static_cast<float>(ParseNumber(args[1])));
            }
            else if (args[0] == "hour" && args.size() == 2) {
                accepted = SetHour(ParseNumber(args[1]));
            }
            else if (args[0] == "day" && args.size() == 3) {
                // Checked here, not in SetTime: "3.5" is a typo to refuse rather than truncate.
                const double day = ParseNumber(args[1]);

                accepted = day >= 0 && day <= static_cast<double>(kMaxDay) && day == std::floor(day) && SetTime(static_cast<std::int64_t>(day), ParseNumber(args[2]));
            }
            else if (args[0] == "weather" && (args.size() == 2 || args.size() == 3)) {
                accepted = SetWeather(args[1], args.size() == 3 ? ParseNumber(args[2]) : 0);
            }
            else if (args[0] == "rain" && (args.size() == 2 || args.size() == 3)) {
                accepted = SetRain(static_cast<float>(ParseNumber(args[1])), args.size() == 3 ? static_cast<float>(ParseNumber(args[2])) : 1.0F);
            }
            else if (args[0] == "wind" && (args.size() == 3 || args.size() == 4)) {
                accepted = SetWind(glm::vec3(static_cast<float>(ParseNumber(args[1])), static_cast<float>(ParseNumber(args[2])), args.size() == 4 ? static_cast<float>(ParseNumber(args[3])) : 0.0F));
            }
        }
        catch (const std::exception &) {
            accepted = false;
        }

        if (accepted) {
            return Describe();
        }

        return std::string("Rejected world command (rewind, invalid value, or active blend). ") + usage;
    }
} // namespace KCDC::Features::World
