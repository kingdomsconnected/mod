#include "gathering_scripting.h"

#include "core/scripting/script_context.h"
#include "core/scripting/wrap.h"
#include "core/server.h"
#include "features/inventory/inventory_scripting.h"
#include "shared/game/gathering_catalog.h"
#include "shared/scripting/catalog.h"
#include "shared/scripting/virtual_world_arg.h"
#include <scripting/v8pp_types.h>

#include <cmath>
#include <v8pp/convert.hpp>
#include <v8pp/module.hpp>
#include <vector>

namespace KCDC::Scripting {
    namespace {
        bool ReadWorld(const v8::FunctionCallbackInfo<v8::Value> &info, int index, std::uint32_t &world) {
            world = MafiaNet::VIRTUAL_WORLD_DEFAULT;
            if (info.Length() <= index || info[index]->IsUndefined()) {
                return true;
            }
            if (!info[index]->IsNumber()) {
                return false;
            }
            const auto parsed = VirtualWorldFromNumber(v8pp::from_v8<double>(info.GetIsolate(), info[index]));
            if (!parsed) {
                return false;
            }
            world = *parsed;
            return true;
        }

        void JS_SetRespawnTime(const v8::FunctionCallbackInfo<v8::Value> &info) {
            if (info.Length() != 2 || !info[0]->IsNumber() || !info[1]->IsNumber()) {
                info.GetIsolate()->ThrowError(v8pp::to_v8(info.GetIsolate(), "Gathering.setRespawnTime: expected (minHours, maxHours)"));
                return;
            }
            info.GetReturnValue().Set(Server::Get()->GetGathering().SetRespawnTime(v8pp::from_v8<double>(info.GetIsolate(), info[0]), v8pp::from_v8<double>(info.GetIsolate(), info[1])));
        }

        bool ReadSphere(const v8::FunctionCallbackInfo<v8::Value> &info, glm::vec3 &position, float &radiusOut, std::uint32_t &world) {
            auto *isolate       = info.GetIsolate();
            const auto context  = isolate->GetCurrentContext();
            bool valid          = info.Length() >= 2 && info.Length() <= 3 && info[0]->IsObject() && info[1]->IsNumber() && ReadWorld(info, 2, world);
            const char *names[] = {"x", "y", "z"};
            for (int axis = 0; valid && axis < 3; ++axis) {
                v8::Local<v8::Value> component;
                valid = info[0].As<v8::Object>()->Get(context, v8pp::to_v8(isolate, names[axis])).ToLocal(&component) && component->IsNumber();
                if (valid) {
                    const double value = v8pp::from_v8<double>(isolate, component);
                    valid              = std::isfinite(value) && std::abs(value) <= 100000;
                    position[axis]     = valid ? static_cast<float>(value) : 0;
                }
            }
            const double radius = valid ? v8pp::from_v8<double>(isolate, info[1]) : -1;
            if (!valid || !std::isfinite(radius) || radius < 0 || radius > 100000) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Gathering: expected a finite position, radius 0..100000 metres and optional uint32 virtualWorld"));
                return false;
            }
            radiusOut = static_cast<float>(radius);
            return true;
        }

        void JS_Respawn(const v8::FunctionCallbackInfo<v8::Value> &info) {
            glm::vec3 position {};
            float radius;
            std::uint32_t world;
            if (ReadSphere(info, position, radius, world)) {
                info.GetReturnValue().Set(static_cast<double>(Server::Get()->GetGathering().Respawn(world, position, radius)));
            }
        }

        template <bool Remove>
        void JS_Herbs(const v8::FunctionCallbackInfo<v8::Value> &info) {
            KCDC::Features::Area::AreaRecord area;
            area.definition.shape.type = KCDC::Shared::Game::AreaShapeType::Sphere;
            std::uint32_t world;
            if (!ReadSphere(info, area.definition.shape.position, area.definition.shape.radius, world)) {
                return;
            }
            area.definition.shape.WorldBounds(area.worldPoints, area.boundsMin, area.boundsMax);
            if constexpr (Remove) {
                info.GetReturnValue().Set(static_cast<double>(Server::Get()->GetGathering().RemoveHerbs(area, world)));
            }
            else {
                info.GetReturnValue().Set(HerbsToArray(info.GetIsolate(), Server::Get()->GetGathering().GetHerbs(area, world)));
            }
        }

