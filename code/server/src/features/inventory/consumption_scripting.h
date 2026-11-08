#pragma once

#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>

namespace KCDC::Scripting {
    void DescribeConsumable(nlohmann::json &consumption);
    /** Synchronous vetoes run before stock or native effects change. */
    bool PlayerConsuming(std::uint64_t player, const nlohmann::json &consumption);
    bool PlayerConsumptionEffects(std::uint64_t player, const nlohmann::json &consumption);
    bool PlayerPoisonAbsorbing(std::uint64_t player, const nlohmann::json &consumption, const std::string &buff);
    /** `playerPreparing`: false when a handler refused. */
    bool PlayerPreparing(std::uint64_t player, const nlohmann::json &preparation);
    void PlayerConsumed(std::uint64_t player, const nlohmann::json &consumption);
    void PlayerStatsChanged(std::uint64_t player, const nlohmann::json &changes);
    void RegisterConsumptionEvents();
} // namespace KCDC::Scripting
