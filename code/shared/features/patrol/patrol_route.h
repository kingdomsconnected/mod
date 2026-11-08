#pragma once

#include "shared/features/npc/npc_types.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

namespace KCDC::Shared::Game {
    enum class PatrolMode {
        Once,
        Loop,
        PingPong
    };

    struct PatrolPoint {
        glm::vec3 position {0.0F};
        std::optional<NpcSpeed> speed;
        std::optional<float> waitSeconds;
        std::optional<float> radius;
    };

    /** Actor-independent authored waypoints. A running patrol owns a copy. */
    struct PatrolRouteDefinition {
        std::string id;
        std::string name;
        PatrolMode mode   = PatrolMode::Loop;
        NpcSpeed speed    = NpcSpeed::Walk;
        float waitSeconds = 0.0F;
        float radius      = 1.5F;
        std::vector<PatrolPoint> points;

        static constexpr std::size_t kMaxPoints = 256;
        static constexpr float kMaxWaitSeconds  = 3600.0F;

        bool IsSane(bool named = true) const {
            if ((named && id.empty()) || id.size() > 128 || id.find('\0') != std::string::npos || name.size() > 256 || name.find('\0') != std::string::npos || points.empty() || points.size() > kMaxPoints || !std::isfinite(waitSeconds) || waitSeconds < 0
                || waitSeconds > kMaxWaitSeconds) {
                return false;
            }
            if (mode != PatrolMode::Once && mode != PatrolMode::Loop && mode != PatrolMode::PingPong) {
                return false;
            }
            for (const PatrolPoint &point : points) {
                NpcIntent leg;
                leg.kind         = NpcIntentKind::MoveTo;
                leg.point        = point.position;
                leg.speed        = point.speed.value_or(speed);
                leg.radius       = point.radius.value_or(radius);
                const float wait = point.waitSeconds.value_or(waitSeconds);
                if (!leg.IsSane() || !std::isfinite(wait) || wait < 0 || wait > kMaxWaitSeconds) {
                    return false;
                }
            }
            NpcIntent defaults;
            defaults.kind   = NpcIntentKind::MoveTo;
            defaults.speed  = speed;
            defaults.radius = radius;
            return defaults.IsSane();
        }
    };

    inline const char *PatrolModeName(PatrolMode mode) {
        switch (mode) {
        case PatrolMode::Once: return "once";
        case PatrolMode::PingPong: return "pingPong";
        default: return "loop";
        }
    }

    inline const char *PatrolSpeedName(NpcSpeed speed) {
        switch (speed) {
        case NpcSpeed::Jog: return "jog";
        case NpcSpeed::Run: return "run";
        default: return "walk";
        }
    }

    inline bool ReadPatrolSpeed(const nlohmann::json &value, NpcSpeed &speed) {
        if (value == "walk") {
            speed = NpcSpeed::Walk;
        }
        else if (value == "jog") {
            speed = NpcSpeed::Jog;
        }
        else if (value == "run") {
            speed = NpcSpeed::Run;
        }
        else {
            return false;
        }
        return true;
    }

    /** Both project arrays and scripting Vector3 objects are accepted. */
    inline bool ReadPatrolPosition(const nlohmann::json &value, glm::vec3 &point) {
        const bool array = value.is_array() && value.size() == 3;
        if (!array && !value.is_object()) {
            return false;
        }
        for (int axis = 0; axis < 3; ++axis) {
            const auto &component = array ? value.at(axis) : value.at(axis == 0 ? "x" : axis == 1 ? "y" : "z");
            if (!component.is_number()) {
                return false;
            }
            point[axis] = component.get<float>();
        }
        return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
    }

