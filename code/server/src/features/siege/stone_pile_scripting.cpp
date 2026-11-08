#include "stone_pile_scripting.h"

#include "core/scripting/script_context.h"
#include "core/scripting/wrap.h"

#include "features/siege/stone_pile_service.h"

#include "shared/scripting/catalog.h"
#include "shared/scripting/rotation_arg.h"
#include "shared/scripting/virtual_world_arg.h"

#include <scripting/v8pp_types.h>

#include <v8pp/convert.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <sstream>
#include <vector>

namespace KCDC::Scripting {

    namespace {
        StonePile *Self(const v8::FunctionCallbackInfo<v8::Value> &info) {
            return v8pp::class_<StonePile>::unwrap_object(info.GetIsolate(), info.This());
        }

        // JS: stonePile.destroy()
        void JS_Destroy(const v8::FunctionCallbackInfo<v8::Value> &info) {
            if (auto *self = Self(info)) {
                KCDC::Features::Siege::StonePileService::Get().Destroy(self->ResolvePile());
            }
        }

        // JS: StonePile.spawn(position, rotation?, virtualWorld?) -> StonePile
        void JS_Spawn(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);
            if (info.Length() < 1 || !info[0]->IsObject()) {
                isolate->ThrowError(v8pp::to_v8(isolate, "StonePile.spawn: expected (position, rotation?, virtualWorld?)"));
                return;
            }
            // Called directly for the reason ParseRotationArg gives.
            const glm::vec3 position                    = v8pp::convert<glm::vec3>::from_v8(isolate, info[0]);
            const glm::quat rotation                    = info.Length() >= 2 ? ParseRotationArg(isolate, info[1], glm::quat(1.0F, 0.0F, 0.0F, 0.0F)) : glm::quat(1.0F, 0.0F, 0.0F, 0.0F);
            const MafiaNet::VirtualWorldId virtualWorld = VirtualWorldArg(info, 2).value_or(MafiaNet::VIRTUAL_WORLD_GLOBAL);

            auto *pile = KCDC::Features::Siege::StonePileService::Get().Spawn(position, rotation, virtualWorld);
            if (pile == nullptr) {
                isolate->ThrowError(v8pp::to_v8(isolate, "StonePile.spawn: failed to create entity"));
                return;
            }
            info.GetReturnValue().Set(WrapStonePile(isolate, pile->GetNetworkID()));
        }

        // JS: StonePile.all(virtualWorld?) -> StonePile[]
        void JS_All(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            v8::Local<v8::Array> piles     = v8::Array::New(isolate);
            std::uint32_t index            = 0;
            for (auto *pile : KCDC::Features::Siege::StonePileService::Collect(VirtualWorldArg(info, 0))) {
                piles->Set(context, index++, WrapStonePile(isolate, pile->GetNetworkID())).Check();
            }
            info.GetReturnValue().Set(piles);
        }

