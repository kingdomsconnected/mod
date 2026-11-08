#pragma once

#include "shared/game/progression_catalog.h"
#include <cmath>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace KCDC::Shared::Game {
    /** Validates the complete snapshot before the first restoration order. */
    struct ProgressionRestorePlan {
        std::vector<std::pair<std::uint8_t, std::uint32_t>> tracks;
        std::vector<std::pair<std::uint8_t, std::uint32_t>> points;
        RPC::PerkSet perks;
        bool replacePerks = false;

        static ProgressionRestorePlan Parse(const nlohmann::json &snapshot) {
            if (!snapshot.is_object()) {
                throw std::invalid_argument("Progression snapshot must be an object");
            }
            ProgressionRestorePlan plan;
            if (snapshot.contains("tracks")) {
                if (!snapshot.at("tracks").is_object()) {
                    throw std::invalid_argument("Snapshot tracks must be an object");
                }
                for (const auto &[name, state] : snapshot.at("tracks").items()) {
                    const int index = FindProgressionTrack(name);
                    if (index < 0 || !state.is_object() || !state.contains("level") || !state.at("level").is_number_integer() || !state.contains("xp") || !state.at("xp").is_number()) {
                        throw std::invalid_argument("Invalid snapshot track");
                    }
                    const auto &track  = g_ProgressionTracks[index];
                    const double level = state.at("level").get<double>();
                    const double xp    = state.at("xp").get<double>();
                    // The digest carries the soul's raw remainder, so a capped track
                    // read back from `player.progression` may hold XP past its last
                    // level. The cap is the same total either way: drop it, as
                    // `ProgressFromTotalXpQ7` does.
                    const bool capped = level == track.cap;
                    if (!std::isfinite(xp) || level < 0 || level > track.cap || xp < 0 || (!capped && xp * 128 >= XpQ7ToNextLevel(track, static_cast<std::uint32_t>(level)))) {
                        throw std::invalid_argument("Invalid snapshot progression value");
                    }
                    if (!IsInducedTrack(index)) {
                        plan.tracks.emplace_back(static_cast<std::uint8_t>(index), static_cast<std::uint32_t>(TotalXpQ7ForLevel(track, static_cast<std::uint32_t>(level)) + (capped ? 0U : static_cast<std::uint32_t>(xp * 128))));
                    }
                }
            }
            if (snapshot.contains("perks")) {
                if (!snapshot.at("perks").is_array()) {
                    throw std::invalid_argument("Snapshot perks must be an array");
                }
                plan.replacePerks = true;
                for (const auto &value : snapshot.at("perks")) {
                    if (!value.is_string()) {
                        throw std::invalid_argument("Invalid snapshot perk");
                    }
                    const auto *perk = ResolvePerk(value.get<std::string>());
                    if (perk == nullptr) {
                        throw std::invalid_argument("Unknown snapshot perk");
                    }
                    plan.perks.Set(PerkIndexOf(*perk), true);
                }
            }
            if (snapshot.contains("perkPoints")) {
                if (!snapshot.at("perkPoints").is_object()) {
                    throw std::invalid_argument("Snapshot perkPoints must be an object");
                }
                for (const auto &[name, value] : snapshot.at("perkPoints").items()) {
                    const int index = name == "main" ? RPC::kMainPerkTree : FindProgressionTrack(name);
                    if (index < 0 || !value.is_number_integer() || value.get<double>() < 0 || value.get<double>() > 65535) {
                        throw std::invalid_argument("Invalid snapshot perk point bank");
                    }
                    plan.points.emplace_back(static_cast<std::uint8_t>(index), value.get<std::uint32_t>());
                }
            }
            return plan;
        }
    };
} // namespace KCDC::Shared::Game