    inline bool PatrolRouteFromJson(const nlohmann::json &value, PatrolRouteDefinition &out, std::string &error, bool named = true) {
        error = "Invalid patrol route";
        try {
            if (!value.is_object()) {
                return false;
            }
            PatrolRouteDefinition route;
            route.id   = value.value("id", std::string {});
            route.name = value.value("name", std::string {});
            error += route.id.empty() ? "" : ": " + route.id;
            const std::string mode = value.value("mode", std::string {"loop"});
            if (mode == "once") {
                route.mode = PatrolMode::Once;
            }
            else if (mode == "loop") {
                route.mode = PatrolMode::Loop;
            }
            else if (mode == "pingPong") {
                route.mode = PatrolMode::PingPong;
            }
            else {
                return false;
            }
            if (value.contains("speed") && !ReadPatrolSpeed(value.at("speed"), route.speed)) {
                return false;
            }
            if ((value.contains("waitSeconds") && !value.at("waitSeconds").is_number()) || (value.contains("radius") && !value.at("radius").is_number())) {
                return false;
            }
            route.waitSeconds  = value.value("waitSeconds", 0.0F);
            route.radius       = value.value("radius", 1.5F);
            const auto &points = value.at("points");
            if (!points.is_array() || points.empty() || points.size() > PatrolRouteDefinition::kMaxPoints) {
                return false;
            }
            for (const auto &entry : points) {
                PatrolPoint point;
                if (!entry.is_object() || !ReadPatrolPosition(entry.at("position"), point.position)) {
                    return false;
                }
                if (entry.contains("speed")) {
                    NpcSpeed speed;
                    if (!ReadPatrolSpeed(entry.at("speed"), speed)) {
                        return false;
                    }
                    point.speed = speed;
                }
                if (entry.contains("waitSeconds")) {
                    if (!entry.at("waitSeconds").is_number()) {
                        return false;
                    }
                    point.waitSeconds = entry.at("waitSeconds").get<float>();
                }
                if (entry.contains("radius")) {
                    if (!entry.at("radius").is_number()) {
                        return false;
                    }
                    point.radius = entry.at("radius").get<float>();
                }
                route.points.push_back(point);
            }
            if (!route.IsSane(named)) {
                return false;
            }
            out = std::move(route);
            error.clear();
            return true;
        }
        catch (const nlohmann::json::exception &) {
            return false;
        }
    }

    inline nlohmann::json PatrolRouteToJson(const PatrolRouteDefinition &route, bool script = false) {
        nlohmann::json value {{"id", route.id}, {"name", route.name}, {"mode", PatrolModeName(route.mode)}, {"speed", PatrolSpeedName(route.speed)}, {"waitSeconds", route.waitSeconds}, {"radius", route.radius}, {"points", nlohmann::json::array()}};
        for (const PatrolPoint &point : route.points) {
            nlohmann::json entry;
            entry["position"] = script ? nlohmann::json {{"x", point.position.x}, {"y", point.position.y}, {"z", point.position.z}} : nlohmann::json {point.position.x, point.position.y, point.position.z};
            if (point.speed) {
                entry["speed"] = PatrolSpeedName(*point.speed);
            }
            if (point.waitSeconds) {
                entry["waitSeconds"] = *point.waitSeconds;
            }
            if (point.radius) {
                entry["radius"] = *point.radius;
            }
            value["points"].push_back(std::move(entry));
        }
        return value;
    }

    /** Index progression shared by NPC orders and editor preview. Endpoints are visited once per turn. */
    class PatrolCursor final {
      public:
        std::size_t index = 0;
        std::size_t lap   = 0;
        int direction     = 1;

        bool Advance(PatrolMode mode, std::size_t count) {
            if (count == 0) {
                return false;
            }
            if (mode == PatrolMode::Once) {
                if (index + 1 >= count) {
                    return false;
                }
                ++index;
            }
            else if (mode == PatrolMode::Loop || count == 1) {
                if (++index >= count) {
                    index = 0;
                    ++lap;
                }
            }
            else if (direction > 0) {
                if (index + 1 < count) {
                    ++index;
                }
                else {
                    direction = -1;
                    --index;
                }
            }
            else if (index > 0) {
                --index;
            }
            else {
                direction = 1;
                ++lap;
                ++index;
            }
            return true;
        }
    };
} // namespace KCDC::Shared::Game
