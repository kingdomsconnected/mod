#include "siege_scripting.h"

#include "core/scripting/player_arg.h"
#include "core/scripting/script_context.h"
#include "core/scripting/wrap.h"

#include "features/siege/siege_service.h"

#include "shared/features/siege/siege_ballistics.h"
#include "shared/game/siege_catalog.h"
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
    v8::Local<v8::Object> SiegeResultObject(v8::Isolate *isolate, const KCDC::Features::Siege::SiegeResult &result) {
        v8::Local<v8::Context> context = isolate->GetCurrentContext();
        v8::Local<v8::Object> answer   = v8::Object::New(isolate);
        answer->Set(context, v8pp::to_v8(isolate, "accepted"), v8pp::to_v8(isolate, result.accepted)).Check();
        answer->Set(context, v8pp::to_v8(isolate, "reason"), v8pp::to_v8(isolate, result.reason)).Check();
        answer->Set(context, v8pp::to_v8(isolate, "seconds"), v8pp::to_v8(isolate, result.seconds)).Check();
        return answer;
    }

    namespace {
        // The handle a prototype function was called on, or nullptr.
        SiegeEngine *Self(const v8::FunctionCallbackInfo<v8::Value> &info) {
            return v8pp::class_<SiegeEngine>::unwrap_object(info.GetIsolate(), info.This());
        }

        // The live engine a prototype function was called on; throws and returns nullptr once it has gone.
        Shared::Entities::SiegeEngineEntity *LiveSelf(const v8::FunctionCallbackInfo<v8::Value> &info, const char *verb) {
            auto *self   = Self(info);
            auto *engine = self != nullptr ? self->ResolveEngine() : nullptr;
            if (engine == nullptr) {
                info.GetIsolate()->ThrowError(v8pp::to_v8(info.GetIsolate(), std::string("siegeEngine.") + verb + ": this engine is no longer on the server"));
            }
            return engine;
        }

        // A point argument, or nothing when it is not an object.
        std::optional<glm::vec3> PointArg(const v8::FunctionCallbackInfo<v8::Value> &info, int index) {
            if (info.Length() <= index || !info[index]->IsObject()) {
                return std::nullopt;
            }
            // Called directly for the reason ParseRotationArg gives.
            return v8pp::convert<glm::vec3>::from_v8(info.GetIsolate(), info[index]);
        }

        // JS: siegeEngine.load() -> SiegeResult
        void JS_Load(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::HandleScope handleScope(info.GetIsolate());
            if (auto *engine = LiveSelf(info, "load")) {
                info.GetReturnValue().Set(SiegeResultObject(info.GetIsolate(), KCDC::Features::Siege::SiegeService::Get().Load(*engine)));
            }
        }

        // JS: siegeEngine.fire(target, attacker?) -> SiegeResult
        void JS_Fire(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);
            auto *engine = LiveSelf(info, "fire");
            if (engine == nullptr) {
                return;
            }
            const std::optional<glm::vec3> target = PointArg(info, 0);
            if (!target) {
                isolate->ThrowError(v8pp::to_v8(isolate, "siegeEngine.fire: expected (target, attacker?)"));
                return;
            }
            const std::uint64_t attacker = info.Length() >= 2 ? PlayerArg(isolate, info[1]) : 0;
            info.GetReturnValue().Set(SiegeResultObject(isolate, KCDC::Features::Siege::SiegeService::Get().Fire(*engine, *target, attacker)));
        }

        // JS: siegeEngine.aim(target) -> SiegeResult
        void JS_Aim(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);
            auto *engine = LiveSelf(info, "aim");
            if (engine == nullptr) {
                return;
            }
            const std::optional<glm::vec3> target = PointArg(info, 0);
            if (!target) {
                isolate->ThrowError(v8pp::to_v8(isolate, "siegeEngine.aim: expected (target)"));
                return;
            }
            info.GetReturnValue().Set(SiegeResultObject(isolate, KCDC::Features::Siege::SiegeService::Get().Aim(*engine, *target)));
        }

        // JS: siegeEngine.canHit(target) -> string
        void JS_CanHit(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);
            auto *engine = LiveSelf(info, "canHit");
            if (engine == nullptr) {
                return;
            }
            const std::optional<glm::vec3> target = PointArg(info, 0);
            if (!target) {
                isolate->ThrowError(v8pp::to_v8(isolate, "siegeEngine.canHit: expected (target)"));
                return;
            }
            info.GetReturnValue().Set(v8pp::to_v8(isolate, std::string(Shared::Game::Siege::AimResultReason(KCDC::Features::Siege::SiegeService::CanHit(*engine, *target)))));
        }

        // JS: siegeEngine.repair()
        void JS_Repair(const v8::FunctionCallbackInfo<v8::Value> &info) {
            if (auto *engine = LiveSelf(info, "repair")) {
                KCDC::Features::Siege::SiegeService::Get().Repair(*engine);
            }
        }

        // JS: siegeEngine.setOperator(player | null) -> boolean
        void JS_SetOperator(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);
            auto *engine = LiveSelf(info, "setOperator");
            if (engine == nullptr) {
                return;
            }
            const bool asked           = info.Length() >= 1 && !info[0]->IsNullOrUndefined();
            const std::uint64_t player = asked ? PlayerArg(isolate, info[0]) : 0;
            if (asked && player == 0) {
                isolate->ThrowError(v8pp::to_v8(isolate, "siegeEngine.setOperator: expected (player | null)"));
                return;
            }
            info.GetReturnValue().Set(KCDC::Features::Siege::SiegeService::Get().SetOperator(*engine, player));
        }

        // JS: siegeEngine.destroy()
        void JS_Destroy(const v8::FunctionCallbackInfo<v8::Value> &info) {
            if (auto *self = Self(info)) {
                KCDC::Features::Siege::SiegeService::Get().Destroy(self->ResolveEngine());
            }
        }

        // JS: SiegeEngine.spawn(kind, position, rotation?, virtualWorld?) -> SiegeEngine
        void JS_Spawn(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            if (info.Length() < 2 || !info[0]->IsString() || !info[1]->IsObject()) {
                isolate->ThrowError(v8pp::to_v8(isolate, "SiegeEngine.spawn: expected (kind, position, rotation?, virtualWorld?)"));
                return;
            }

            const std::string kind       = v8pp::from_v8<std::string>(isolate, info[0]);
            const std::size_t assetIndex = Shared::Game::FindSiegeEngineIndex(kind);
            if (assetIndex == Shared::Game::kNoSiegeEngine) {
                isolate->ThrowError(v8pp::to_v8(isolate, "SiegeEngine.spawn: " + kind + " is not a siege engine; expected trebuchet or cannon"));
                return;
            }

            const glm::vec3 position = *PointArg(info, 1);
            const glm::quat rotation = info.Length() >= 3 ? ParseRotationArg(isolate, info[2], glm::quat(1.0F, 0.0F, 0.0F, 0.0F)) : glm::quat(1.0F, 0.0F, 0.0F, 0.0F);
            const MafiaNet::VirtualWorldId virtualWorld = VirtualWorldArg(info, 3).value_or(MafiaNet::VIRTUAL_WORLD_GLOBAL);

            auto *engine = KCDC::Features::Siege::SiegeService::Get().Spawn(assetIndex, position, rotation, virtualWorld);
            if (engine == nullptr) {
                isolate->ThrowError(v8pp::to_v8(isolate, "SiegeEngine.spawn: failed to create entity"));
                return;
            }
            info.GetReturnValue().Set(WrapSiegeEngine(isolate, engine->GetNetworkID()));
        }

        // JS: SiegeEngine.all(virtualWorld?) -> SiegeEngine[]
        void JS_All(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            v8::Local<v8::Array> engines   = v8::Array::New(isolate);

            std::uint32_t index = 0;
            for (auto *engine : KCDC::Features::Siege::SiegeService::Collect(VirtualWorldArg(info, 0))) {
                engines->Set(context, index++, WrapSiegeEngine(isolate, engine->GetNetworkID())).Check();
            }
            info.GetReturnValue().Set(engines);
        }

        // JS: SiegeEngine.getById(id) -> SiegeEngine | null
        void JS_GetById(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);
            if (info.Length() < 1 || !info[0]->IsNumber()) {
                isolate->ThrowError(v8pp::to_v8(isolate, "SiegeEngine.getById: expected (id)"));
                return;
            }
            info.GetReturnValue().Set(WrapSiegeEngine(isolate, static_cast<std::uint64_t>(info[0]->IntegerValue(isolate->GetCurrentContext()).FromMaybe(0))));
        }

        // JS: SiegeEngine.destroyAll(virtualWorld?) -> number
        void JS_DestroyAll(const v8::FunctionCallbackInfo<v8::Value> &info) {
            info.GetReturnValue().Set(static_cast<std::uint32_t>(KCDC::Features::Siege::SiegeService::Get().DestroyAll(VirtualWorldArg(info, 0))));
        }

        // The shape of the engine-only events: one subject, nothing else.
        void EmitEngineEvent(const char *eventName, std::uint64_t engineNetworkId) {
            EmitReservedEvent(eventName, [engineNetworkId](v8::Isolate *isolate) {
                std::vector<v8::Local<v8::Value>> args;
                args.push_back(WrapSiegeEngine(isolate, engineNetworkId));
                return args;
            });
        }
    } // namespace

    Shared::Entities::SiegeEngineEntity *SiegeEngine::ResolveEngine() const {
        return dynamic_cast<Shared::Entities::SiegeEngineEntity *>(Resolve());
    }

    std::string SiegeEngine::GetKind() const {
        auto *engine = ResolveEngine();
        return engine != nullptr ? std::string(engine->Asset().name) : std::string();
    }

    std::string SiegeEngine::GetCycle() const {
        auto *engine = ResolveEngine();
        return engine != nullptr ? std::string(Shared::Entities::SiegeStateName(engine->state)) : std::string();
    }

    bool SiegeEngine::IsLoaded() const {
        auto *engine = ResolveEngine();
        return engine != nullptr && engine->state == Shared::Entities::SiegeState::Ready;
    }

    float SiegeEngine::GetSpeed() const {
        auto *engine = ResolveEngine();
        return engine != nullptr ? engine->speed : 0.0F;
    }

    void SiegeEngine::SetSpeed(float speed) {
        if (auto *engine = ResolveEngine()) {
            KCDC::Features::Siege::SiegeService::Get().SetSpeed(*engine, speed);
        }
    }

    float SiegeEngine::GetDamage() const {
        auto *engine = ResolveEngine();
        return engine != nullptr ? KCDC::Features::Siege::SiegeService::Get().GetDamage(*engine) : 0.0F;
    }

    void SiegeEngine::SetDamage(float damage) {
        if (auto *engine = ResolveEngine()) {
            KCDC::Features::Siege::SiegeService::Get().SetDamage(*engine, damage);
        }
    }

    float SiegeEngine::GetDamageRadius() const {
        auto *engine = ResolveEngine();
        return engine != nullptr ? KCDC::Features::Siege::SiegeService::Get().GetDamageRadius(*engine) : 0.0F;
    }

    void SiegeEngine::SetDamageRadius(float radius) {
        if (auto *engine = ResolveEngine()) {
            KCDC::Features::Siege::SiegeService::Get().SetDamageRadius(*engine, radius);
        }
    }

    bool SiegeEngine::IsUsable() const {
        auto *engine = ResolveEngine();
        return engine != nullptr && engine->usable;
    }

    void SiegeEngine::SetUsable(bool usable) {
        if (auto *engine = ResolveEngine()) {
            engine->usable = usable;
        }
    }

    bool SiegeEngine::HasAimGuide() const {
        auto *engine = ResolveEngine();
        return engine != nullptr && engine->aimGuide;
    }

    void SiegeEngine::SetAimGuide(bool aimGuide) {
        if (auto *engine = ResolveEngine()) {
            engine->aimGuide = aimGuide;
        }
    }

    float SiegeEngine::GetRange() const {
        auto *engine = ResolveEngine();
        return engine != nullptr ? KCDC::Features::Siege::SiegeService::Get().GetRange(*engine) : 0.0F;
    }

    void SiegeEngine::SetRange(float range) {
        if (auto *engine = ResolveEngine()) {
            KCDC::Features::Siege::SiegeService::Get().SetRange(*engine, range);
        }
    }

    float SiegeEngine::GetMinRange() const {
        auto *engine = ResolveEngine();
        return engine != nullptr ? engine->Asset().minRange : 0.0F;
    }

    float SiegeEngine::GetMaxRange() const {
        auto *engine = ResolveEngine();
        return engine != nullptr ? engine->Asset().maxRange : 0.0F;
    }

    float SiegeEngine::GetLoadDuration() const {
        auto *engine = ResolveEngine();
        return engine != nullptr ? static_cast<float>(engine->StepDurationMs(Shared::Game::SiegeStep::Draw) + engine->StepDurationMs(Shared::Game::SiegeStep::Load)) / 1000.0F : 0.0F;
    }

    float SiegeEngine::GetMaxHealth() const {
        auto *engine = ResolveEngine();
        return engine != nullptr ? engine->maxHealth : 0.0F;
    }

    void SiegeEngine::SetMaxHealth(float maxHealth) {
        if (auto *engine = ResolveEngine()) {
            KCDC::Features::Siege::SiegeService::Get().SetMaxHealth(*engine, maxHealth);
        }
    }

    float SiegeEngine::GetHealth() const {
        auto *engine = ResolveEngine();
        return engine != nullptr ? engine->health : 0.0F;
    }

    void SiegeEngine::SetHealth(float health) {
        if (auto *engine = ResolveEngine()) {
            KCDC::Features::Siege::SiegeService::Get().SetHealth(*engine, health);
        }
    }

    bool SiegeEngine::IsWrecked() const {
        auto *engine = ResolveEngine();
        return engine != nullptr && engine->state == Shared::Entities::SiegeState::Wrecked;
    }

    std::int32_t SiegeEngine::GetAmmo() const {
        auto *engine = ResolveEngine();
        return engine != nullptr ? engine->ammo : 0;
    }

    void SiegeEngine::SetAmmo(std::int32_t ammo) {
        if (auto *engine = ResolveEngine()) {
            KCDC::Features::Siege::SiegeService::Get().SetAmmo(*engine, ammo);
        }
    }

    std::uint32_t SiegeEngine::GetCrewRequired() const {
        auto *engine = ResolveEngine();
        return engine != nullptr ? engine->crewRequired : 0;
    }

    void SiegeEngine::SetCrewRequired(std::uint32_t required) {
        if (auto *engine = ResolveEngine()) {
            KCDC::Features::Siege::SiegeService::Get().SetCrewRequired(*engine, static_cast<std::uint8_t>(std::min<std::uint32_t>(required, 255)));
        }
    }

    std::uint32_t SiegeEngine::GetCrewPlaces() const {
        auto *engine = ResolveEngine();
        return engine != nullptr ? static_cast<std::uint32_t>(engine->CrewPlaces()) : 0;
    }

    v8::Local<v8::Value> SiegeEngine::GetOperatorHandle(v8::Isolate *isolate) const {
        auto *engine = ResolveEngine();
        return WrapPlayer(isolate, engine != nullptr ? engine->operatorNetworkId : 0);
    }

    v8::Local<v8::Value> SiegeEngine::GetCrewHandles(v8::Isolate *isolate) const {
        v8::Local<v8::Context> context = isolate->GetCurrentContext();
        v8::Local<v8::Array> crew      = v8::Array::New(isolate);
        if (auto *engine = ResolveEngine()) {
            for (std::size_t place = 0; place < engine->CrewPlaces(); ++place) {
                crew->Set(context, static_cast<std::uint32_t>(place), WrapPlayer(isolate, engine->crewNetworkIds[place])).Check();
            }
        }
        return crew;
    }

    std::string SiegeEngine::ToString() const {
        std::ostringstream ss;
        ss << "SiegeEngine{ id: " << GetId() << ", kind: " << GetKind() << ", cycle: " << GetCycle() << " }";
        return ss.str();
    }

    v8pp::class_<SiegeEngine> &SiegeEngine::GetClass(v8::Isolate *isolate) {
        auto &_class = _classes[isolate];
        if (!_class) {
            // v8pp's inherit<Entity> needs the framework Entity registered first.
            Framework::Scripting::Builtins::Entity::GetClass(isolate);

            _class = std::make_unique<v8pp::class_<SiegeEngine>>(isolate, ServerCatalog(), "SiegeEngine", "Replicated handle for a trebuchet or a cannon.");
            _class->auto_wrap_objects(true);
            _class->inherit<Framework::Scripting::Builtins::Entity>()
                .ctor<std::uint64_t>(v8pp::metadata::docs("void", {v8pp::metadata::param("id", "number", false, "Network entity identifier.")}, "Creates a script wrapper for an existing siege engine with this ID; use SiegeEngine.spawn() to build one."))
                .function("toString", &SiegeEngine::ToString, v8pp::metadata::docs("string", {}, "Formats this engine handle for logging and debugging.", "The engine ID, its kind and where it is in its cycle."));

            _class->property("kind", &SiegeEngine::GetKind, v8pp::metadata::property_docs("string", "What the engine is: `trebuchet` or `cannon`. The game ships no catapult and no ballista."));
            _class->property("cycle", &SiegeEngine::GetCycle,
                v8pp::metadata::property_docs("string",
                    "Where the engine is in its cycle: `idle` (unloaded), `drawing` (a trebuchet's arm being winched down), `loading` (the crew laying the projectile), `ready` or `firing`. Named `cycle` because every entity already has a `state`, which is its state bag."));
            _class->property("loaded", &SiegeEngine::IsLoaded, v8pp::metadata::property_docs("boolean", "Whether the engine is ready to fire."));
            _class->property("speed", &SiegeEngine::GetSpeed, &SiegeEngine::SetSpeed,
                v8pp::metadata::property_docs("number",
                    "How fast the cycle runs, from 0.1 to 20; 1 is the game's own pace, which takes a trebuchet's crew nearly two minutes to load and lets the stone go 3.5 seconds into the shot. Changing it mid-step carries on from where the engine has got to."));
            _class->property("damage", &SiegeEngine::GetDamage, &SiegeEngine::SetDamage,
                v8pp::metadata::property_docs("number", "Health taken from anything at the point of impact; a player has 100. A quarter of it reaches the edge of `damageRadius`. 0 leaves hits to `siegeImpact`."));
            _class->property("damageRadius", &SiegeEngine::GetDamageRadius, &SiegeEngine::SetDamageRadius, v8pp::metadata::property_docs("number", "How far from the point of impact anything is hurt, in metres, up to 100."));
            _class->property("usable", &SiegeEngine::IsUsable, &SiegeEngine::SetUsable,
                v8pp::metadata::property_docs("boolean",
                    "Whether players may work the engine. A player within 2 m of an empty one gets the game's own action hint on the use key to take its place, and becomes its `operator`; near a trebuchet with a `crewRequired`, a free winch is offered too. Each ask raises `siegeEngineUse` first, which may refuse it. Off by default."));
            _class->property("range", &SiegeEngine::GetRange, &SiegeEngine::SetRange,
                v8pp::metadata::property_docs("number", "How far a shot goes along the operator's facing when they fire before laying the engine anywhere, in metres; kept between `minRange` and `maxRange`. 150 for a trebuchet and 120 for a cannon to start with."));
            _class->property("minRange", &SiegeEngine::GetMinRange, v8pp::metadata::property_docs("number", "The closest the engine can be aimed, in metres along the ground."));
            _class->property("maxRange", &SiegeEngine::GetMaxRange, v8pp::metadata::property_docs("number", "The furthest the engine can be aimed, in metres along the ground."));
            _class->property("loadDuration", &SiegeEngine::GetLoadDuration, v8pp::metadata::property_docs("number", "How long `load` takes at the engine's speed, in seconds."));
            _class->property("maxHealth", &SiegeEngine::GetMaxHealth, &SiegeEngine::SetMaxHealth,
                v8pp::metadata::property_docs("number",
                    "What the engine can take before it is wrecked; 0, the default, means nothing harms it. Setting it heals the engine to the new maximum and repairs a wreck. A blast within `damageRadius` of an engine -- any engine's, its own included -- takes its share of that shot's `damage` off it, raising `siegeEngineDamage`."));
            _class->property("health", &SiegeEngine::GetHealth, &SiegeEngine::SetHealth,
                v8pp::metadata::property_docs("number", "What is left of `maxHealth`. Setting it to 0 wrecks the engine and raising it from 0 repairs one; ignored while `maxHealth` is 0."));
            _class->property("wrecked", &SiegeEngine::IsWrecked,
                v8pp::metadata::property_docs("boolean",
                    "Whether the engine's health ran out. A wrecked cannon shows the game's broken gun and a wrecked trebuchet leans and settles; nothing loads, aims or fires it, and its operator and crew are let go, until `repair` or a new `maxHealth`."));
            _class->property("ammo", &SiegeEngine::GetAmmo, &SiegeEngine::SetAmmo,
                v8pp::metadata::property_docs("number", "Shots left to load; each `load` takes one, and an engine with none refuses to load. -1, the default, never runs out; any negative value means the same."));
            _class->property("crewRequired", &SiegeEngine::GetCrewRequired, &SiegeEngine::SetCrewRequired,
                v8pp::metadata::property_docs("number",
                    "How many of the engine's winches must be manned for its arm to be winched down; 0, the default, winds it by itself. With fewer hands than this the winding holds where it is, and goes on when they come back. Players near a usable engine that needs them are offered its winches, and each one there plays the game's own pull. Kept to `crewPlaces`."));
            _class->property("crewPlaces", &SiegeEngine::GetCrewPlaces, v8pp::metadata::property_docs("number", "How many winches the engine has: 2 on a trebuchet, 0 on a cannon."));
            _class->property(
                "operator",
                [](const SiegeEngine &self, v8::Isolate *isolate) {
                    return self.GetOperatorHandle(isolate);
                },
                v8pp::metadata::property_docs("Player | null",
                    "The player in the engine's place, or null. They lay it where they look while they hold the block key, load it with the torch key, fire it with the attack key and step away with the use key; walking off lets go of it too. `setOperator` puts somebody there."));
            _class->property(
                "crew",
                [](const SiegeEngine &self, v8::Isolate *isolate) {
                    return self.GetCrewHandles(isolate);
                },
                v8pp::metadata::property_docs("(Player | null)[]", "Who mans each winch, in place order, with null for an empty one."));

            _class->prototype_function("load", &JS_Load, v8pp::metadata::docs("SiegeResult", {}, "Starts the reload cycle of an idle engine. `siegeEngineReady` fires when it can shoot.", "What happened, and the phrase to explain it with when nothing did."));
            _class->prototype_function("fire", &JS_Fire,
                v8pp::metadata::docs("SiegeResult",
                    {
                        v8pp::metadata::param("target", "Vector3 | Partial<Vector3>", false, "Where the projectile should come down."),
                        v8pp::metadata::param("attacker", "Player | number | null", true, "The player credited with whatever it hits, in `playerDamage`, `npcDamage` and `siegeImpact`."),
                    },
                    "Turns a loaded engine to face the target and shoots at it. A trebuchet's arm lets the stone go at one angle, so its speed is solved for the distance; a cannon fires at one speed, so its elevation is. The stone is let go partway through the shot (`seconds`), then every client nearby draws it along the same arc. It comes down where the world stops it, which the attacker is asked to report when near enough, else the player nearest the target, or on the target when nobody can.",
                    "What happened, and the phrase to explain it with when nothing did."));
            _class->prototype_function("aim", &JS_Aim,
                v8pp::metadata::docs("SiegeResult", {v8pp::metadata::param("target", "Vector3 | Partial<Vector3>", false, "The point to turn towards.")}, "Turns the engine to face a point without shooting. `fire` turns it anyway; this is for showing where it will shoot.",
                    "What happened, and the phrase to explain it with when nothing did."));
            _class->prototype_function("canHit", &JS_CanHit,
                v8pp::metadata::docs("string", {v8pp::metadata::param("target", "Vector3 | Partial<Vector3>", false, "The point to test.")}, "Whether the engine can reach a point from where it stands.", "Empty when it can; otherwise why not, as a phrase."));
            _class->function("setAimGuide", &SiegeEngine::SetAimGuide,
                v8pp::metadata::docs("void", {v8pp::metadata::param("enabled", "boolean", false, "Whether the operator sees the beam.")},
                    "Shows or hides the operator's aim guide: a translucent red beam along the shot's path -- out of a trebuchet's sling and along its arc, or straight from a cannon's muzzle -- to where it would come down, with a ball there. Off by default, which leaves laying the engine to the eye; the Fire hint says how far the shot goes either way."));
            _class->function("getAimGuide", &SiegeEngine::HasAimGuide, v8pp::metadata::docs("boolean", {}, "Whether the operator sees the aim guide; see `setAimGuide`.", "True when the beam is shown."));
            _class->prototype_function("repair", &JS_Repair, v8pp::metadata::docs("void", {}, "Makes the engine whole again: `health` back to `maxHealth`, and a wreck idle and unloaded."));
            _class->prototype_function("setOperator", &JS_SetOperator,
                v8pp::metadata::docs("boolean", {v8pp::metadata::param("player", "Player | null", false, "Who takes the engine's place, or null to empty it.")},
                    "Puts a player in the engine's place without asking them, or takes whoever is there out of it. A player holds one place at a time, so they leave any other engine or winch first. Raises no `siegeEngineUse`.", "False for a player who is no longer connected."));
            _class->prototype_function("destroy", &JS_Destroy, v8pp::metadata::docs("void", {}, "Removes the engine on every client after emitting `siegeEngineDestroy`. A projectile already in the air still lands."));
        }
        return *_class;
    }

    void SiegeEngine::UnregisterIsolate(v8::Isolate *isolate) {
        _classes.erase(isolate);
    }

    void SiegeEngine::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        if (isolate == nullptr || global.IsEmpty()) {
            return;
        }

        auto &result = ServerCatalog().data_type("SiegeResult");
        result.add_property("accepted", "boolean", "Whether the engine did it.");
        result.add_property("reason", "string", "Why it did not, as a phrase that reads after the engine's name: `is not loaded`, `cannot fire: the target is further than the engine can throw`. Empty when it did.");
        result.add_property("seconds", "number", "How long until the step it started is over: the whole load for `load`, until the projectile is let go for `fire`. 0 when nothing happened.");

        auto &events = ServerCatalog().data_type("EventMap");
        events.add_property("siegeEngineSpawn", "[engine: SiegeEngine]", "Dispatched immediately after a siege engine is built.");
        events.add_property("siegeEngineDestroy", "[engine: SiegeEngine]", "Dispatched while a siege engine is being removed. The handle still resolves.");
        events.add_property("siegeEngineReady", "[engine: SiegeEngine]", "Dispatched when an engine's load cycle finishes and it can fire.");
        events.add_property("siegeEngineUse", "[player: Player, engine: SiegeEngine, action: \"operate\" | \"winch\" | \"leave\" | \"load\" | \"fire\"]",
            "Dispatched when a player asks something of a `usable` engine, standing at it: to take its place (`operate`) or a free winch (`winch`) from the game's own prompt, or -- in its place -- to `load` it or `fire` it where they laid it, with the player as the attacker. Return false to refuse. `leave` reports a player stepping away from either and cannot be refused. A handler that works the engine itself should refuse, so it is not worked twice.");
        events.add_property("siegeEngineDamage", "[engine: SiegeEngine, amount: number, attacker: Player | null]",
            "Dispatched when a blast takes `amount` off an engine with a `maxHealth`, before it is wrecked by it. `health` already reads what is left.");
        events.add_property("siegeEngineWreck", "[engine: SiegeEngine, attacker: Player | null]", "Dispatched when an engine's health runs out. Its operator and crew have been let go, and a stone still in its sling went down with it.");
        events.add_property("siegeFire", "[engine: SiegeEngine, attacker: Player | null, target: Vector3]", "Dispatched the moment an engine lets its projectile go, partway through the shot `fire` started.");
        events.add_property("siegeImpact", "[engine: SiegeEngine | null, position: Vector3, attacker: Player | null]",
            "Dispatched where a projectile came down: what the reporting player -- the attacker when near enough, else whoever is nearest the target -- saw it hit, checked against the arc it was thrown on, or the target itself when nobody could see it. Players within `damageRadius` have already been told to take their share, and NPCs have taken theirs. `engine` is null when it was destroyed while the stone was in the air.");

        v8pp::class_<SiegeEngine> &cls = GetClass(isolate);
        cls.static_function("spawn", &JS_Spawn,
            v8pp::metadata::docs("SiegeEngine",
                {
                    v8pp::metadata::param("kind", "string", false, "`trebuchet` or `cannon`."),
                    v8pp::metadata::param("position", "Vector3 | Partial<Vector3>", false, "World-space position of the engine's base."),
                    v8pp::metadata::param("rotation", "Vector3 | Quaternion", true, "Optional initial orientation: a Quaternion, or a Vector3 of Euler angles in degrees. `fire` and `aim` turn it to face their target."),
                    v8pp::metadata::param("virtualWorld", "number", true, "Optional virtual world the engine belongs to; omitted puts it in the global one."),
                },
                "Builds and replicates a siege engine, unloaded. The trebuchet is the game's own model and plays its own winch, load and fire animations; the cannon is the game's static gun.", "The new engine's handle."));
        cls.static_function("all", &JS_All,
            v8pp::metadata::docs("SiegeEngine[]", {v8pp::metadata::param("virtualWorld", "number", true, "Optional virtual world to list; omitted lists every one.")}, "Lists the siege engines the server has.", "One handle per engine, in no particular order."));
        cls.static_function("getById", &JS_GetById, v8pp::metadata::docs("SiegeEngine | null", {v8pp::metadata::param("id", "number", false, "Network entity identifier.")}, "Looks an engine up by its network entity ID.", "The engine's handle, or null when no live engine has that ID."));
        cls.static_function("destroyAll", &JS_DestroyAll,
            v8pp::metadata::docs("number", {v8pp::metadata::param("virtualWorld", "number", true, "Optional virtual world to clear; omitted clears every one.")}, "Removes siege engines, emitting `siegeEngineDestroy` for each one.", "How many were removed."));
        cls.publish(global);
    }

    void Events::SiegeEngineSpawned(std::uint64_t engineNetworkId) {
        EmitEngineEvent("siegeEngineSpawn", engineNetworkId);
    }

    void Events::SiegeEngineDestroyed(std::uint64_t engineNetworkId) {
        EmitEngineEvent("siegeEngineDestroy", engineNetworkId);
    }

    void Events::SiegeEngineReady(std::uint64_t engineNetworkId) {
        EmitEngineEvent("siegeEngineReady", engineNetworkId);
    }

    bool Events::SiegeEngineUse(std::uint64_t playerNetworkId, std::uint64_t engineNetworkId, const char *action) {
        return EmitReservedEventSync("siegeEngineUse", [playerNetworkId, engineNetworkId, action](v8::Isolate *isolate) {
            std::vector<v8::Local<v8::Value>> args;
            args.push_back(WrapPlayer(isolate, playerNetworkId));
            args.push_back(WrapSiegeEngine(isolate, engineNetworkId));
            args.push_back(v8pp::to_v8(isolate, std::string(action)));
            return args;
        });
    }

    void Events::SiegeEngineDamaged(std::uint64_t engineNetworkId, float amount, std::uint64_t attackerNetworkId) {
        EmitReservedEvent("siegeEngineDamage", [engineNetworkId, amount, attackerNetworkId](v8::Isolate *isolate) {
            std::vector<v8::Local<v8::Value>> args;
            args.push_back(WrapSiegeEngine(isolate, engineNetworkId));
            args.push_back(v8pp::to_v8(isolate, amount));
            args.push_back(WrapPlayer(isolate, attackerNetworkId));
            return args;
        });
    }

    void Events::SiegeEngineWrecked(std::uint64_t engineNetworkId, std::uint64_t attackerNetworkId) {
        EmitReservedEvent("siegeEngineWreck", [engineNetworkId, attackerNetworkId](v8::Isolate *isolate) {
            std::vector<v8::Local<v8::Value>> args;
            args.push_back(WrapSiegeEngine(isolate, engineNetworkId));
            args.push_back(WrapPlayer(isolate, attackerNetworkId));
            return args;
        });
    }

    void Events::SiegeFired(std::uint64_t engineNetworkId, std::uint64_t attackerNetworkId, const glm::vec3 &target) {
        EmitReservedEvent("siegeFire", [engineNetworkId, attackerNetworkId, target](v8::Isolate *isolate) {
            std::vector<v8::Local<v8::Value>> args;
            args.push_back(WrapSiegeEngine(isolate, engineNetworkId));
            args.push_back(WrapPlayer(isolate, attackerNetworkId));
            args.push_back(v8pp::convert<glm::vec3>::to_v8(isolate, target));
            return args;
        });
    }

    void Events::SiegeImpact(std::uint64_t engineNetworkId, std::uint64_t attackerNetworkId, const glm::vec3 &point) {
        EmitReservedEvent("siegeImpact", [engineNetworkId, attackerNetworkId, point](v8::Isolate *isolate) {
            std::vector<v8::Local<v8::Value>> args;
            args.push_back(WrapSiegeEngine(isolate, engineNetworkId));
            args.push_back(v8pp::convert<glm::vec3>::to_v8(isolate, point));
            args.push_back(WrapPlayer(isolate, attackerNetworkId));
            return args;
        });
    }
} // namespace KCDC::Scripting
