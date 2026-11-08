#pragma once

#include "shared/game/gathering_sources.h"

#include <nlohmann/json.hpp>
#include <v8.h>

#include <cstdint>

namespace KCDC::Scripting {
    v8::Local<v8::Array> HerbsToArray(v8::Isolate *isolate, const std::vector<KCDC::Shared::Game::GatheringSource> &herbs);
    /** `gatheringHarvest`: false when a handler refused the harvest. */
    bool GatheringHarvest(std::uint64_t playerId, const nlohmann::json &proposal);
    void EmitGatheringHarvested(std::uint64_t playerId, const nlohmann::json &payload);
    void RegisterGatheringBindings(v8::Isolate *isolate, v8::Local<v8::Object> global);
} // namespace KCDC::Scripting
