#include "vfx_scripting.h"

#include "core/application.h"

#include "features/vfx/vfx_service.h"

#include "common/vector_math.h"

#include "shared/streamed_assets/streamed_asset_policy.h"
#include "shared/game/vfx_catalog.h"
#include "shared/scripting/catalog.h"
#include "shared/scripting/vfx_options.h"

#include <scripting/v8pp_types.h>

#include <glm/glm.hpp>

#include <v8pp/convert.hpp>
#include <v8pp/module.hpp>

#include <cstdint>
#include <string>

namespace KCDC::Scripting {
    namespace {
        // Long enough to see, short enough that one fired per hit never reaches
        // the service's budget.
        constexpr std::uint32_t kDefaultBurstMs = 2000;

        // An effect that ran forever would be a leak in a resource nobody
        // restarts, so the binding gives one an end even when the caller does
        // not. Anything permanent belongs on the server as a replicated effect.
        constexpr std::uint32_t kMaxDurationMs = 3600000;

        bool IsTextAt(const v8::FunctionCallbackInfo<v8::Value> &info, int index) {
            return info.Length() > index && info[index]->IsString();
        }

        std::string TextAt(const v8::FunctionCallbackInfo<v8::Value> &info, int index) {
            return IsTextAt(info, index) ? v8pp::from_v8<std::string>(info.GetIsolate(), info[index]) : std::string {};
        }

        /** The effect a name means: a catalog row, or one out of a library the session streams. Nullopt for neither. */
        std::optional<Shared::Game::VfxEffectRef> EffectOf(const std::string &name) {
            const std::size_t index = Shared::Game::FindVfxEffectIndex(name);
            if (index != Shared::Game::kNoVfxEffect) {
                return Shared::Game::VfxEffectRef::Catalog(index);
            }
            if (KCDC::Shared::StreamedAssets::Policy::IsStreamedEffectName(name) && KCDC::Core::gApplication->GetStreamedAssetService().HasEntry(KCDC::Shared::StreamedAssets::Policy::ParticleLibraryEntryOf(name))) {
                return Shared::Game::VfxEffectRef::Streamed(name);
            }
            return std::nullopt;
        }

        /** The effect a name argument means, throwing the binding's own error when it names nothing. */
        std::optional<Shared::Game::VfxEffectRef> EffectArg(const v8::FunctionCallbackInfo<v8::Value> &info, const char *verb) {
            std::optional<Shared::Game::VfxEffectRef> effect = EffectOf(TextAt(info, 0));
            if (!effect) {
                info.GetIsolate()->ThrowError(v8pp::to_v8(info.GetIsolate(), std::string(verb) + ": \"" + TextAt(info, 0) + "\" is neither an effect in the particle catalog nor one out of a library the server streams"));
            }
            return effect;
        }

        std::uint32_t DurationOf(const VfxOptions &options) {
            const std::uint32_t requested = options.durationGiven ? options.durationMs : kDefaultBurstMs;
            return requested == 0 || requested > kMaxDurationMs ? kMaxDurationMs : requested;
        }

        std::uint32_t HandleArg(const v8::FunctionCallbackInfo<v8::Value> &info, int index) {
            return info.Length() > index && info[index]->IsNumber() ? static_cast<std::uint32_t>(v8pp::from_v8<double>(info.GetIsolate(), info[index])) : 0;
        }
    } // namespace

    void Vfx::JS_Spawn(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);

        if (!IsTextAt(info, 0) || info.Length() < 2 || !info[1]->IsObject()) {
            isolate->ThrowError(v8pp::to_v8(isolate, "Vfx.spawn: expected (effect, position, options?)"));
            return;
        }

        const std::optional<Shared::Game::VfxEffectRef> effect = EffectArg(info, "Vfx.spawn");
        if (!effect) {
            return;
        }

        const glm::vec3 position = v8pp::convert<glm::vec3>::from_v8(isolate, info[1]);
        if (!KCDC::Common::IsFinite(position)) {
            isolate->ThrowError(v8pp::to_v8(isolate, "Vfx.spawn: position must be finite"));
            return;
        }

