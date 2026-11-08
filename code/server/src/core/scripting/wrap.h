#pragma once

#include <v8.h>

#include <cstdint>

namespace KCDC::Scripting {
    // Turns a NetworkID into the handle a script sees. Both check the id still names a live entity: the framework's Entity constructor throws for one that does not, and an event carrying a body that has just gone would otherwise take the whole emit down with it. A gone entity
    // reads as JS null, which a handler can test.
    //
    // The isolate must already be entered, with a handle scope and a context open -- these are called from inside WithScriptContext.
    v8::Local<v8::Value> WrapPlayer(v8::Isolate *isolate, std::uint64_t playerNetworkId);
    v8::Local<v8::Value> WrapHorse(v8::Isolate *isolate, std::uint64_t horseNetworkId);
    v8::Local<v8::Value> WrapDog(v8::Isolate *isolate, std::uint64_t dogNetworkId);
    v8::Local<v8::Value> WrapProp(v8::Isolate *isolate, std::uint64_t propNetworkId);
    v8::Local<v8::Value> WrapCart(v8::Isolate *isolate, std::uint64_t cartNetworkId);
    v8::Local<v8::Value> WrapGroundItem(v8::Isolate *isolate, std::uint64_t groundItemNetworkId);
    v8::Local<v8::Value> WrapDoor(v8::Isolate *isolate, std::uint64_t doorNetworkId);
    v8::Local<v8::Value> WrapGate(v8::Isolate *isolate, std::uint64_t gateNetworkId);
    v8::Local<v8::Value> WrapStash(v8::Isolate *isolate, std::uint64_t stashNetworkId);
    v8::Local<v8::Value> WrapMarker(v8::Isolate *isolate, std::uint64_t markerNetworkId);
    v8::Local<v8::Value> WrapBlip(v8::Isolate *isolate, std::uint64_t blipNetworkId);
    v8::Local<v8::Value> WrapNpc(v8::Isolate *isolate, std::uint64_t npcNetworkId);
    // Either side of a fight: an NPC's handle when the id names a server NPC, a player's otherwise.
    v8::Local<v8::Value> WrapCombatant(v8::Isolate *isolate, std::uint64_t networkId);
    v8::Local<v8::Value> WrapQuest(v8::Isolate *isolate, std::uint64_t questNetworkId);
    v8::Local<v8::Value> WrapVfx(v8::Isolate *isolate, std::uint64_t vfxNetworkId);
    v8::Local<v8::Value> WrapLight(v8::Isolate *isolate, std::uint64_t lightNetworkId);
    v8::Local<v8::Value> WrapSiegeEngine(v8::Isolate *isolate, std::uint64_t engineNetworkId);
    v8::Local<v8::Value> WrapSiegeLadder(v8::Isolate *isolate, std::uint64_t ladderNetworkId);
    v8::Local<v8::Value> WrapStonePile(v8::Isolate *isolate, std::uint64_t pileNetworkId);
} // namespace KCDC::Scripting