        // JS: StonePile.getById(id) -> StonePile | null
        void JS_GetById(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);
            if (info.Length() < 1 || !info[0]->IsNumber()) {
                isolate->ThrowError(v8pp::to_v8(isolate, "StonePile.getById: expected (id)"));
                return;
            }
            info.GetReturnValue().Set(WrapStonePile(isolate, static_cast<std::uint64_t>(info[0]->IntegerValue(isolate->GetCurrentContext()).FromMaybe(0))));
        }

        // JS: StonePile.destroyAll(virtualWorld?) -> number
        void JS_DestroyAll(const v8::FunctionCallbackInfo<v8::Value> &info) {
            info.GetReturnValue().Set(static_cast<std::uint32_t>(KCDC::Features::Siege::StonePileService::Get().DestroyAll(VirtualWorldArg(info, 0))));
        }

        void EmitPileEvent(const char *eventName, std::uint64_t pileNetworkId) {
            EmitReservedEvent(eventName, [pileNetworkId](v8::Isolate *isolate) {
                std::vector<v8::Local<v8::Value>> args;
                args.push_back(WrapStonePile(isolate, pileNetworkId));
                return args;
            });
        }
    } // namespace

    Shared::Entities::StonePileEntity *StonePile::ResolvePile() const {
        return dynamic_cast<Shared::Entities::StonePileEntity *>(Resolve());
    }

    std::int32_t StonePile::GetStones() const {
        auto *pile = ResolvePile();
        return pile != nullptr ? pile->stones : 0;
    }

    void StonePile::SetStones(std::int32_t stones) {
        if (auto *pile = ResolvePile()) {
            pile->stones = stones < 0 ? Shared::Entities::StonePileEntity::kUnlimitedStones : stones;
        }
    }

    bool StonePile::IsUsable() const {
        auto *pile = ResolvePile();
        return pile != nullptr && pile->usable;
    }

    void StonePile::SetUsable(bool usable) {
        if (auto *pile = ResolvePile()) {
            pile->usable = usable;
        }
    }

    float StonePile::GetDamage() const {
        auto *pile = ResolvePile();
        return pile != nullptr ? pile->damage : 0.0F;
    }

    void StonePile::SetDamage(float damage) {
        auto *pile = ResolvePile();
        if (pile != nullptr && std::isfinite(damage)) {
            pile->damage = std::clamp(damage, 0.0F, Shared::Entities::StonePileEntity::kMaxDamage);
        }
    }

    float StonePile::GetDamageRadius() const {
        auto *pile = ResolvePile();
        return pile != nullptr ? pile->damageRadius : 0.0F;
    }

    void StonePile::SetDamageRadius(float radius) {
        auto *pile = ResolvePile();
        if (pile != nullptr && std::isfinite(radius)) {
            pile->damageRadius = std::clamp(radius, 0.0F, Shared::Entities::StonePileEntity::kMaxDamageRadius);
        }
    }

    std::string StonePile::ToString() const {
        std::ostringstream ss;
        ss << "StonePile{ id: " << GetId() << ", stones: " << GetStones() << " }";
        return ss.str();
    }

    v8pp::class_<StonePile> &StonePile::GetClass(v8::Isolate *isolate) {
        auto &_class = _classes[isolate];
        if (!_class) {
            Framework::Scripting::Builtins::Entity::GetClass(isolate);

            _class = std::make_unique<v8pp::class_<StonePile>>(isolate, ServerCatalog(), "StonePile", "Replicated handle for a pile of hurling stones on a battlement.");
            _class->auto_wrap_objects(true);
            _class->inherit<Framework::Scripting::Builtins::Entity>()
                .ctor<std::uint64_t>(v8pp::metadata::docs("void", {v8pp::metadata::param("id", "number", false, "Network entity identifier.")}, "Creates a script wrapper for an existing stone pile with this ID; use StonePile.spawn() to place one."))
                .function("toString", &StonePile::ToString, v8pp::metadata::docs("string", {}, "Formats this pile handle for logging and debugging.", "The pile ID and the stones it has left."));

            _class->property("stones", &StonePile::GetStones, &StonePile::SetStones,
                v8pp::metadata::property_docs("number", "Stones left to throw; each throw takes one, and a pile with none offers nothing. Players see at most nine in the heap. -1, the default, never runs out; any negative value means the same."));
            _class->property("usable", &StonePile::IsUsable, &StonePile::SetUsable, v8pp::metadata::property_docs("boolean", "Whether players are offered the game's own prompt at the pile. On by default."));
            _class->property("damage", &StonePile::GetDamage, &StonePile::SetDamage,
                v8pp::metadata::property_docs("number", "Health taken from anyone at the point a stone comes down, up to 1000; a quarter of it reaches the edge of `damageRadius`. 60 to start with; a player has 100."));
            _class->property("damageRadius", &StonePile::GetDamageRadius, &StonePile::SetDamageRadius, v8pp::metadata::property_docs("number", "How far from where a stone comes down anyone is hurt, in metres, up to 10. 1.5 to start with."));

            _class->prototype_function("destroy", &JS_Destroy, v8pp::metadata::docs("void", {}, "Removes the pile on every client after emitting `stonePileDestroy`. A stone already thrown still lands."));
        }
        return *_class;
    }

    void StonePile::UnregisterIsolate(v8::Isolate *isolate) {
        _classes.erase(isolate);
    }

    void StonePile::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        if (isolate == nullptr || global.IsEmpty()) {
            return;
        }

        auto &events = ServerCatalog().data_type("EventMap");
        events.add_property("stonePileSpawn", "[pile: StonePile]", "Dispatched immediately after a stone pile is placed.");
        events.add_property("stonePileDestroy", "[pile: StonePile]", "Dispatched while a stone pile is being removed. The handle still resolves.");
        events.add_property("stonePileThrow", "[player: Player, pile: StonePile]",
            "Dispatched when a player takes a stone off a pile. The game's own stone throwing has already lifted it and heaves it over the wall at once, so it cannot be refused; make the pile not `usable` to stop the next one.");
        events.add_property("stoneImpact", "[pile: StonePile | null, position: Vector3, attacker: Player]",
            "Dispatched where a thrown stone first met something, as its thrower saw it, checked to lie within 30 m of its pile. Players within the pile's `damageRadius` have been told to take their share, and NPCs have taken theirs.");

        v8pp::class_<StonePile> &cls = GetClass(isolate);
        cls.static_function("spawn", &JS_Spawn,
            v8pp::metadata::docs("StonePile",
                {
                    v8pp::metadata::param("position", "Vector3 | Partial<Vector3>", false, "Where the thrower stands on the walk, behind the parapet."),
                    v8pp::metadata::param("rotation", "Vector3 | Quaternion", true, "Its facing: forward is the way the stones go, over the wall. A Quaternion, or a Vector3 of Euler angles in degrees."),
                    v8pp::metadata::param("virtualWorld", "number", true, "Optional virtual world the pile belongs to; omitted puts it in the global one."),
                },
                "Places the game's own battlement pile of hurling stones, the Nebakov and Suchdol defenders' heap. Its prompt, the lift and the heave over the wall are the game's stone throwing; each stone is a real rigid body that falls where it falls.",
                "The new pile's handle."));
        cls.static_function("all", &JS_All,
            v8pp::metadata::docs("StonePile[]", {v8pp::metadata::param("virtualWorld", "number", true, "Optional virtual world to list; omitted lists every one.")}, "Lists the stone piles the server has.", "One handle per pile, in no particular order."));
        cls.static_function("getById", &JS_GetById, v8pp::metadata::docs("StonePile | null", {v8pp::metadata::param("id", "number", false, "Network entity identifier.")}, "Looks a pile up by its network entity ID.", "The pile's handle, or null when no live pile has that ID."));
        cls.static_function("destroyAll", &JS_DestroyAll,
            v8pp::metadata::docs("number", {v8pp::metadata::param("virtualWorld", "number", true, "Optional virtual world to clear; omitted clears every one.")}, "Removes stone piles, emitting `stonePileDestroy` for each one.", "How many were removed."));
        cls.publish(global);
    }

    void Events::StonePileSpawned(std::uint64_t pileNetworkId) {
        EmitPileEvent("stonePileSpawn", pileNetworkId);
    }

    void Events::StonePileDestroyed(std::uint64_t pileNetworkId) {
        EmitPileEvent("stonePileDestroy", pileNetworkId);
    }

    void Events::StonePileThrow(std::uint64_t playerNetworkId, std::uint64_t pileNetworkId) {
        EmitReservedEvent("stonePileThrow", [playerNetworkId, pileNetworkId](v8::Isolate *isolate) {
            std::vector<v8::Local<v8::Value>> args;
            args.push_back(WrapPlayer(isolate, playerNetworkId));
            args.push_back(WrapStonePile(isolate, pileNetworkId));
            return args;
        });
    }

    void Events::StoneImpact(std::uint64_t pileNetworkId, std::uint64_t attackerNetworkId, const glm::vec3 &point) {
        EmitReservedEvent("stoneImpact", [pileNetworkId, attackerNetworkId, point](v8::Isolate *isolate) {
            std::vector<v8::Local<v8::Value>> args;
            args.push_back(WrapStonePile(isolate, pileNetworkId));
            args.push_back(v8pp::convert<glm::vec3>::to_v8(isolate, point));
            args.push_back(WrapPlayer(isolate, attackerNetworkId));
            return args;
        });
    }
} // namespace KCDC::Scripting