        const VfxOptions options = ParseVfxOptions(info, 2);
        info.GetReturnValue().Set(KCDC::Core::gApplication->GetVfxService().Spawn(*effect, position, options.rotation, options.tuning, DurationOf(options)));
    }

    void Vfx::JS_Attach(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);

        if (!IsTextAt(info, 0) || info.Length() < 2 || !info[1]->IsNumber()) {
            isolate->ThrowError(v8pp::to_v8(isolate, "Vfx.attach: expected (effect, entityId, options?)"));
            return;
        }

        const std::optional<Shared::Game::VfxEffectRef> effect = EffectArg(info, "Vfx.attach");
        if (!effect) {
            return;
        }

        const SDK::EntityId entityId = static_cast<SDK::EntityId>(v8pp::from_v8<double>(isolate, info[1]));
        const VfxOptions options     = ParseVfxOptions(info, 2);
        if (!KCDC::Common::IsFinite(options.offset)) {
            isolate->ThrowError(v8pp::to_v8(isolate, "Vfx.attach: offset must be finite"));
            return;
        }

        info.GetReturnValue().Set(KCDC::Core::gApplication->GetVfxService().Attach(*effect, entityId, options.offset, options.rotation, options.tuning, DurationOf(options)));
    }

    void Vfx::JS_Stop(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);

        if (info.Length() < 1 || !info[0]->IsNumber()) {
            isolate->ThrowError(v8pp::to_v8(isolate, "Vfx.stop: expected (handle)"));
            return;
        }

        info.GetReturnValue().Set(KCDC::Core::gApplication->GetVfxService().Stop(HandleArg(info, 0)));
    }

    void Vfx::JS_StopAll(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::HandleScope handleScope(info.GetIsolate());
        KCDC::Core::gApplication->GetVfxService().StopAll();
    }

    void Vfx::JS_IsPlaying(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::HandleScope handleScope(info.GetIsolate());
        info.GetReturnValue().Set(KCDC::Core::gApplication->GetVfxService().IsAlive(HandleArg(info, 0)));
    }

    void Vfx::JS_Has(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);

        if (!IsTextAt(info, 0)) {
            isolate->ThrowError(v8pp::to_v8(isolate, "Vfx.has: expected (effect)"));
            return;
        }

        const std::optional<Shared::Game::VfxEffectRef> effect = EffectOf(TextAt(info, 0));
        info.GetReturnValue().Set(effect && KCDC::Features::Vfx::VfxService::IsAvailable(*effect));
    }

    void Vfx::JS_List(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = isolate->GetCurrentContext();
        v8::Local<v8::Array> names     = v8::Array::New(isolate);

        const std::string prefix = TextAt(info, 0);
        std::uint32_t written    = 0;
        for (const Shared::Game::VfxEffectEntry &effect : Shared::Game::g_VfxEffects) {
            if (!prefix.empty() && effect.name.compare(0, prefix.size(), prefix) != 0) {
                continue;
            }
            names->Set(context, written++, v8pp::to_v8(isolate, std::string(effect.name))).Check();
        }
        info.GetReturnValue().Set(names);
    }

    void Vfx::JS_Count(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::HandleScope handleScope(info.GetIsolate());
        info.GetReturnValue().Set(static_cast<std::uint32_t>(KCDC::Core::gApplication->GetVfxService().LiveCount()));
    }

    void Vfx::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        if (isolate == nullptr || global.IsEmpty()) {
            return;
        }

        v8pp::module vfx(isolate, ClientCatalog(), "Vfx",
            "The game's own particle effects, played on this machine. An effect is a name out of the game's particle libraries -- `WH_Particels.fires.candle`, `collisions.combat.sword_sword`, `cinematics.dust.dust_army_a` -- and `list` is the whole vocabulary. "
            "Nothing here is replicated: an effect everyone should see is one the server tells every client to play with `Vfx.burst`, or one the server places with `Vfx.spawn`, which the interest grid then streams to whoever is near it. "
            "Almost every effect the game ships is continuous, which means it never ends on its own -- so everything started here is given a duration, and one that is not stopped first is stopped when the session ends.");

        vfx.function("spawn", &Vfx::JS_Spawn,
            v8pp::metadata::docs("number",
                {
                    v8pp::metadata::param("effect", "string", false, "Name of the effect, spelled as the game's particle libraries spell it."),
                    v8pp::metadata::param("position", "Vector3", false, "Where to play it, in world-space metres."),
                    v8pp::metadata::param("options", kVfxOptionsType, true,
                        "`rotation` aims the emitter; `scale`, `countScale`, `speedScale` and `timeScale` multiply what the effect authored; `strength` feeds its strength curves; `pulsePeriod` restarts it on that many seconds; `prime` starts it already running, which is what a fire wants. "
                        "`durationMs` is how long it lives -- two seconds by default, and an hour at most."),
                },
                "Plays an effect standing at a point in the world, on an entity this client owns and removes again. Every spawn parameter is read once, when the emitter is created, so none of them can be changed afterwards -- stop it and start another.",
                "A handle to pass to `stop`, or 0 when the world was not up, the budget was full or the game would not load the effect."));

        vfx.function("attach", &Vfx::JS_Attach,
            v8pp::metadata::docs("number",
                {
                    v8pp::metadata::param("effect", "string", false, "Name of the effect, spelled as the game's particle libraries spell it."),
                    v8pp::metadata::param("entityId", "number", false, "Engine entity to hang it on -- what `Player.entityId` reports. Entity ids are reused, so one is only meaningful while the entity is alive."),
                    v8pp::metadata::param("options", kVfxAttachOptionsType, true, "As `spawn`, plus `offset`: where the emitter sits in the entity's own space, in metres. Up to 8 m from its origin."),
                },
                "Plays an effect in a free render slot of an entity, where it follows that entity: smoke off a cart, a glow around a body, dust under a horse. The slot is freed when the duration runs out, when `stop` is called, or when the entity goes. "
                "The offset is measured from the entity's own origin, not from a bone, so an effect cannot be pinned to a hand or a weapon this way.",
                "A handle to pass to `stop`, or 0 when the entity was not there or the game would not load the effect."));

        vfx.function("stop", &Vfx::JS_Stop,
            v8pp::metadata::docs("boolean", {v8pp::metadata::param("handle", "number", false, "What `spawn` or `attach` returned.")}, "Stops one effect and releases what it was standing on.", "True when the handle named an effect that was still playing."));

        vfx.function("stopAll", &Vfx::JS_StopAll, v8pp::metadata::docs("void", {}, "Stops every effect this client started, including ones other resources started. The session ending does this too.", "Nothing."));

        vfx.function("isPlaying", &Vfx::JS_IsPlaying,
            v8pp::metadata::docs("boolean", {v8pp::metadata::param("handle", "number", false, "What `spawn` or `attach` returned.")},
                "Whether the effect behind a handle is still running. False once its duration has run out, its entity has gone, or the level has changed under it.", "True while it is playing."));

        vfx.function("has", &Vfx::JS_Has,
            v8pp::metadata::docs("boolean", {v8pp::metadata::param("effect", "string", false, "Name to look for.")},
                "Asks the game whether it can resolve an effect by this name, without playing it. False while no world is loaded, and for a name the catalog knows but this install's particle libraries no longer declare, so a resource that ships names can fail early rather than silently.",
                "True when the name resolves here and now."));

        vfx.function("list", &Vfx::JS_List,
            v8pp::metadata::docs("string[]", {v8pp::metadata::param("prefix", "string", true, "Keep only the names starting with this, e.g. `WH_Particels.fires` or `collisions.combat`.")},
                "Every effect name the shipped game declares, in order. Mined from the particle libraries at build time, so it is the same list on the server.", "The matching names."));

        vfx.function("count", &Vfx::JS_Count, v8pp::metadata::docs("number", {}, "How many effects this client is playing right now, across every resource. The budget is 192.", "The number of live effects."));

        vfx.publish(global);
    }
} // namespace KCDC::Scripting