        void JS_RespawnAll(const v8::FunctionCallbackInfo<v8::Value> &info) {
            std::uint32_t world = 0;
            if (!ReadWorld(info, 0, world)) {
                info.GetIsolate()->ThrowError(v8pp::to_v8(info.GetIsolate(), "Gathering.respawnAll: expected an optional uint32 virtualWorld"));
                return;
            }
            info.GetReturnValue().Set(static_cast<double>(Server::Get()->GetGathering().Respawn(world, std::nullopt, 0)));
        }
    } // namespace

    v8::Local<v8::Array> HerbsToArray(v8::Isolate *isolate, const std::vector<KCDC::Shared::Game::GatheringSource> &herbs) {
        const auto result = v8::Array::New(isolate, static_cast<int>(herbs.size()));
        for (std::size_t index = 0; index < herbs.size(); ++index) {
            const auto &herb = herbs[index];
            const auto row   = v8::Object::New(isolate);
            row->Set(isolate->GetCurrentContext(), v8pp::to_v8(isolate, "kind"), v8pp::to_v8(isolate, herb.kind)).Check();
            row->Set(isolate->GetCurrentContext(), v8pp::to_v8(isolate, "item"), v8pp::to_v8(isolate, std::string(KCDC::Shared::Game::FindGatheringKind(herb.kind)->itemClass))).Check();
            row->Set(isolate->GetCurrentContext(), v8pp::to_v8(isolate, "position"), v8pp::to_v8(isolate, herb.position)).Check();
            result->Set(isolate->GetCurrentContext(), static_cast<std::uint32_t>(index), row).Check();
        }
        return result;
    }

    bool GatheringHarvest(std::uint64_t playerId, const nlohmann::json &proposal) {
        return EmitReservedEventSync("gatheringHarvest", [playerId, &proposal](v8::Isolate *isolate) {
            return std::vector<v8::Local<v8::Value>> {WrapPlayer(isolate, playerId), FrozenValue(isolate, proposal)};
        });
    }

    void EmitGatheringHarvested(std::uint64_t playerId, const nlohmann::json &payload) {
        EmitReservedEvent("gatheringHarvested", [playerId, &payload](v8::Isolate *isolate) {
            return std::vector<v8::Local<v8::Value>> {WrapPlayer(isolate, playerId), InventoryValue(isolate, payload)};
        });
    }

    void RegisterGatheringBindings(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        auto &herb = ServerCatalog().data_type("GatheringHerb", "One available authored herb, sampled when getHerbs is called.");
        herb.add_property("kind", "number", "The game's pickable area ID for this species.");
        herb.add_property("item", "string", "Item class GUID yielded by this species.");
        herb.add_property("position", "Vector3", "The source position in world metres.");
        auto &timing = ServerCatalog().data_type("GatheringRespawnTime", "The server-wide override for herb regrowth, measured in game hours.");
        timing.add_property("minHours", "number", "Lower bound, inclusive. Both bounds zero disable timed regrowth.");
        timing.add_property("maxHours", "number", "Upper bound, inclusive. Equal positive bounds give a fixed duration.");
        v8pp::module gathering(isolate, ServerCatalog(), "Gathering", "Finds and removes authored herbs and controls regrowth. Defaults are five times faster than the game's authored species timings. Changes are held in server memory.");
        const auto spherePosition = v8pp::metadata::param("position", "Vector3", false, "Centre in world metres; finite coordinates within +/-100000.");
        const auto sphereRadius   = v8pp::metadata::param("radius", "number", false, "Inclusive 3D radius in metres, from 0 to 100000.");
        const auto sphereWorld    = v8pp::metadata::param("virtualWorld", "number", true, "One virtual-world bucket, default 0. Pass player.virtualWorld for a player's bucket. 0xffffffff selects only the global bucket. Invalid IDs throw.");
        gathering.function("getHerbs", &JS_Herbs<false>,
            v8pp::metadata::docs("GatheringHerb[]", {spherePosition, sphereRadius, sphereWorld}, "Lists available authored herbs in a sphere, without requiring nearby players. Excludes picked or removed herbs and custom map assets. Invalid arguments throw."));
        gathering.function("removeHerbs", &JS_Herbs<true>,
            v8pp::metadata::docs("number", {spherePosition, sphereRadius, sphereWorld},
                "Removes authored herbs in a sphere, including already-picked plants. Gives no rewards or harvest events. Removal lasts until an explicit respawn or server restart, regardless of regrowth timing changes.",
                "Number newly marked for removal; repeated removal returns 0. Invalid arguments throw before mutation."));
        gathering.property(
            "respawnTime",
            [isolate]() -> v8::Local<v8::Value> {
                const auto &time = Server::Get()->GetGathering().GetRespawnTime();
                return time ? FrozenValue(isolate, {{"minHours", time->minHours}, {"maxHours", time->maxHours}}) : v8::Local<v8::Value>(v8::Null(isolate));
            },
            v8pp::metadata::property_docs("GatheringRespawnTime | null", "Current frozen override, or null for the species defaults at one fifth of their authored duration."));
        gathering.function("setRespawnTime", &JS_SetRespawnTime,
            v8pp::metadata::docs("boolean",
                {
                    v8pp::metadata::param("minHours", "number", false, "Minimum game hours, from 0 to maxHours."),
                    v8pp::metadata::param("maxHours", "number", false, "Maximum game hours, up to 87600. Positive durations must reach at least one game millisecond."),
                },
                "Restarts active picked-herb deadlines from now and sets the policy for future picks. Script-removed herbs stay removed. Samples uniformly per plant. Equal bounds fix the duration; (0, 0) disables timed regrowth while manual respawn remains available. Applies to every virtual world.",
                "True on success; false for non-finite, reversed or out-of-range bounds."));
        gathering.function(
            "resetRespawnTime",
            []() {
                Server::Get()->GetGathering().ResetRespawnTime();
            },
            v8pp::metadata::docs("void", {}, "Restores five-times-faster species defaults and restarts active picked-herb timers from now. Script-removed herbs stay removed."));
        gathering.function("respawn", &JS_Respawn,
            v8pp::metadata::docs("number",
                {
                    v8pp::metadata::param("position", "Vector3", false, "Centre of the sphere in world metres; finite coordinates within +/-100000."),
                    v8pp::metadata::param("radius", "number", false, "Inclusive 3D radius in metres, from 0 to 100000."),
                    v8pp::metadata::param("virtualWorld", "number", true, "Only this virtual world; defaults to 0."),
                },
                "Respawns picked and script-removed plants in a sphere, including plants with timed regrowth disabled. Restores client vegetation as well as server availability. Does not create new plants.",
                "Number of active picked or script-removed sources restored. Invalid arguments throw."));
        gathering.function("respawnAll", &JS_RespawnAll,
            v8pp::metadata::docs("number",
                {
                    v8pp::metadata::param("virtualWorld", "number", true, "Only this virtual world; defaults to 0."),
                },
                "Respawns every picked or script-removed plant in one virtual world, including plants with timed regrowth disabled.", "Number of active picked or script-removed sources restored. Invalid arguments throw."));
        global->Set(isolate->GetCurrentContext(), v8pp::to_v8(isolate, "Gathering"), gathering.new_instance()).Check();
        auto &proposal = ServerCatalog().data_type("GatheringProposal", "A plant a player picked and what it is about to give them.");
        proposal.add_property("item", "string", "Item class GUID of the herb.");
        proposal.add_property("amount", "number", "How many, as the game computes it from the player's survival level and the plants harvested together.");
        proposal.add_property("kind", "number", "The game's pickable area id for the plant.");
        proposal.add_property("position", "number[]", "Where the plant grows, as the player's game reported it.");
        proposal.add_property("virtualWorld", "number", "The player's virtual world.");

        auto &harvested = ServerCatalog().data_type("GatheringHarvestedEvent", "A plant a player picked, and what they were given.");
        harvested.add_property("item", "string", "Item class GUID of the herb.");
        harvested.add_property("amount", "number", "How many.");
        harvested.add_property("kind", "number", "The game's pickable area id for the plant.");
        harvested.add_property("position", "number[]", "Where the plant grows.");
        harvested.add_property("virtualWorld", "number", "The player's virtual world.");
        harvested.add_property("items", "InventoryUnit[]", "The inventory rows that received the herb.");

        auto &events = ServerCatalog().data_type("EventMap");
        events.add_property("gatheringHarvest", "[player: Player, proposal: GatheringProposal]",
            "A player picked a plant within their reach, and the server is about to give them the herb. Return false to refuse it: nothing is given and the plants stay. The proposal is frozen; every handler runs whatever an earlier one returned, and an async handler cannot refuse.");
        events.add_property("gatheringHarvested", "[player: Player, event: GatheringHarvestedEvent]",
            "A player was given a herb, after the `playerInventoryChanged` it caused. The plant and the same-kind plants harvested with it are picked for everyone in the virtual world until timed or scripted regrowth; nothing about them survives a restart.");
    }
} // namespace KCDC::Scripting
