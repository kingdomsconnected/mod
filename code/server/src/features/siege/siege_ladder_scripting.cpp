#include "siege_ladder_scripting.h"

#include "core/scripting/player_arg.h"
#include "core/scripting/script_context.h"
#include "core/scripting/wrap.h"

#include "features/siege/siege_ladder_service.h"
#include "features/siege/siege_scripting.h"

#include "shared/game/siege_defence_catalog.h"
#include "shared/scripting/catalog.h"
#include "shared/scripting/rotation_arg.h"
#include "shared/scripting/virtual_world_arg.h"

#include <scripting/v8pp_types.h>

#include <v8pp/convert.hpp>

#include <cstdint>
#include <optional>
#include <sstream>
#include <vector>

namespace KCDC::Scripting {

    namespace {
        // The handle a prototype function was called on, or nullptr.
        SiegeLadder *Self(const v8::FunctionCallbackInfo<v8::Value> &info) {
            return v8pp::class_<SiegeLadder>::unwrap_object(info.GetIsolate(), info.This());
        }

        // The live ladder a prototype function was called on; throws and returns nullptr once it has gone.
        Shared::Entities::SiegeLadderEntity *LiveSelf(const v8::FunctionCallbackInfo<v8::Value> &info, const char *verb) {
            auto *self   = Self(info);
            auto *ladder = self != nullptr ? self->ResolveLadder() : nullptr;
            if (ladder == nullptr) {
                info.GetIsolate()->ThrowError(v8pp::to_v8(info.GetIsolate(), std::string("siegeLadder.") + verb + ": this ladder is no longer on the server"));
            }
            return ladder;
        }

        // JS: siegeLadder.push(player?) -> SiegeResult
        void JS_Push(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);
            if (auto *ladder = LiveSelf(info, "push")) {
                const std::uint64_t pusher = info.Length() >= 1 ? PlayerArg(isolate, info[0]) : 0;
                info.GetReturnValue().Set(SiegeResultObject(isolate, KCDC::Features::Siege::SiegeLadderService::Get().Push(*ladder, pusher)));
            }
        }

