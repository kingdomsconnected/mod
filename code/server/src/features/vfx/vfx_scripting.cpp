#include "vfx_scripting.h"

#include "core/scripting/script_context.h"
#include "core/server.h"
#include "core/scripting/wrap.h"

#include "features/vfx/vfx_verbs.h"

#include "shared/streamed_assets/streamed_asset_policy.h"
#include "shared/features/vfx/vfx_rpc.h"
#include "shared/game/vfx_catalog.h"
#include "shared/scripting/catalog.h"
#include "shared/scripting/vfx_options.h"

#include <core_modules.h>
#include <networking/replication/replication_manager.h>
#include <scripting/v8pp_types.h>

#include <v8pp/convert.hpp>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace KCDC::Scripting {

    namespace {
        // Long enough to read as an event, short enough that one fired per hit
        // never piles up.
        constexpr std::uint32_t kDefaultBurstMs = 2000;

        // The shape both of this feature's events have: one subject, nothing else.
        void EmitVfxEvent(const char *eventName, std::uint64_t vfxNetworkId) {
            EmitReservedEvent(eventName, [vfxNetworkId](v8::Isolate *isolate) {
                std::vector<v8::Local<v8::Value>> args;
                args.push_back(WrapVfx(isolate, vfxNetworkId));
                return args;
            });
        }

        /** The virtual world an argument names, or none for every one of them. */
        std::optional<MafiaNet::VirtualWorldId> WorldArg(const v8::FunctionCallbackInfo<v8::Value> &info, int index) {
            if (info.Length() <= index || !info[index]->IsNumber()) {
                return std::nullopt;
            }
            return static_cast<MafiaNet::VirtualWorldId>(v8pp::from_v8<double>(info.GetIsolate(), info[index]));
        }

        /** The handle a prototype function was called on, or nullptr. */
        Vfx *Self(const v8::FunctionCallbackInfo<v8::Value> &info) {
            return v8pp::class_<Vfx>::unwrap_object(info.GetIsolate(), info.This());
        }

        /** The effect a name argument means -- a catalog row, or one out of a library this server streams -- throwing the binding's own error when it names neither. */
        std::optional<Shared::Game::VfxEffectRef> EffectArg(const v8::FunctionCallbackInfo<v8::Value> &info, const char *verb) {
            v8::Isolate *isolate    = info.GetIsolate();
            const std::string given = info.Length() > 0 && info[0]->IsString() ? v8pp::from_v8<std::string>(isolate, info[0]) : std::string {};
            const std::size_t index = Shared::Game::FindVfxEffectIndex(given);
            if (index != Shared::Game::kNoVfxEffect) {
                return Shared::Game::VfxEffectRef::Catalog(index);
            }
            if (KCDC::Shared::StreamedAssets::Policy::IsStreamedEffectName(given) && Server::Get()->GetAssetPaks().HasEntry(KCDC::Shared::StreamedAssets::kLaneStream, KCDC::Shared::StreamedAssets::Policy::ParticleLibraryEntryOf(given))) {
                return Shared::Game::VfxEffectRef::Streamed(given);
            }
            isolate->ThrowError(v8pp::to_v8(isolate, std::string(verb) + ": \"" + given + "\" is neither an effect in the particle catalog nor one out of a library this server streams"));
            return std::nullopt;
        }

        std::uint32_t DurationOf(const VfxOptions &options) {
            const std::uint32_t requested = options.durationGiven ? options.durationMs : kDefaultBurstMs;
            if (requested == 0) {
                return kDefaultBurstMs;
            }
            return requested > Shared::RPC::VfxBurst::kMaxDurationMs ? Shared::RPC::VfxBurst::kMaxDurationMs : requested;
        }

        /** The replicated entity an argument names: a handle of any kind, or a bare network id. */
        Framework::Networking::Replication::NetworkEntity *SubjectArg(const v8::FunctionCallbackInfo<v8::Value> &info, int index) {
            v8::Isolate *isolate = info.GetIsolate();
            if (info.Length() <= index) {
                return nullptr;
            }

            if (info[index]->IsNumber()) {
                auto *replication             = Framework::CoreModules::GetReplication();
                const std::uint64_t networkId = static_cast<std::uint64_t>(info[index]->IntegerValue(isolate->GetCurrentContext()).FromMaybe(0));
                return replication != nullptr && networkId != 0 ? replication->GetEntityByNetworkID(networkId) : nullptr;
            }

            // Player, Prop, Horse, Dog and the rest all inherit the framework's
            // Entity, so one unwrap covers every handle the API hands out.
            auto *entity = v8pp::class_<Framework::Scripting::Builtins::Entity>::unwrap_object(isolate, info[index]);
            return entity != nullptr ? entity->GetHandle() : nullptr;
        }

        // JS: vfx.destroy()
        void JS_Destroy(const v8::FunctionCallbackInfo<v8::Value> &info) {
            if (auto *self = Self(info)) {
                KCDC::Features::Vfx::Effects::Destroy(self->ResolveVfx());
            }
        }

        // JS: Vfx.spawn(effect, position, options?) -> Vfx
        void JS_Spawn(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            const std::optional<Shared::Game::VfxEffectRef> effectRef = EffectArg(info, "Vfx.spawn");
            if (!effectRef) {
                return;
            }

            glm::vec3 position {0.0F};
            if (info.Length() >= 2 && info[1]->IsObject()) {
                position = v8pp::convert<glm::vec3>::from_v8(isolate, info[1]);
            }

            const VfxOptions options         = ParseVfxOptions(info, 2);
            const std::uint32_t virtualWorld = info.Length() >= 4 && info[3]->IsNumber() ? static_cast<std::uint32_t>(v8pp::from_v8<double>(isolate, info[3])) : MafiaNet::VIRTUAL_WORLD_GLOBAL;

            auto *effect = KCDC::Features::Vfx::Effects::Spawn(*effectRef, position, options.rotation, options.tuning, virtualWorld);
            if (effect == nullptr) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Vfx.spawn: failed to create entity"));
                return;
            }

            info.GetReturnValue().Set(WrapVfx(isolate, effect->GetNetworkID()));
        }

        // JS: Vfx.burst(effect, position, options?, virtualWorld?) -> number
        void JS_Burst(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            const std::optional<Shared::Game::VfxEffectRef> effectRef = EffectArg(info, "Vfx.burst");
            if (!effectRef) {
                return;
            }

            if (info.Length() < 2 || !info[1]->IsObject()) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Vfx.burst: expected (effect, position, options?, virtualWorld?)"));
                return;
            }

            const glm::vec3 position         = v8pp::convert<glm::vec3>::from_v8(isolate, info[1]);
            const VfxOptions options         = ParseVfxOptions(info, 2);
            const std::uint32_t virtualWorld = info.Length() >= 4 && info[3]->IsNumber() ? static_cast<std::uint32_t>(v8pp::from_v8<double>(isolate, info[3])) : MafiaNet::VIRTUAL_WORLD_GLOBAL;

            info.GetReturnValue().Set(static_cast<std::uint32_t>(KCDC::Features::Vfx::Effects::Burst(*effectRef, position, options.rotation, options.tuning, DurationOf(options), virtualWorld)));
        }

        // JS: Vfx.burstOn(effect, entity, options?) -> number
        void JS_BurstOn(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            const std::optional<Shared::Game::VfxEffectRef> effectRef = EffectArg(info, "Vfx.burstOn");
            if (!effectRef) {
                return;
            }

            auto *subject = SubjectArg(info, 1);
            if (subject == nullptr) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Vfx.burstOn: expected (effect, entity, options?) with a live entity"));
                return;
            }

            const VfxOptions options = ParseVfxOptions(info, 2);
            info.GetReturnValue().Set(static_cast<std::uint32_t>(KCDC::Features::Vfx::Effects::BurstOnEntity(*effectRef, subject, options.offset, options.rotation, options.tuning, DurationOf(options))));
        }

        // JS: Vfx.all(virtualWorld?) -> Vfx[]
        void JS_All(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            v8::Local<v8::Array> effects   = v8::Array::New(isolate);

            std::uint32_t index = 0;
            for (auto *effect : KCDC::Features::Vfx::Effects::Collect(WorldArg(info, 0))) {
                effects->Set(context, index++, WrapVfx(isolate, effect->GetNetworkID())).Check();
            }
            info.GetReturnValue().Set(effects);
        }

        // JS: Vfx.getById(id) -> Vfx | null
        void JS_GetById(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            if (info.Length() < 1 || !info[0]->IsNumber()) {
                isolate->ThrowError(v8pp::to_v8(isolate, "Vfx.getById: expected (id)"));
                return;
            }

            const std::uint64_t networkId = static_cast<std::uint64_t>(info[0]->IntegerValue(isolate->GetCurrentContext()).FromMaybe(0));
            info.GetReturnValue().Set(WrapVfx(isolate, networkId));
        }

        // JS: Vfx.destroyAll(virtualWorld?) -> number
        void JS_DestroyAll(const v8::FunctionCallbackInfo<v8::Value> &info) {
            info.GetReturnValue().Set(static_cast<std::uint32_t>(KCDC::Features::Vfx::Effects::DestroyAll(WorldArg(info, 0))));
        }

        // JS: Vfx.list(prefix?) -> string[]
        void JS_List(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            v8::Local<v8::Array> names     = v8::Array::New(isolate);

            const std::string prefix = info.Length() > 0 && info[0]->IsString() ? v8pp::from_v8<std::string>(isolate, info[0]) : std::string {};
            std::uint32_t written    = 0;
            for (const Shared::Game::VfxEffectEntry &effect : Shared::Game::g_VfxEffects) {
                if (!prefix.empty() && effect.name.compare(0, prefix.size(), prefix) != 0) {
                    continue;
                }
                names->Set(context, written++, v8pp::to_v8(isolate, std::string(effect.name))).Check();
            }
            info.GetReturnValue().Set(names);
        }

        /** What a handle plays, or nullopt once it has gone. Its views are into the live entity, so read them at once. */
        std::optional<Shared::Game::VfxEffectEntry> RowOf(const Vfx &handle) {
            auto *effect = handle.ResolveVfx();
            return effect != nullptr ? effect->effect.Entry() : std::nullopt;
        }

        /** One field of a live effect's tuning, or 0 once it has gone. */
        float ReadTuning(const Vfx &handle, float Shared::Game::VfxTuning::*field) {
            auto *effect = handle.ResolveVfx();
            return effect != nullptr ? effect->tuning.*field : 0.0F;
        }

        /** Lets `apply` change one field of a live effect's tuning and pushes the result back, which rebuilds its emitter. */
        template <typename Apply>
        void Retune(Shared::Entities::VfxEntity *effect, Apply apply) {
            if (effect == nullptr) {
                return;
            }
            Shared::Game::VfxTuning tuning = effect->tuning;
            apply(tuning);
            KCDC::Features::Vfx::Effects::SetTuning(effect, tuning);
        }
    } // namespace

    Shared::Entities::VfxEntity *Vfx::ResolveVfx() const {
        return dynamic_cast<Shared::Entities::VfxEntity *>(Resolve());
    }

    std::string Vfx::GetEffect() const {
        const std::optional<Shared::Game::VfxEffectEntry> row = RowOf(*this);
        return row ? std::string(row->name) : std::string();
    }

    std::string Vfx::GetLibrary() const {
        const std::optional<Shared::Game::VfxEffectEntry> row = RowOf(*this);
        return row ? std::string(row->library) : std::string();
    }

    std::string Vfx::GetGroup() const {
        const std::optional<Shared::Game::VfxEffectEntry> row = RowOf(*this);
        return row ? std::string(row->group) : std::string();
    }

    float Vfx::GetScale() const {
        return ReadTuning(*this, &Shared::Game::VfxTuning::scale);
    }

    void Vfx::SetScale(float scale) {
        Retune(ResolveVfx(), [scale](Shared::Game::VfxTuning &tuning) {
            tuning.scale = std::clamp(scale, Shared::Game::VfxTuning::kMinScale, Shared::Game::VfxTuning::kMaxScale);
        });
    }

    float Vfx::GetCountScale() const {
        return ReadTuning(*this, &Shared::Game::VfxTuning::countScale);
    }

    void Vfx::SetCountScale(float countScale) {
        Retune(ResolveVfx(), [countScale](Shared::Game::VfxTuning &tuning) {
            tuning.countScale = std::clamp(countScale, Shared::Game::VfxTuning::kMinScale, Shared::Game::VfxTuning::kMaxCountScale);
        });
    }

    float Vfx::GetSpeedScale() const {
        return ReadTuning(*this, &Shared::Game::VfxTuning::speedScale);
    }

    void Vfx::SetSpeedScale(float speedScale) {
        Retune(ResolveVfx(), [speedScale](Shared::Game::VfxTuning &tuning) {
            tuning.speedScale = std::clamp(speedScale, 0.0F, Shared::Game::VfxTuning::kMaxSpeedScale);
        });
    }

    float Vfx::GetTimeScale() const {
        return ReadTuning(*this, &Shared::Game::VfxTuning::timeScale);
    }

    void Vfx::SetTimeScale(float timeScale) {
        Retune(ResolveVfx(), [timeScale](Shared::Game::VfxTuning &tuning) {
            tuning.timeScale = std::clamp(timeScale, Shared::Game::VfxTuning::kMinScale, Shared::Game::VfxTuning::kMaxTimeScale);
        });
    }

    float Vfx::GetStrength() const {
        return ReadTuning(*this, &Shared::Game::VfxTuning::strength);
    }

    void Vfx::SetStrength(float strength) {
        Retune(ResolveVfx(), [strength](Shared::Game::VfxTuning &tuning) {
            tuning.strength = std::clamp(strength, -1.0F, 1.0F);
        });
    }

    float Vfx::GetPulsePeriod() const {
        return ReadTuning(*this, &Shared::Game::VfxTuning::pulsePeriod);
    }

    void Vfx::SetPulsePeriod(float pulsePeriod) {
        Retune(ResolveVfx(), [pulsePeriod](Shared::Game::VfxTuning &tuning) {
            tuning.pulsePeriod = std::clamp(pulsePeriod, 0.0F, Shared::Game::VfxTuning::kMaxPulsePeriod);
        });
    }

    std::string Vfx::ToString() const {
        std::ostringstream ss;
        ss << "Vfx{ id: " << GetId() << ", effect: " << GetEffect() << ", scale: " << GetScale() << " }";
        return ss.str();
    }

    v8pp::class_<Vfx> &Vfx::GetClass(v8::Isolate *isolate) {
        auto &_class = _classes[isolate];
        if (!_class) {
            // v8pp's inherit<Entity> needs the framework Entity registered first.
            Framework::Scripting::Builtins::Entity::GetClass(isolate);

            _class = std::make_unique<v8pp::class_<Vfx>>(isolate, ServerCatalog(), "Vfx", "Replicated particle effect handle.");
            _class->auto_wrap_objects(true);
            _class->inherit<Framework::Scripting::Builtins::Entity>()
                .ctor<std::uint64_t>(v8pp::metadata::docs("void", {v8pp::metadata::param("id", "number", false, "Network entity identifier.")}, "Creates a script wrapper for an existing effect with this ID; use Vfx.spawn() to place one."))
                .function("toString", &Vfx::ToString, v8pp::metadata::docs("string", {}, "Formats this effect handle for logging and debugging.", "The effect ID, its name and its scale."));

            _class->prototype_function("destroy", &JS_Destroy, v8pp::metadata::docs("void", {}, "Stops this effect on every client that can see it, after emitting vfxDestroy."));

            _class->property("effect", &Vfx::GetEffect,
                v8pp::metadata::property_docs("string", "Name of the particle effect this is playing, e.g. `WH_Particels.fires.campfire_a`. Read-only: every spawn parameter is read once when the emitter is created, so an effect is the one it was placed as."));
            _class->property("library", &Vfx::GetLibrary, v8pp::metadata::property_docs("string", "Particle library the effect came out of, e.g. `WH_Particels`, which is the part of the name before the first dot."));
            _class->property("group", &Vfx::GetGroup, v8pp::metadata::property_docs("string", "Authored bucket inside that library, e.g. `fires`."));
            _class->property("scale", &Vfx::GetScale, &Vfx::SetScale,
                v8pp::metadata::property_docs("number", "Multiplies every size the effect authored, from 0.01 to 32; a value outside that is clamped into it. Assignment rebuilds the emitter on every client that can see it, so the effect restarts."));
            _class->property("countScale", &Vfx::GetCountScale, &Vfx::SetCountScale, v8pp::metadata::property_docs("number", "Multiplies how many particles are emitted, from 0.01 to 16. Assignment restarts the effect."));
            _class->property("speedScale", &Vfx::GetSpeedScale, &Vfx::SetSpeedScale, v8pp::metadata::property_docs("number", "Multiplies emission speed, from 0 to 16. Assignment restarts the effect."));
            _class->property("timeScale", &Vfx::GetTimeScale, &Vfx::SetTimeScale, v8pp::metadata::property_docs("number", "Multiplies how fast the emitter's own time runs, from 0.01 to 16. Assignment restarts the effect."));
            _class->property("strength", &Vfx::GetStrength, &Vfx::SetStrength, v8pp::metadata::property_docs("number", "Feeds the effect's own strength curves, from -1 to 1; -1 leaves them where the effect authored them. Assignment restarts the effect."));
            _class->property("pulsePeriod", &Vfx::GetPulsePeriod, &Vfx::SetPulsePeriod, v8pp::metadata::property_docs("number", "Seconds between restarts of the whole emitter, from 0 to 600; 0 never restarts it. Assignment restarts the effect."));
        }
        return *_class;
    }

    void Vfx::UnregisterIsolate(v8::Isolate *isolate) {
        _classes.erase(isolate);
    }

    void Vfx::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        if (isolate == nullptr || global.IsEmpty()) {
            return;
        }

        auto &events = ServerCatalog().data_type("EventMap");
        events.add_property("vfxSpawn", "[vfx: Vfx]", "Dispatched immediately after a replicated particle effect is placed, whether by `Vfx.spawn`, a command, or anything else.");
        events.add_property("vfxDestroy", "[vfx: Vfx]", "Dispatched while a replicated particle effect is being stopped. The handle still resolves, so its name and its pose can be read one last time.");

        v8pp::class_<Vfx> &cls = GetClass(isolate);

        cls.static_function("spawn", &JS_Spawn,
            v8pp::metadata::docs("Vfx",
                {
                    v8pp::metadata::param("effect", "string", false, "Effect to place, spelled as the game's particle libraries spell it -- `WH_Particels.fires.campfire_a`. `Vfx.list()` is the whole vocabulary."),
                    v8pp::metadata::param("position", "Vector3 | Partial<Vector3>", true, "Optional world-space position; omitted components default to zero."),
                    v8pp::metadata::param("options", kVfxOptionsType, true,
                        "`rotation` aims the emitter; `scale`, `countScale`, `speedScale` and `timeScale` multiply what the effect authored; `strength` feeds its strength curves; `pulsePeriod` restarts it on that many seconds; `prime` starts it already running, which is what a fire wants. "
                        "`durationMs` is ignored here -- a placed effect lasts until it is destroyed."),
                    v8pp::metadata::param("virtualWorld", "number", true, "Optional virtual world the effect belongs to; omitted puts it in the global one."),
                },
                "Places a particle effect in the world and leaves it running. It is a replicated entity, so the interest grid streams it to whoever comes near and takes it away again when they leave -- which is what a campfire, a torch or a plume of smoke needs and what `burst` cannot do. "
                "How far it streams is taken from the effect's own draw distance, between 25 and 250 metres.",
                "The newly placed effect handle."));

        cls.static_function("burst", &JS_Burst,
            v8pp::metadata::docs("number",
                {
                    v8pp::metadata::param("effect", "string", false, "Effect to play, spelled as the game's particle libraries spell it."),
                    v8pp::metadata::param("position", "Vector3", false, "Where it happens, in world-space metres."),
                    v8pp::metadata::param("options", kVfxOptionsType, true, "As `spawn`, and here `durationMs` does apply: how long each client keeps the emitter, two seconds by default and a minute at most."),
                    v8pp::metadata::param("virtualWorld", "number", true, "Optional virtual world; omitted uses the global one."),
                },
                "Plays an effect once for the players who can see the point it happens at, and keeps nothing. Recipients are chosen by the effect's own draw distance -- there is no point telling a client about something its engine would not draw -- so a blood spray reaches the people standing there and nobody else. "
                "A player who arrives afterwards sees nothing, which is the right answer for a spark and the wrong one for a campfire; use `spawn` for those.",
                "How many clients were told."));

        cls.static_function("burstOn", &JS_BurstOn,
            v8pp::metadata::docs("number",
                {
                    v8pp::metadata::param("effect", "string", false, "Effect to play, spelled as the game's particle libraries spell it."),
                    v8pp::metadata::param("entity", "Entity | number", false, "What to hang it on: any replicated handle -- a Player, Prop, Horse or Dog -- or a bare network entity ID."),
                    v8pp::metadata::param("options", kVfxAttachOptionsType, true, "As `burst`, plus `offset`: where the emitter sits in the entity's own space, in metres, up to 8 m from its origin."),
                },
                "Plays an effect once on a replicated entity, where it follows that entity: blood on the body that was hit, sparks on the weapon that struck. A client that has not streamed the entity in drops it rather than playing it somewhere else.",
                "How many clients were told."));

        cls.static_function("all", &JS_All,
            v8pp::metadata::docs("Vfx[]", {v8pp::metadata::param("virtualWorld", "number", true, "Optional virtual world to list; omitted lists every one of them.")}, "Lists every replicated effect the server currently has.",
                "One handle per live effect, in no particular order."));
        cls.static_function("getById", &JS_GetById,
            v8pp::metadata::docs("Vfx | null", {v8pp::metadata::param("id", "number", false, "Network entity identifier.")}, "Looks a replicated effect up by its network entity ID.", "The effect's handle, or null when no live effect has that ID."));
        cls.static_function("destroyAll", &JS_DestroyAll,
            v8pp::metadata::docs("number", {v8pp::metadata::param("virtualWorld", "number", true, "Optional virtual world to clear; omitted clears every one of them.")}, "Stops replicated effects, emitting vfxDestroy for each one.", "How many effects were stopped."));
        cls.static_function("list", &JS_List,
            v8pp::metadata::docs("string[]", {v8pp::metadata::param("prefix", "string", true, "Keep only the names starting with this, e.g. `WH_Particels.fires` or `collisions.combat`.")},
                "Every effect name the shipped game declares, in order. Mined from the game's particle libraries at build time, so it is the same list the client has.", "The matching names."));
        cls.publish(global);
    }

    void Events::VfxSpawned(std::uint64_t vfxNetworkId) {
        EmitVfxEvent("vfxSpawn", vfxNetworkId);
    }

    void Events::VfxDestroyed(std::uint64_t vfxNetworkId) {
        EmitVfxEvent("vfxDestroy", vfxNetworkId);
    }
} // namespace KCDC::Scripting
