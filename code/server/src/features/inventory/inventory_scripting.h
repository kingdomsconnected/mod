#pragma once

#include <nlohmann/json.hpp>
#include <v8.h>

#include <cstdint>
#include <string>

namespace KCDC::Scripting {
    /** A request object argument as JSON, as every inventory binding takes one; throws for a missing or oversized one. */
    nlohmann::json InventoryRequestArgument(const v8::FunctionCallbackInfo<v8::Value> &info, int index);
    /** JSON as a plain script value. */
    v8::Local<v8::Value> InventoryValue(v8::Isolate *isolate, const nlohmann::json &value);
    /** JSON as a deeply frozen script value, so one veto handler cannot change what the next one reads. */
    v8::Local<v8::Value> FrozenValue(v8::Isolate *isolate, const nlohmann::json &value);

    void EmitPlayerInventoryChanged(std::uint64_t playerId, const nlohmann::json &change);
    void EmitPlayerInventoryReady(std::uint64_t playerId);
    void EmitPlayerCustomItemUse(std::uint64_t playerId, const nlohmann::json &row, std::uint64_t revision);
    void EmitPlayerItemUsed(std::uint64_t playerId, const std::string &itemClass, const char *kind, std::uint32_t amount);
    void GetPlayerInventory(const v8::FunctionCallbackInfo<v8::Value> &info);
    void GetNpcInventory(const v8::FunctionCallbackInfo<v8::Value> &info);
    void RegisterInventoryBindings(v8::Isolate *isolate, v8::Local<v8::Object> global);
} // namespace KCDC::Scripting
