#pragma once

#include <nlohmann/json.hpp>
#include <v8.h>

#include <cstdint>
#include <string>

namespace KCDC::Scripting {
    void RegisterCraftingBindings(v8::Isolate *isolate, v8::Local<v8::Object> global);
    /** `craftingStarting`: false when a handler refused. */
    bool CraftingStarting(std::uint64_t playerId, const nlohmann::json &proposal);
    /** `craftingCompleting`: false when a handler refused. */
    bool CraftingCompleting(std::uint64_t playerId, const nlohmann::json &proposal);
    /** `craftingRefunding`: true only when a synchronous handler called refund(). */
    bool CraftingRefunding(std::uint64_t playerId, const nlohmann::json &proposal);
    /** `craftingStarted`, `craftingCompleted` or `craftingEnded`, with a frozen payload. */
    void EmitCraftingEvent(const std::string &name, std::uint64_t playerId, const nlohmann::json &payload);
} // namespace KCDC::Scripting
