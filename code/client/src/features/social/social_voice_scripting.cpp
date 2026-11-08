#include "social_voice_scripting.h"
#include "core/scripting/script_context.h"

#include "features/player/player.h"
#include "features/player/player_scripting.h"
#include "features/ui/ui_hud_elements.h"

#include <core_modules.h>
#include <scripting/resource/resource_manager.h>
#include <scripting/scripting_catalog.h>
#include <voice/client/voice_client.h>

#include <v8pp/class.hpp>
#include <v8pp/convert.hpp>
#include <v8pp/module.hpp>

#include <cstdint>

namespace KCDC::Scripting {
    void Voice::JS_SetIndicatorVisible(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);

        if (info.Length() < 1 || !info[0]->IsBoolean()) {
            isolate->ThrowError(v8pp::to_v8(isolate, "Voice.setIndicatorVisible: expected (visible)"));
            return;
        }

        KCDC::Features::UI::HudElements::Get().SetVoiceIndicatorVisible(KCDC::ResolveCallingResource(isolate), info[0]->BooleanValue(isolate));
    }

    void Voice::JS_IsIndicatorVisible(const v8::FunctionCallbackInfo<v8::Value> &info) {
        info.GetReturnValue().Set(KCDC::Features::UI::HudElements::Get().IsVoiceIndicatorVisible());
    }

    void Voice::JS_IsPlayerTalking(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);

        const KCDC::Scripting::Player *player = info.Length() > 0 ? v8pp::class_<KCDC::Scripting::Player>::unwrap_object(isolate, info[0]) : nullptr;
        if (player == nullptr) {
            isolate->ThrowError(v8pp::to_v8(isolate, "Voice.isPlayerTalking: expected (player)"));
            return;
        }

        // Keyed as the nametag's icon keys it, by the body's owning peer. The
        // local player is not a talker this client plays back, so theirs is
        // the microphone's answer.
        const Framework::Voice::VoiceClient *voice = Framework::CoreModules::GetVoiceClient();
        const KCDC::Features::Player::Human *human = player->ResolveReplica();
        info.GetReturnValue().Set(voice != nullptr && human != nullptr && (human->IsOwner() ? voice->IsLocalTalking() : voice->IsSpeakerTalking(static_cast<std::uint64_t>(human->ownerGUID))));
    }

    void Voice::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        if (Framework::Scripting::ResourceManager *manager = KCDC::ScriptResources(); manager != nullptr) {
            KCDC::Features::UI::HudElements::Get().InstallCleanup(*manager);
        }

        // Attach onto the framework's own global Voice rather than shadowing
        // it. ClientScriptingModule::Init registers the framework's builtins
        // before any mod binding, so it is always there.
        v8::Local<v8::Context> context = isolate->GetCurrentContext();
        v8::Local<v8::Object> voice    = global->Get(context, v8pp::to_v8(isolate, "Voice")).ToLocalChecked().As<v8::Object>();
        auto &metadata                 = Framework::Scripting::GetScriptingCatalog(isolate).global_object("Voice");

        v8pp::bind_function(isolate, voice, metadata, "setIndicatorVisible", &Voice::JS_SetIndicatorVisible,
            v8pp::metadata::docs("void", {v8pp::metadata::param("visible", "boolean", false, "False hides it; true shows it again.")},
                "Hides or shows the mod's own voice indicator: the microphone low on the left while this player is sending, and the talking icon on the nametag of each player they hear. "
                "Hiding it is for a resource that draws its own from `isTalking`, `getTier`, `getInputLevel`, `isPlayerTalking` and the `voiceStart` and `voiceStop` events; voice itself carries on unchanged. "
                "A hidden indicator belongs to the resource that hid it, and comes back when that resource stops or the session ends. Throws unless `visible` is a boolean."));
        v8pp::bind_function(isolate, voice, metadata, "isIndicatorVisible", &Voice::JS_IsIndicatorVisible, v8pp::metadata::docs("boolean", {}, "Checks whether the mod draws its own voice indicator.", "True unless a resource hid it with `setIndicatorVisible(false)`."));
        v8pp::bind_function(isolate, voice, metadata, "isPlayerTalking", &Voice::JS_IsPlayerTalking,
            v8pp::metadata::docs("boolean", {v8pp::metadata::param("player", "Player", false, "Player to query.")},
                "Checks whether this player hears another player speaking right now -- the state the talking icon on their nametag follows, held across the pauses between words. "
                "A fact about this client's playback: someone out of range, muted for this player or beyond the talkers it plays at once reads as silent. For the local player it answers `isTalking`. Throws unless `player` is a Player.",
                "True while their voice is playing here."));
    }
} // namespace KCDC::Scripting
