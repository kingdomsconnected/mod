#include "audio_scripting.h"

#include "features/audio/audio_service.h"

#include "common/vector_math.h"

#include "shared/scripting/catalog.h"

#include <scripting/v8pp_types.h>

#include <glm/glm.hpp>

#include <v8pp/convert.hpp>
#include <v8pp/module.hpp>

#include <cstdint>
#include <string>

namespace KCDC::Scripting {
    namespace {
        bool IsTextAt(const v8::FunctionCallbackInfo<v8::Value> &info, int index) {
            return info.Length() > index && info[index]->IsString();
        }

        std::string TextAt(const v8::FunctionCallbackInfo<v8::Value> &info, int index) {
            return IsTextAt(info, index) ? v8pp::from_v8<std::string>(info.GetIsolate(), info[index]) : std::string {};
        }

        bool IsEntityIdAt(const v8::FunctionCallbackInfo<v8::Value> &info, int index) {
            return info.Length() > index && info[index]->IsNumber();
        }

        std::uint32_t EntityIdAt(const v8::FunctionCallbackInfo<v8::Value> &info, int index) {
            return static_cast<std::uint32_t>(v8pp::from_v8<double>(info.GetIsolate(), info[index]));
        }
    } // namespace

    void Audio::JS_Play(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);

        if (!IsTextAt(info, 0)) {
            isolate->ThrowError(v8pp::to_v8(isolate, "Audio.play: expected (trigger)"));
            return;
        }