        // JS: siegeLadder.raise(player?) -> SiegeResult
        void JS_Raise(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);
            if (auto *ladder = LiveSelf(info, "raise")) {
                const std::uint64_t raiser = info.Length() >= 1 ? PlayerArg(isolate, info[0]) : 0;
                info.GetReturnValue().Set(SiegeResultObject(isolate, KCDC::Features::Siege::SiegeLadderService::Get().Raise(*ladder, raiser)));
            }
        }

        // JS: siegeLadder.break()
        void JS_Break(const v8::FunctionCallbackInfo<v8::Value> &info) {
            if (auto *ladder = LiveSelf(info, "break")) {
                KCDC::Features::Siege::SiegeLadderService::Get().Break(*ladder);
            }
        }

        // JS: siegeLadder.repair()
        void JS_Repair(const v8::FunctionCallbackInfo<v8::Value> &info) {
            if (auto *ladder = LiveSelf(info, "repair")) {
                KCDC::Features::Siege::SiegeLadderService::Get().Repair(*ladder);
            }
        }

        // JS: siegeLadder.destroy()
        void JS_Destroy(const v8::FunctionCallbackInfo<v8::Value> &info) {
            if (auto *self = Self(info)) {
                KCDC::Features::Siege::SiegeLadderService::Get().Destroy(self->ResolveLadder());
            }
        }

        // JS: SiegeLadder.spawn(kind, position, rotation?, virtualWorld?) -> SiegeLadder
        void JS_Spawn(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            if (info.Length() < 2 || !info[0]->IsString() || !info[1]->IsObject()) {
                isolate->ThrowError(v8pp::to_v8(isolate, "SiegeLadder.spawn: expected (kind, position, rotation?, virtualWorld?)"));
                return;
            }
            const std::string kind       = v8pp::from_v8<std::string>(isolate, info[0]);
            const std::size_t assetIndex = Shared::Game::FindSiegeLadderIndex(kind);
            if (assetIndex == Shared::Game::kNoSiegeLadder) {
                isolate->ThrowError(v8pp::to_v8(isolate, "SiegeLadder.spawn: " + kind + " is not a siege ladder; expected siege, tall, short or low"));
                return;
            }
            // Called directly for the reason ParseRotationArg gives.
            const glm::vec3 position                    = v8pp::convert<glm::vec3>::from_v8(isolate, info[1]);
            const glm::quat rotation                    = info.Length() >= 3 ? ParseRotationArg(isolate, info[2], glm::quat(1.0F, 0.0F, 0.0F, 0.0F)) : glm::quat(1.0F, 0.0F, 0.0F, 0.0F);
            const MafiaNet::VirtualWorldId virtualWorld = VirtualWorldArg(info, 3).value_or(MafiaNet::VIRTUAL_WORLD_GLOBAL);

            auto *ladder = KCDC::Features::Siege::SiegeLadderService::Get().Spawn(assetIndex, position, rotation, virtualWorld);
            if (ladder == nullptr) {
                isolate->ThrowError(v8pp::to_v8(isolate, "SiegeLadder.spawn: failed to create entity"));
                return;
            }
            info.GetReturnValue().Set(WrapSiegeLadder(isolate, ladder->GetNetworkID()));
        }

        // JS: SiegeLadder.describe(kind) -> SiegeLadderKind | null
        void JS_Describe(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);
            const std::size_t index = info.Length() >= 1 && info[0]->IsString() ? Shared::Game::FindSiegeLadderIndex(v8pp::from_v8<std::string>(isolate, info[0])) : Shared::Game::kNoSiegeLadder;
            if (index == Shared::Game::kNoSiegeLadder) {
                info.GetReturnValue().SetNull();
                return;
            }
            const Shared::Game::SiegeLadderAsset &asset = Shared::Game::g_SiegeLadderAssets[index];
            v8::Local<v8::Context> context              = isolate->GetCurrentContext();
            v8::Local<v8::Object> answer                = v8::Object::New(isolate);
            answer->Set(context, v8pp::to_v8(isolate, "kind"), v8pp::to_v8(isolate, std::string(asset.name))).Check();
            answer->Set(context, v8pp::to_v8(isolate, "model"), v8pp::to_v8(isolate, std::string(asset.model))).Check();
            answer->Set(context, v8pp::to_v8(isolate, "height"), v8pp::to_v8(isolate, asset.height)).Check();
            answer->Set(context, v8pp::to_v8(isolate, "lean"), v8pp::to_v8(isolate, asset.leanDeg)).Check();
            info.GetReturnValue().Set(answer);
        }

        // JS: SiegeLadder.all(virtualWorld?) -> SiegeLadder[]
        void JS_All(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            v8::Local<v8::Array> ladders   = v8::Array::New(isolate);
            std::uint32_t index            = 0;
            for (auto *ladder : KCDC::Features::Siege::SiegeLadderService::Collect(VirtualWorldArg(info, 0))) {
                ladders->Set(context, index++, WrapSiegeLadder(isolate, ladder->GetNetworkID())).Check();
            }
            info.GetReturnValue().Set(ladders);
        }

        // JS: SiegeLadder.getById(id) -> SiegeLadder | null
        void JS_GetById(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);
            if (info.Length() < 1 || !info[0]->IsNumber()) {
                isolate->ThrowError(v8pp::to_v8(isolate, "SiegeLadder.getById: expected (id)"));
                return;
            }
            info.GetReturnValue().Set(WrapSiegeLadder(isolate, static_cast<std::uint64_t>(info[0]->IntegerValue(isolate->GetCurrentContext()).FromMaybe(0))));
        }

        // JS: SiegeLadder.destroyAll(virtualWorld?) -> number
        void JS_DestroyAll(const v8::FunctionCallbackInfo<v8::Value> &info) {
            info.GetReturnValue().Set(static_cast<std::uint32_t>(KCDC::Features::Siege::SiegeLadderService::Get().DestroyAll(VirtualWorldArg(info, 0))));
        }

        void EmitLadderEvent(const char *eventName, std::uint64_t ladderNetworkId) {
            EmitReservedEvent(eventName, [ladderNetworkId](v8::Isolate *isolate) {
                std::vector<v8::Local<v8::Value>> args;
                args.push_back(WrapSiegeLadder(isolate, ladderNetworkId));
                return args;
            });
        }
    } // namespace

    Shared::Entities::SiegeLadderEntity *SiegeLadder::ResolveLadder() const {
        return dynamic_cast<Shared::Entities::SiegeLadderEntity *>(Resolve());
    }

    std::string SiegeLadder::GetKind() const {
        auto *ladder = ResolveLadder();
        return ladder != nullptr ? std::string(ladder->Asset().name) : std::string();
    }

    std::string SiegeLadder::GetPose() const {
        auto *ladder = ResolveLadder();
        return ladder != nullptr ? std::string(Shared::Entities::SiegeLadderStateName(ladder->state)) : std::string();
    }

    float SiegeLadder::GetHeight() const {
        auto *ladder = ResolveLadder();
        return ladder != nullptr ? ladder->Asset().height : 0.0F;
    }

    bool SiegeLadder::IsClimbable() const {
        auto *ladder = ResolveLadder();
        return ladder != nullptr && ladder->IsClimbable();
    }

    bool SiegeLadder::IsUsable() const {
        auto *ladder = ResolveLadder();
        return ladder != nullptr && ladder->usable;
    }

    void SiegeLadder::SetUsable(bool usable) {
        if (auto *ladder = ResolveLadder()) {
            ladder->usable = usable;
        }
    }

    v8::Local<v8::Value> SiegeLadder::GetPusherHandle(v8::Isolate *isolate) const {
        auto *ladder = ResolveLadder();
        return WrapPlayer(isolate, ladder != nullptr ? ladder->pusherNetworkId : 0);
    }

    std::string SiegeLadder::ToString() const {
        std::ostringstream ss;
        ss << "SiegeLadder{ id: " << GetId() << ", kind: " << GetKind() << ", pose: " << GetPose() << " }";
        return ss.str();
    }

    v8pp::class_<SiegeLadder> &SiegeLadder::GetClass(v8::Isolate *isolate) {
        auto &_class = _classes[isolate];
        if (!_class) {
            // v8pp's inherit<Entity> needs the framework Entity registered first.
            Framework::Scripting::Builtins::Entity::GetClass(isolate);

            _class = std::make_unique<v8pp::class_<SiegeLadder>>(isolate, ServerCatalog(), "SiegeLadder", "Replicated handle for a siege ladder leaning on a wall.");
            _class->auto_wrap_objects(true);
            _class->inherit<Framework::Scripting::Builtins::Entity>()
                .ctor<std::uint64_t>(v8pp::metadata::docs("void", {v8pp::metadata::param("id", "number", false, "Network entity identifier.")}, "Creates a script wrapper for an existing siege ladder with this ID; use SiegeLadder.spawn() to place one."))
                .function("toString", &SiegeLadder::ToString, v8pp::metadata::docs("string", {}, "Formats this ladder handle for logging and debugging.", "The ladder ID, its kind and its pose."));

            _class->property("kind", &SiegeLadder::GetKind, v8pp::metadata::property_docs("string", "Which of the game's ladders it is: `siege` (the Suchdol siege ladder, 9.75 m), `tall` (5.5 m), `short` (3.25 m) or `low` (2 m)."));
            _class->property("pose", &SiegeLadder::GetPose,
                v8pp::metadata::property_docs("string", "`standing` against its wall, `pushed` (a defender's swing, then its fall), `down`, `raising` or `broken`. Named `pose` because every entity already has a `state`, which is its state bag."));
            _class->property("height", &SiegeLadder::GetHeight, v8pp::metadata::property_docs("number", "How far up its rungs go, in metres: its top rests this high above its foot on the wall's top. Pick the kind whose height meets the walk."));
            _class->property("climbable", &SiegeLadder::IsClimbable, v8pp::metadata::property_docs("boolean", "Whether the game's own climb is offered on it: while it stands and nobody has started to push it."));
            _class->property("usable", &SiegeLadder::IsUsable, &SiegeLadder::SetUsable,
                v8pp::metadata::property_docs("boolean",
                    "Whether players are offered the push at its top and the raise at the foot of a fallen one, each raising `siegeLadderUse` first. On by default. The climb is the game's and is offered whenever the ladder stands."));
            _class->property(
                "pusher",
                [](const SiegeLadder &self, v8::Isolate *isolate) {
                    return self.GetPusherHandle(isolate);
                },
                v8pp::metadata::property_docs("Player | null", "Who last pushed it, or null."));

            _class->prototype_function("push", &JS_Push,
                v8pp::metadata::docs("SiegeResult", {v8pp::metadata::param("player", "Player | number | null", true, "Who pushes it: they play the game's own halberd push, and are credited with it in `siegeLadderFall`.")},
                    "Pushes a standing ladder off its wall. It stands through the swing, falls away from the wall as the blade meets it -- throwing off anyone climbing it -- and lies where it lands, about 4.5 seconds later.",
                    "What happened, and the phrase to explain it with when nothing did."));
            _class->prototype_function("raise", &JS_Raise,
                v8pp::metadata::docs("SiegeResult", {v8pp::metadata::param("player", "Player | number | null", true, "Who raises it: they play the game's own ladder lift.")}, "Lays a fallen ladder back against its wall, over the lift's 5.7 seconds.",
                    "What happened, and the phrase to explain it with when nothing did."));
            _class->prototype_function("break", &JS_Break, v8pp::metadata::docs("void", {}, "Breaks the ladder: it lies in pieces where it stands, anyone on it falls, and nothing raises it until `repair`."));
            _class->prototype_function("repair", &JS_Repair, v8pp::metadata::docs("void", {}, "Puts a fallen or broken ladder back against its wall at once."));
            _class->prototype_function("destroy", &JS_Destroy, v8pp::metadata::docs("void", {}, "Removes the ladder on every client after emitting `siegeLadderDestroy`."));
        }
        return *_class;
    }

    void SiegeLadder::UnregisterIsolate(v8::Isolate *isolate) {
        _classes.erase(isolate);
    }

    void SiegeLadder::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        if (isolate == nullptr || global.IsEmpty()) {
            return;
        }

        auto &kind = ServerCatalog().data_type("SiegeLadderKind");
        kind.add_property("kind", "string", "Its name: `siege`, `tall`, `short` or `low`.");
        kind.add_property("model", "string", "The mesh it stands as, a path from the shared prop catalog.");
        kind.add_property("height", "number", "How far up its rungs go, in metres.");
        kind.add_property("lean", "number", "How far it leans towards the wall standing, in degrees about its own X; it leans towards its forward.");

        auto &events = ServerCatalog().data_type("EventMap");
        events.add_property("siegeLadderSpawn", "[ladder: SiegeLadder]", "Dispatched immediately after a siege ladder is placed.");
        events.add_property("siegeLadderDestroy", "[ladder: SiegeLadder]", "Dispatched while a siege ladder is being removed. The handle still resolves.");
        events.add_property("siegeLadderUse", "[player: Player, ladder: SiegeLadder, action: \"push\" | \"raise\"]",
            "Dispatched when a player asks to push a `usable` ladder from the walk at its top, or to raise a fallen one from beside its foot. Return false to refuse.");
        events.add_property("siegeLadderFall", "[ladder: SiegeLadder, pusher: Player | null]",
            "Dispatched when a pushed ladder hits the ground. Anyone who was on it was thrown off as it began to fall, and took the game's own fall.");
        events.add_property("siegeLadderRaise", "[ladder: SiegeLadder]", "Dispatched when a raised ladder leans on its wall again and can be climbed.");

        v8pp::class_<SiegeLadder> &cls = GetClass(isolate);
        cls.static_function("spawn", &JS_Spawn,
            v8pp::metadata::docs("SiegeLadder",
                {
                    v8pp::metadata::param("kind", "string", false, "`siege`, `tall`, `short` or `low`."),
                    v8pp::metadata::param("position", "Vector3 | Partial<Vector3>", false, "Where its foot stands, on the ground below the wall."),
                    v8pp::metadata::param("rotation", "Vector3 | Quaternion", true, "Its facing: forward is the way to the wall it leans on. A Quaternion, or a Vector3 of Euler angles in degrees."),
                    v8pp::metadata::param("virtualWorld", "number", true, "Optional virtual world the ladder belongs to; omitted puts it in the global one."),
                },
                "Places one of the game's own ladders leaning on a wall, the way the Suchdol and Nebakov sieges stand theirs: the game's climb, and the game's step off over the palisade at its top, which wants the top a little over the walk.",
                "The new ladder's handle."));
        cls.static_function("describe", &JS_Describe,
            v8pp::metadata::docs("SiegeLadderKind | null", {v8pp::metadata::param("kind", "string", false, "`siege`, `tall`, `short` or `low`.")},
                "What one kind of ladder is: its mesh, its climb and its lean -- what a placement preview needs to show it as it will stand.", "The kind, or null for a name that is not one."));
        cls.static_function("all", &JS_All,
            v8pp::metadata::docs("SiegeLadder[]", {v8pp::metadata::param("virtualWorld", "number", true, "Optional virtual world to list; omitted lists every one.")}, "Lists the siege ladders the server has.", "One handle per ladder, in no particular order."));
        cls.static_function("getById", &JS_GetById, v8pp::metadata::docs("SiegeLadder | null", {v8pp::metadata::param("id", "number", false, "Network entity identifier.")}, "Looks a ladder up by its network entity ID.", "The ladder's handle, or null when no live ladder has that ID."));
        cls.static_function("destroyAll", &JS_DestroyAll,
            v8pp::metadata::docs("number", {v8pp::metadata::param("virtualWorld", "number", true, "Optional virtual world to clear; omitted clears every one.")}, "Removes siege ladders, emitting `siegeLadderDestroy` for each one.", "How many were removed."));
        cls.publish(global);
    }

    void Events::SiegeLadderSpawned(std::uint64_t ladderNetworkId) {
        EmitLadderEvent("siegeLadderSpawn", ladderNetworkId);
    }

    void Events::SiegeLadderDestroyed(std::uint64_t ladderNetworkId) {
        EmitLadderEvent("siegeLadderDestroy", ladderNetworkId);
    }

    bool Events::SiegeLadderUse(std::uint64_t playerNetworkId, std::uint64_t ladderNetworkId, const char *action) {
        return EmitReservedEventSync("siegeLadderUse", [playerNetworkId, ladderNetworkId, action](v8::Isolate *isolate) {
            std::vector<v8::Local<v8::Value>> args;
            args.push_back(WrapPlayer(isolate, playerNetworkId));
            args.push_back(WrapSiegeLadder(isolate, ladderNetworkId));
            args.push_back(v8pp::to_v8(isolate, std::string(action)));
            return args;
        });
    }

    void Events::SiegeLadderFell(std::uint64_t ladderNetworkId, std::uint64_t pusherNetworkId) {
        EmitReservedEvent("siegeLadderFall", [ladderNetworkId, pusherNetworkId](v8::Isolate *isolate) {
            std::vector<v8::Local<v8::Value>> args;
            args.push_back(WrapSiegeLadder(isolate, ladderNetworkId));
            args.push_back(WrapPlayer(isolate, pusherNetworkId));
            return args;
        });
    }

    void Events::SiegeLadderRaised(std::uint64_t ladderNetworkId) {
        EmitLadderEvent("siegeLadderRaise", ladderNetworkId);
    }
} // namespace KCDC::Scripting