        info.GetReturnValue().Set(KCDC::Features::Audio::AudioService::Play(TextAt(info, 0)));
    }

    void Audio::JS_PlayAt(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);

        if (!IsTextAt(info, 0) || info.Length() < 2 || !info[1]->IsObject()) {
            isolate->ThrowError(v8pp::to_v8(isolate, "Audio.playAt: expected (trigger, position)"));
            return;
        }

        const glm::vec3 position = v8pp::convert<glm::vec3>::from_v8(isolate, info[1]);
        if (!KCDC::Common::IsFinite(position)) {
            isolate->ThrowError(v8pp::to_v8(isolate, "Audio.playAt: position must be finite"));
            return;
        }

        info.GetReturnValue().Set(KCDC::Features::Audio::AudioService::PlayAt(TextAt(info, 0), position));
    }

    void Audio::JS_PlayOnEntity(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);

        if (!IsTextAt(info, 0) || !IsEntityIdAt(info, 1)) {
            isolate->ThrowError(v8pp::to_v8(isolate, "Audio.playOnEntity: expected (trigger, entityId)"));
            return;
        }

        info.GetReturnValue().Set(KCDC::Features::Audio::AudioService::PlayOnEntity(TextAt(info, 0), EntityIdAt(info, 1)));
    }

    void Audio::JS_PlayFile(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);

        if (!IsTextAt(info, 0) || !IsEntityIdAt(info, 1)) {
            isolate->ThrowError(v8pp::to_v8(isolate, "Audio.playFile: expected (file, entityId, purpose?)"));
            return;
        }

        const std::string purpose = IsTextAt(info, 2) ? TextAt(info, 2) : std::string("50");
        info.GetReturnValue().Set(KCDC::Features::Audio::AudioService::PlayFileOnEntity(TextAt(info, 0), EntityIdAt(info, 1), purpose));
    }

    void Audio::JS_StopOnEntity(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);

        if (!IsTextAt(info, 0) || !IsEntityIdAt(info, 1)) {
            isolate->ThrowError(v8pp::to_v8(isolate, "Audio.stopOnEntity: expected (trigger, entityId)"));
            return;
        }

        info.GetReturnValue().Set(KCDC::Features::Audio::AudioService::StopOnEntity(TextAt(info, 0), EntityIdAt(info, 1)));
    }

    void Audio::JS_StopAllOnEntity(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);

        if (!IsEntityIdAt(info, 0)) {
            isolate->ThrowError(v8pp::to_v8(isolate, "Audio.stopAllOnEntity: expected (entityId)"));
            return;
        }

        info.GetReturnValue().Set(KCDC::Features::Audio::AudioService::StopAllOnEntity(EntityIdAt(info, 0)));
    }

    void Audio::JS_SetEntityParameter(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);

        if (!IsEntityIdAt(info, 0) || !IsTextAt(info, 1) || info.Length() < 3 || !info[2]->IsNumber()) {
            isolate->ThrowError(v8pp::to_v8(isolate, "Audio.setEntityParameter: expected (entityId, parameter, value)"));
            return;
        }

        const float value = static_cast<float>(v8pp::from_v8<double>(isolate, info[2]));
        info.GetReturnValue().Set(KCDC::Features::Audio::AudioService::SetEntityRtpc(EntityIdAt(info, 0), TextAt(info, 1), value));
    }

    void Audio::JS_SetEntitySwitch(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);

        if (!IsEntityIdAt(info, 0) || !IsTextAt(info, 1) || !IsTextAt(info, 2)) {
            isolate->ThrowError(v8pp::to_v8(isolate, "Audio.setEntitySwitch: expected (entityId, switchName, state)"));
            return;
        }

        info.GetReturnValue().Set(KCDC::Features::Audio::AudioService::SetEntitySwitch(EntityIdAt(info, 0), TextAt(info, 1), TextAt(info, 2)));
    }

    void Audio::JS_HasTrigger(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);

        if (!IsTextAt(info, 0)) {
            isolate->ThrowError(v8pp::to_v8(isolate, "Audio.hasTrigger: expected (trigger)"));
            return;
        }

        info.GetReturnValue().Set(KCDC::Features::Audio::AudioService::ResolveTrigger(TextAt(info, 0)) != 0);
    }

    void Audio::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        if (isolate == nullptr || global.IsEmpty()) {
            return;
        }

        v8pp::module audio(isolate, ClientCatalog(), "Audio",
            "The game's own sound triggers, played at this machine. A trigger is a name the game's audio data declares -- `a_o_bell_kkut_kostelni`, `c_torch_whoosh1`, `f_ge_cough_woman` -- standing for an FMOD event. "
            "The full vocabulary is `Libs/GameAudio/*.xml` in `IPL_GameData.pak`, with the gameplay-facing subset listed in `Libs/Tables/GameAudio/SkaldAtlTrigger.xml`. "
            "Nothing here is replicated: a sound everyone should hear is one the server tells every client to play. Every call returns false while the audio system is not up, which is the case before a world is loaded, and for a trigger name the audio data does not declare.");

        audio.function("play", &Audio::JS_Play,
            v8pp::metadata::docs("boolean", {v8pp::metadata::param("trigger", "string", false, "Name of the trigger, spelled as the game's audio data spells it. Not text and not translated.")},
                "Plays a trigger with no position: heard at full volume wherever the player is, like a UI cue. It goes on the one global audio object every 2D sound in the game shares, which is why it cannot be stopped again.",
                "True when the audio system took it."));

        audio.function("playAt", &Audio::JS_PlayAt,
            v8pp::metadata::docs("boolean",
                {
                    v8pp::metadata::param("trigger", "string", false, "Name of the trigger, spelled as the game's audio data spells it."),
                    v8pp::metadata::param("position", "Vector3", false, "Where to play it, in world-space metres."),
                },
                "Plays a trigger once at a point in the world, positioned and attenuated against the player's ears. The sound is handed to the engine and forgotten: it stays where it was put, plays to its end, and cannot be stopped or moved. "
                "Occlusion is ignored rather than ray-traced, so a wall between the point and the player does not silence it.",
                "True when the audio system took it."));

        audio.function("playOnEntity", &Audio::JS_PlayOnEntity,
            v8pp::metadata::docs("boolean",
                {
                    v8pp::metadata::param("trigger", "string", false, "Name of the trigger, spelled as the game's audio data spells it."),
                    v8pp::metadata::param("entityId", "number", false, "Engine entity to hang it on -- what `Player.entityId` reports. Entity ids are reused, so one is only meaningful while the entity is alive."),
                },
                "Plays a trigger on an entity, giving it an audio component if it has none. The sound follows the entity for as long as it lasts and can be stopped again by name, which is what anything attached to a body, a horse or a prop wants.",
                "True when the entity was there and took it."));

        audio.function("playFile", &Audio::JS_PlayFile,
            v8pp::metadata::docs("boolean",
                {
                    v8pp::metadata::param("file", "string", false, "Game-root path of an `.ogg`, with or without the extension -- `sounds/kcdc/<resource>/line` for one a server streams."),
                    v8pp::metadata::param("entityId", "number", false, "Engine entity to play it on -- what `Player.entityId` reports."),
                    v8pp::metadata::param("purpose", "string", true, "Which of the game's voice host events carries it: `inner`, `10`, `20`, `35`, `50`, `100`, `200` (a bark heard up to that many metres), `sfp_dialog`, `sfp_cutscene` or `sfp_music`. Defaults to `50`."),
                },
                "Plays a sound file on an entity, positioned and attenuated like the game's own spoken lines and under the same volume settings. A file streamed by the server plays exactly like one the game ships. An entity takes one file per 0.1 seconds.",
                "True when the entity was there and took it."));

        audio.function("stopOnEntity", &Audio::JS_StopOnEntity,
            v8pp::metadata::docs("boolean",
                {
                    v8pp::metadata::param("trigger", "string", false, "Name of the trigger to stop."),
                    v8pp::metadata::param("entityId", "number", false, "Engine entity it was played on."),
                },
                "Stops every instance of one trigger on the entity. Triggers that have already finished are not an error.", "True when the entity was there to take it."));

        audio.function("stopAllOnEntity", &Audio::JS_StopAllOnEntity,
            v8pp::metadata::docs("boolean", {v8pp::metadata::param("entityId", "number", false, "Engine entity to silence.")},
                "Stops everything the entity is playing, the game's own sounds included. Use `stopOnEntity` when the trigger is known.",
                "True when the entity was there to take it."));

        audio.function("setEntityParameter", &Audio::JS_SetEntityParameter,
            v8pp::metadata::docs("boolean",
                {
                    v8pp::metadata::param("entityId", "number", false, "Engine entity whose sounds the parameter applies to."),
                    v8pp::metadata::param("parameter", "string", false, "Name of the continuous parameter, as `Libs/Tables/GameAudio/SkaldAtlRtpc.xml` lists it -- `horse_speed`, `player_stamina`, `wind_velocity`."),
                    v8pp::metadata::param("value", "number", false, "What to set it to. The range is whatever the FMOD event expects."),
                },
                "Sets one of the continuous parameters the game's FMOD events read, on this entity's sounds alone. It affects what is already playing as well as what starts later.",
                "True when both the parameter and the entity were there."));

        audio.function("setEntitySwitch", &Audio::JS_SetEntitySwitch,
            v8pp::metadata::docs("boolean",
                {
                    v8pp::metadata::param("entityId", "number", false, "Engine entity whose sounds the switch applies to."),
                    v8pp::metadata::param("switchName", "string", false, "Name of the switch, as `Libs/GameAudio/g_switches.xml` declares it -- `sword_mat`, `bow_type`."),
                    v8pp::metadata::param("state", "string", false, "Which of that switch's states to select. States belong to their switch, so the same name under a different switch is a different state."),
                },
                "Selects one of the discrete switch states the game's sounds branch on, for this entity alone.", "True when the switch, the state and the entity were all there."));

        audio.function("hasTrigger", &Audio::JS_HasTrigger,
            v8pp::metadata::docs("boolean", {v8pp::metadata::param("trigger", "string", false, "Name to look for.")},
                "Asks the audio system whether it knows a trigger by this name, without playing it. A patch or another mod can change what is declared, so a resource that ships names can fail early rather than silently.",
                "True when the name resolves. False also while the audio system is not up."));

        audio.publish(global);
    }
} // namespace KCDC::Scripting
