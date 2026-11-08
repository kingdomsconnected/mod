#include "core/scripting/script_context.h"
#include "ui_hud_scripting.h"

#include "ui_hud_elements.h"
#include "ui_hud_notifications.h"
#include "ui_hud_screen_effects.h"

#include "core/application.h"
#include "features/camera/camera_view.h"
#include "features/dialogue/dialogue_module.h"
#include "common/module_support.h"
#include "common/vector_math.h"
#include "sdk/wh/dialogmodule/c_dialog_manager.h"

#include "shared/scripting/catalog.h"

#include <gui/manager.h>
#include <scripting/resource/resource_manager.h>
#include <scripting/v8pp_types.h>

#include <glm/glm.hpp>

#include <v8pp/convert.hpp>
#include <v8pp/module.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace KCDC::Scripting {
    namespace {
        bool IsTextAt(const v8::FunctionCallbackInfo<v8::Value> &info, int index) {
            return info.Length() > index && info[index]->IsString();
        }

        std::string TextAt(const v8::FunctionCallbackInfo<v8::Value> &info, int index) {
            if (!IsTextAt(info, index)) {
                return {};
            }

            return v8pp::from_v8<std::string>(info.GetIsolate(), info[index]);
        }

        int NumberAt(const v8::FunctionCallbackInfo<v8::Value> &info, int index, int fallback) {
            if (info.Length() <= index || !info[index]->IsNumber()) {
                return fallback;
            }

            return static_cast<int>(v8pp::from_v8<double>(info.GetIsolate(), info[index]));
        }

        // Read from the SDK's table, so the documented list cannot drift from the one accepted.
        std::string ElementNames() {
            std::string names;
            for (const std::string_view name : SDK::wh::framework::E_HudElements::kNames) {
                names += names.empty() ? "`" : ", `";
                names += name;
                names += '`';
            }
            return names;
        }

        bool ElementAt(const v8::FunctionCallbackInfo<v8::Value> &info, int index, SDK::wh::framework::E_HudElements::Type &element) {
            return IsTextAt(info, index) && SDK::wh::framework::E_HudElements::FromString(TextAt(info, index), element);
        }

        // Every parameter any effect takes, so one bag type documents them all.
        constexpr const char *kParamsType = "{ saturation?: number; brightness?: number; contrast?: number; cyan?: number; magenta?: number; yellow?: number; darkness?: number; hue?: number; amount?: number }";

        void Throw(v8::Isolate *isolate, const std::string &message) {
            isolate->ThrowError(v8pp::to_v8(isolate, message));
        }

        std::uint32_t HandleArg(const v8::FunctionCallbackInfo<v8::Value> &info, int index) {
            if (info.Length() <= index || !info[index]->IsNumber()) {
                return KCDC::Features::UI::ScreenEffectStack::kNoHandle;
            }
            const double handle = v8pp::from_v8<double>(info.GetIsolate(), info[index]);
            return std::isfinite(handle) && handle > 0.0 && handle <= 4294967295.0 ? static_cast<std::uint32_t>(handle) : KCDC::Features::UI::ScreenEffectStack::kNoHandle;
        }

        /** The bag at `index`, an empty one when it is absent, or nothing (with an exception thrown) when it is something else. */
        std::optional<v8::Local<v8::Object>> BagAt(const v8::FunctionCallbackInfo<v8::Value> &info, int index, const char *what) {
            v8::Isolate *isolate = info.GetIsolate();
            if (info.Length() <= index || info[index]->IsUndefined() || info[index]->IsNull()) {
                return v8::Object::New(isolate);
            }
            if (!info[index]->IsObject()) {
                Throw(isolate, std::string(what) + " must be an object");
                return std::nullopt;
            }
            return info[index].As<v8::Object>();
        }

        /**
         * One duration in milliseconds off an options bag: `fallback` when the
         * key is absent, nothing (with an exception thrown) when it is not a
         * finite, non-negative number. Clamped to `max`.
         */
        std::optional<std::uint32_t> ReadMs(v8::Isolate *isolate, v8::Local<v8::Object> bag, const char *key, std::uint32_t fallback, std::uint32_t max, const char *verb) {
            v8::Local<v8::Value> value;
            if (!bag->Get(isolate->GetCurrentContext(), v8pp::to_v8(isolate, key)).ToLocal(&value) || value->IsUndefined()) {
                return fallback;
            }
            const double ms = value->IsNumber() ? v8pp::from_v8<double>(isolate, value) : -1.0;
            if (!std::isfinite(ms) || ms < 0.0) {
                Throw(isolate, std::string(verb) + ": " + key + " must be a non-negative number of milliseconds");
                return std::nullopt;
            }
            return static_cast<std::uint32_t>(std::min(ms, static_cast<double>(max)));
        }

        /**
         * Reads a parameter bag against one effect. Rejects a key the effect
         * does not take rather than ignoring it, so a misspelt `saturaton`
         * fails where it was written instead of silently doing nothing.
         */
        bool ReadParams(v8::Isolate *isolate, v8::Local<v8::Object> bag, const KCDC::Features::UI::ScreenEffectSpec &effect, KCDC::Features::UI::ScreenEffectStack::ParamValues &out, const char *verb) {
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            v8::Local<v8::Array> keys;
            if (!bag->GetOwnPropertyNames(context).ToLocal(&keys)) {
                return false;
            }

            for (std::uint32_t k = 0; k < keys->Length(); ++k) {
                v8::Local<v8::Value> key;
                if (!keys->Get(context, k).ToLocal(&key)) {
                    return false;
                }
                const std::string name = v8pp::from_v8<std::string>(isolate, key);

                std::size_t index = effect.params.size();
                for (std::size_t i = 0; i < effect.params.size(); ++i) {
                    if (effect.params[i].name == name) {
                        index = i;
                        break;
                    }
                }
                if (index == effect.params.size()) {
                    Throw(isolate, std::string(verb) + ": \"" + std::string(effect.name) + "\" has no parameter \"" + name + "\"");
                    return false;
                }

                v8::Local<v8::Value> value;
                if (!bag->Get(context, key).ToLocal(&value)) {
                    return false;
                }
                const double number = value->IsNumber() ? v8pp::from_v8<double>(isolate, value) : NAN;
                if (!std::isfinite(number)) {
                    Throw(isolate, std::string(verb) + ": " + name + " must be a finite number");
                    return false;
                }
                out.values[index] = static_cast<float>(number);
                out.given[index]  = true;
            }
            return true;
        }

        v8::Local<v8::Object> DescribeEffect(v8::Isolate *isolate, const KCDC::Features::UI::ScreenEffectSpec &effect) {
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            v8::Local<v8::Array> params    = v8::Array::New(isolate, static_cast<int>(effect.params.size()));
            for (std::size_t i = 0; i < effect.params.size(); ++i) {
                const KCDC::Features::UI::ScreenEffectParam &param = effect.params[i];
                v8::Local<v8::Object> entry                            = v8::Object::New(isolate);
                entry->Set(context, v8pp::to_v8(isolate, "name"), v8pp::to_v8(isolate, std::string(param.name))).Check();
                entry->Set(context, v8pp::to_v8(isolate, "description"), v8pp::to_v8(isolate, std::string(param.description))).Check();
                entry->Set(context, v8pp::to_v8(isolate, "min"), v8pp::to_v8(isolate, param.min)).Check();
                entry->Set(context, v8pp::to_v8(isolate, "max"), v8pp::to_v8(isolate, param.max)).Check();
                entry->Set(context, v8pp::to_v8(isolate, "default"), v8pp::to_v8(isolate, param.fallback)).Check();
                entry->Set(context, v8pp::to_v8(isolate, "neutral"), v8pp::to_v8(isolate, param.neutral)).Check();
                params->Set(context, static_cast<std::uint32_t>(i), entry).Check();
            }

            v8::Local<v8::Object> result = v8::Object::New(isolate);
            result->Set(context, v8pp::to_v8(isolate, "name"), v8pp::to_v8(isolate, std::string(effect.name))).Check();
            result->Set(context, v8pp::to_v8(isolate, "description"), v8pp::to_v8(isolate, std::string(effect.description))).Check();
            result->Set(context, v8pp::to_v8(isolate, "params"), params).Check();
            return result;
        }
    } // namespace

    void Hud::JS_ShowNotification(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);

        if (!IsTextAt(info, 0)) {
            isolate->ThrowError(v8pp::to_v8(isolate, "Hud.showNotification: expected (message)"));
            return;
        }

        info.GetReturnValue().Set(KCDC::Features::UI::Hud::ShowNotification(TextAt(info, 0)));
    }

    void Hud::JS_ShowInfoText(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);

        if (!IsTextAt(info, 0)) {
            isolate->ThrowError(v8pp::to_v8(isolate, "Hud.showInfoText: expected (message, durationMs?, priority?)"));
            return;
        }

        info.GetReturnValue().Set(KCDC::Features::UI::Hud::ShowInfoText(TextAt(info, 0), NumberAt(info, 1, 3000), NumberAt(info, 2, 0)));
    }

    void Hud::JS_HideInfoText(const v8::FunctionCallbackInfo<v8::Value> &info) {
        info.GetReturnValue().Set(KCDC::Features::UI::Hud::HideInfoText());
    }

    void Hud::JS_ShowPerkUsed(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);

        if (!IsTextAt(info, 0) || !IsTextAt(info, 1)) {
            isolate->ThrowError(v8pp::to_v8(isolate, "Hud.showPerkUsed: expected (iconName, name)"));
            return;
        }

        info.GetReturnValue().Set(KCDC::Features::UI::Hud::ShowPerkUsed(TextAt(info, 0), TextAt(info, 1)));
    }

    void Hud::JS_ShowPerkGained(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);

        if (!IsTextAt(info, 0) || !IsTextAt(info, 1)) {
            isolate->ThrowError(v8pp::to_v8(isolate, "Hud.showPerkGained: expected (iconName, name)"));
            return;
        }

        info.GetReturnValue().Set(KCDC::Features::UI::Hud::ShowPerkGained(TextAt(info, 0), TextAt(info, 1)));
    }

    void Hud::JS_ShowComboLearned(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);

        if (!IsTextAt(info, 0) || !IsTextAt(info, 1)) {
            isolate->ThrowError(v8pp::to_v8(isolate, "Hud.showComboLearned: expected (iconName, name)"));
            return;
        }

        info.GetReturnValue().Set(KCDC::Features::UI::Hud::ShowComboLearned(TextAt(info, 0), TextAt(info, 1)));
    }

    void Hud::JS_ShowReputationChanged(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);

        if (info.Length() < 2 || !info[0]->IsNumber() || !IsTextAt(info, 1)) {
            isolate->ThrowError(v8pp::to_v8(isolate, "Hud.showReputationChanged: expected (eventId, message)"));
            return;
        }

        info.GetReturnValue().Set(KCDC::Features::UI::Hud::ShowReputationChanged(NumberAt(info, 0, 0), TextAt(info, 1)));
    }

    void Hud::JS_ShowRandomEventResult(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);

        if (info.Length() < 3 || !info[0]->IsNumber() || !IsTextAt(info, 1) || !IsTextAt(info, 2)) {
            isolate->ThrowError(v8pp::to_v8(isolate, "Hud.showRandomEventResult: expected (type, result, name)"));
            return;
        }

        info.GetReturnValue().Set(KCDC::Features::UI::Hud::ShowRandomEventResult(NumberAt(info, 0, 0), TextAt(info, 1), TextAt(info, 2)));
    }

    void Hud::JS_ShowXpGain(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);

        if (!IsTextAt(info, 0) || !IsTextAt(info, 1)) {
            isolate->ThrowError(v8pp::to_v8(isolate, "Hud.showXpGain: expected (stat, statName, type?)"));
            return;
        }

        info.GetReturnValue().Set(KCDC::Features::UI::Hud::ShowXpGain(TextAt(info, 0), TextAt(info, 1), NumberAt(info, 2, 0)));
    }

    void Hud::JS_ShowTutorial(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);

        if (!IsTextAt(info, 0)) {
            isolate->ThrowError(v8pp::to_v8(isolate, "Hud.showTutorial: expected (name)"));
            return;
        }

        info.GetReturnValue().Set(KCDC::Features::UI::Hud::ShowTutorial(TextAt(info, 0)));
    }

    void Hud::JS_HideTutorial(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);

        if (!IsTextAt(info, 0)) {
            isolate->ThrowError(v8pp::to_v8(isolate, "Hud.hideTutorial: expected (name)"));
            return;
        }

        info.GetReturnValue().Set(KCDC::Features::UI::Hud::HideTutorial(TextAt(info, 0)));
    }

    void Hud::JS_HideCurrentTutorial(const v8::FunctionCallbackInfo<v8::Value> &info) {
        info.GetReturnValue().Set(KCDC::Features::UI::Hud::HideCurrentTutorial());
    }

    void Hud::JS_HideCodexActionHint(const v8::FunctionCallbackInfo<v8::Value> &info) {
        info.GetReturnValue().Set(KCDC::Features::UI::Hud::HideCodexActionHint());
    }

    void Hud::JS_ClearNotifications(const v8::FunctionCallbackInfo<v8::Value> &info) {
        info.GetReturnValue().Set(KCDC::Features::UI::Hud::ClearNotificationQueue());
    }

    void Hud::JS_SetElementVisible(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);

        SDK::wh::framework::E_HudElements::Type element {};
        if (!ElementAt(info, 0, element) || info.Length() < 2 || !info[1]->IsBoolean()) {
            isolate->ThrowError(v8pp::to_v8(isolate, "Hud.setElementVisible: expected (element, visible) with a known element name"));
            return;
        }

        info.GetReturnValue().Set(KCDC::Features::UI::HudElements::Get().SetVisible(KCDC::ResolveCallingResource(isolate), element, info[1]->BooleanValue(isolate)));
    }

    void Hud::JS_IsElementVisible(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);

        SDK::wh::framework::E_HudElements::Type element {};
        if (!ElementAt(info, 0, element)) {
            isolate->ThrowError(v8pp::to_v8(isolate, "Hud.isElementVisible: expected (element) with a known element name"));
            return;
        }

        info.GetReturnValue().Set(KCDC::Features::UI::HudElements::Get().IsVisible(element));
    }

    void Hud::JS_SetMonologueEnabled(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);

        if (info.Length() < 1 || !info[0]->IsBoolean()) {
            isolate->ThrowError(v8pp::to_v8(isolate, "Hud.setMonologueEnabled: expected (enabled)"));
            return;
        }

        KCDC::Features::UI::HudElements::Get().SetMonologueEnabled(info[0]->BooleanValue(isolate));
    }

    void Hud::JS_IsMonologueEnabled(const v8::FunctionCallbackInfo<v8::Value> &info) {
        info.GetReturnValue().Set(KCDC::Features::UI::HudElements::Get().IsMonologueEnabled());
    }

    void Hud::JS_SetStoryCutscenesEnabled(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);

        if (info.Length() < 1 || !info[0]->IsBoolean()) {
            isolate->ThrowError(v8pp::to_v8(isolate, "Hud.setStoryCutscenesEnabled: expected (enabled)"));
            return;
        }

        KCDC::Features::UI::HudElements::Get().SetStoryCutscenesEnabled(info[0]->BooleanValue(isolate));
    }

    void Hud::JS_IsStoryCutscenesEnabled(const v8::FunctionCallbackInfo<v8::Value> &info) {
        info.GetReturnValue().Set(KCDC::Features::UI::HudElements::Get().IsStoryCutscenesEnabled());
    }

    void Hud::JS_IsInDialogue(const v8::FunctionCallbackInfo<v8::Value> &info) {
        // Two conversations can own the screen and a caller cares about either:
        // one this mod is driving, and one the game started by itself. Reporting
        // only the first would let a resource act during a real conversation.
        const bool mod                                        = KCDC::Features::Dialogue::DialogueModule::Get().IsShowing();
        const SDK::wh::dialogmodule::C_DialogManager *manager = SDK::wh::dialogmodule::C_DialogManager::GetInstance();
        const bool native                                     = manager != nullptr && manager->IsPlayerInDialog();
        info.GetReturnValue().Set(mod || native);
    }

    void Hud::JS_ScreenEffects(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = isolate->GetCurrentContext();

        v8::Local<v8::Array> effects = v8::Array::New(isolate);
        std::uint32_t written        = 0;
        for (const KCDC::Features::UI::ScreenEffectSpec &effect : KCDC::Features::UI::ScreenEffectStack::Effects()) {
            effects->Set(context, written++, DescribeEffect(isolate, effect)).Check();
        }
        info.GetReturnValue().Set(effects);
    }

    void Hud::JS_AddScreenEffect(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);

        if (info.Length() < 1 || !info[0]->IsString()) {
            Throw(isolate, "Hud.addScreenEffect: expected (effect, params?, options?)");
            return;
        }

        const std::string name                                 = v8pp::from_v8<std::string>(isolate, info[0]);
        const KCDC::Features::UI::ScreenEffectSpec *effect = KCDC::Features::UI::ScreenEffectStack::FindEffect(name);
        if (effect == nullptr) {
            Throw(isolate, "Hud.addScreenEffect: \"" + name + "\" is not a screen effect; Hud.screenEffects() lists them");
            return;
        }

        const std::optional<v8::Local<v8::Object>> params  = BagAt(info, 1, "Hud.addScreenEffect: params");
        const std::optional<v8::Local<v8::Object>> options = params ? BagAt(info, 2, "Hud.addScreenEffect: options") : std::nullopt;
        if (!params || !options) {
            return;
        }

        KCDC::Features::UI::ScreenEffectStack::ParamValues values;
        if (!ReadParams(isolate, *params, *effect, values, "Hud.addScreenEffect")) {
            return;
        }

        const std::optional<std::uint32_t> fadeIn   = ReadMs(isolate, *options, "fadeInMs", 0, KCDC::Features::UI::ScreenEffectStack::kMaxFadeMs, "Hud.addScreenEffect");
        const std::optional<std::uint32_t> fadeOut  = fadeIn ? ReadMs(isolate, *options, "fadeOutMs", 0, KCDC::Features::UI::ScreenEffectStack::kMaxFadeMs, "Hud.addScreenEffect") : std::nullopt;
        const std::optional<std::uint32_t> duration = fadeOut ? ReadMs(isolate, *options, "durationMs", 0, KCDC::Features::UI::ScreenEffectStack::kMaxDurationMs, "Hud.addScreenEffect") : std::nullopt;
        if (!duration) {
            return;
        }

        KCDC::Features::UI::ScreenEffectStack::Timing timing;
        timing.fadeInMs   = *fadeIn;
        timing.fadeOutMs  = *fadeOut;
        timing.durationMs = *duration;
        info.GetReturnValue().Set(KCDC::Features::UI::HudScreenEffects::Get().Create(KCDC::ResolveCallingResource(isolate), *effect, values, timing));
    }

    void Hud::JS_UpdateScreenEffect(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);

        if (info.Length() < 2 || !info[0]->IsNumber() || !info[1]->IsObject()) {
            Throw(isolate, "Hud.updateScreenEffect: expected (handle, params, options?)");
            return;
        }

        KCDC::Features::UI::HudScreenEffects &module = KCDC::Features::UI::HudScreenEffects::Get();
        const std::uint32_t handle                          = HandleArg(info, 0);
        const std::string resource                          = KCDC::ResolveCallingResource(isolate);
        const KCDC::Features::UI::ScreenEffectSpec *effect = module.EffectOf(resource, handle);
        if (effect == nullptr) {
            info.GetReturnValue().Set(false);
            return;
        }

        const std::optional<v8::Local<v8::Object>> options = BagAt(info, 2, "Hud.updateScreenEffect: options");
        if (!options) {
            return;
        }

        KCDC::Features::UI::ScreenEffectStack::ParamValues values;
        if (!ReadParams(isolate, info[1].As<v8::Object>(), *effect, values, "Hud.updateScreenEffect")) {
            return;
        }

        const std::optional<std::uint32_t> fade = ReadMs(isolate, *options, "fadeMs", 0, KCDC::Features::UI::ScreenEffectStack::kMaxFadeMs, "Hud.updateScreenEffect");
        if (!fade) {
            return;
        }
        info.GetReturnValue().Set(module.Retune(resource, handle, values, *fade));
    }

    void Hud::JS_RemoveScreenEffect(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);

        if (info.Length() < 1 || !info[0]->IsNumber()) {
            Throw(isolate, "Hud.removeScreenEffect: expected (handle, options?)");
            return;
        }

        const std::optional<v8::Local<v8::Object>> options = BagAt(info, 1, "Hud.removeScreenEffect: options");
        if (!options) {
            return;
        }

        // An absent fadeOutMs means the one the effect was created with.
        v8::Local<v8::Value> given;
        const bool ownFade = !(*options)->Get(isolate->GetCurrentContext(), v8pp::to_v8(isolate, "fadeOutMs")).ToLocal(&given) || given->IsUndefined();
        const std::optional<std::uint32_t> fade = ReadMs(isolate, *options, "fadeOutMs", 0, KCDC::Features::UI::ScreenEffectStack::kMaxFadeMs, "Hud.removeScreenEffect");
        if (!fade) {
            return;
        }
        info.GetReturnValue().Set(KCDC::Features::UI::HudScreenEffects::Get().Remove(KCDC::ResolveCallingResource(isolate), HandleArg(info, 0), ownFade, *fade));
    }

    void Hud::JS_RemoveAllScreenEffects(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);
        info.GetReturnValue().Set(static_cast<std::uint32_t>(KCDC::Features::UI::HudScreenEffects::Get().RemoveAll(KCDC::ResolveCallingResource(isolate))));
    }

    void Hud::JS_IsScreenEffectActive(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::HandleScope handleScope(info.GetIsolate());
        info.GetReturnValue().Set(KCDC::Features::UI::HudScreenEffects::Get().IsActive(HandleArg(info, 0)));
    }

    void Hud::JS_WorldToScreen(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);

        if (info.Length() < 1 || !info[0]->IsObject()) {
            Throw(isolate, "Hud.worldToScreen: expected (point)");
            return;
        }
        const glm::vec3 point = v8pp::convert<glm::vec3>::from_v8(isolate, info[0]);
        if (!KCDC::Common::IsFinite(point)) {
            Throw(isolate, "Hud.worldToScreen: point must be finite");
            return;
        }

        // The pixels Web views are placed in, which follow the window's client area.
        Core::Application *application = Core::gApplication.get();
        const Framework::GUI::Manager *web = application != nullptr ? application->GetWebManager() : nullptr;
        const Framework::GUI::ViewportConfiguration viewport = web != nullptr ? web->GetViewportConfiguration() : Framework::GUI::ViewportConfiguration {};

        KCDC::Features::Camera::ScreenPoint screen;
        if (viewport.width <= 0 || viewport.height <= 0 || !KCDC::Features::Camera::ProjectToScreen(KCDC::Common::ToNative(point), screen)) {
            info.GetReturnValue().SetNull();
            return;
        }

        v8::Local<v8::Context> context = isolate->GetCurrentContext();
        v8::Local<v8::Object> result   = v8::Object::New(isolate);
        result->Set(context, v8pp::to_v8(isolate, "x"), v8pp::to_v8(isolate, screen.x * static_cast<float>(viewport.width))).Check();
        result->Set(context, v8pp::to_v8(isolate, "y"), v8pp::to_v8(isolate, screen.y * static_cast<float>(viewport.height))).Check();
        result->Set(context, v8pp::to_v8(isolate, "onScreen"), v8pp::to_v8(isolate, screen.onScreen)).Check();
        info.GetReturnValue().Set(result);
    }

    void Hud::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        if (isolate == nullptr || global.IsEmpty()) {
            return;
        }

        if (Framework::Scripting::ResourceManager *manager = KCDC::ScriptResources(); manager != nullptr) {
            KCDC::Features::UI::HudElements::Get().InstallCleanup(*manager);
        }

        v8pp::module hud(isolate, ClientCatalog(), "Hud",
            "The game's own HUD messages, shown to the player at this machine. Nothing here is replicated: a message everyone should see is sent to everyone, and each client shows it. Every message call returns false while there is no HUD, which is the case in the main menu and across a level load.\n\nThe screen-effect verbs -- `addScreenEffect` and its siblings -- put full-screen post effects on this player's own picture. They are drawn by the game's renderer rather than the HUD, so they need no HUD and hiding HUD elements leaves them alone.");

        hud.function("isInDialogue", &Hud::JS_IsInDialogue,
            v8pp::metadata::docs("boolean", {},
                "Whether this player is in a conversation right now -- one a server resource opened through `Dialogue`, or one the game started by itself. "
                "Both count, because a resource that acts during the game's own dialogue is as wrong as one that acts during ours. "
                "The game's half is read from its own dialogue manager, so it is true for real NPC conversations as well.",
                "True while a conversation owns the screen."));

        hud.function("showNotification", &Hud::JS_ShowNotification, v8pp::metadata::docs("boolean", {v8pp::metadata::param("message", "string", false, "Text to show, as written.")}, "Shows the plain notification: one line, no icon, no payload.", "True when the HUD took it."));
        hud.function("showInfoText", &Hud::JS_ShowInfoText,
            v8pp::metadata::docs("boolean",
                {
                    v8pp::metadata::param("message", "string", false, "Text to show, as written."),
                    v8pp::metadata::param("durationMs", "number", true, "How long to hold it, in real milliseconds; defaults to 3000."),
                    v8pp::metadata::param("priority", "number", true, "What an already-showing message does to this one; defaults to 0."),
                },
                "Shows the single line of info text above the HUD.", "True when the HUD took it."));
        hud.function("hideInfoText", &Hud::JS_HideInfoText, v8pp::metadata::docs("boolean", {}, "Takes the info text down before its time is up.", "True when the HUD took it."));

        hud.function("showPerkUsed", &Hud::JS_ShowPerkUsed,
            v8pp::metadata::docs("boolean",
                {
                    v8pp::metadata::param("iconName", "string", false, "Icon from the game's own atlas, by name -- `perk_alchemist`, say. It is not text and is not translated."),
                    v8pp::metadata::param("name", "string", false, "Line beside the icon, as written."),
                },
                "Shows the perk-used toast: an icon and a line.", "True when the HUD took it."));
        hud.function("showPerkGained", &Hud::JS_ShowPerkGained,
            v8pp::metadata::docs("boolean",
                {
                    v8pp::metadata::param("iconName", "string", false, "Icon from the game's own atlas, by name."),
                    v8pp::metadata::param("name", "string", false, "Line beside the icon, as written."),
                },
                "Shows the perk-gained toast, which is the one the game plays when a perk is earned.", "True when the HUD took it."));
        hud.function("showComboLearned", &Hud::JS_ShowComboLearned,
            v8pp::metadata::docs("boolean",
                {
                    v8pp::metadata::param("iconName", "string", false, "Icon from the game's own atlas, by name."),
                    v8pp::metadata::param("name", "string", false, "Line beside the icon, as written."),
                },
                "Shows the combo-learned toast.", "True when the HUD took it."));

        hud.function("showReputationChanged", &Hud::JS_ShowReputationChanged,
            v8pp::metadata::docs("boolean",
                {
                    v8pp::metadata::param("eventId", "number", false, "Which reputation event the HUD draws it as."),
                    v8pp::metadata::param("message", "string", false, "Line to show, as written."),
                },
                "Shows the reputation-change message.", "True when the HUD took it."));
        hud.function("showRandomEventResult", &Hud::JS_ShowRandomEventResult,
            v8pp::metadata::docs("boolean",
                {
                    v8pp::metadata::param("type", "number", false, "Which result the HUD draws it as."),
                    v8pp::metadata::param("result", "string", false, "Result line, as written."),
                    v8pp::metadata::param("name", "string", false, "Name of what it was, as written."),
                },
                "Shows the random-event result panel.", "True when the HUD took it."));
        hud.function("showXpGain", &Hud::JS_ShowXpGain,
            v8pp::metadata::docs("boolean",
                {
                    v8pp::metadata::param("stat", "string", false, "The stat's own unique name -- `str`, say. It is an identifier, not text, and is not translated."),
                    v8pp::metadata::param("statName", "string", false, "What to call it on screen, as written."),
                    v8pp::metadata::param("type", "number", true, "Which XP bar the HUD draws it against; defaults to 0."),
                },
                "Shows the experience-gain toast.", "True when the HUD took it."));

        hud.function("showTutorial", &Hud::JS_ShowTutorial,
            v8pp::metadata::docs("boolean", {v8pp::metadata::param("name", "string", false, "The tutorial's own name in the game's tables, such as `OB_O20_Inventory`. Not text.")},
                "Shows one of the game's tutorials. This drives the HUD slot directly, which is past whatever the mod suppresses at the source.", "True when the HUD took it."));
        hud.function("hideTutorial", &Hud::JS_HideTutorial, v8pp::metadata::docs("boolean", {v8pp::metadata::param("name", "string", false, "The tutorial's own name in the game's tables.")}, "Takes one named tutorial down.", "True when the HUD took it."));
        hud.function("hideCurrentTutorial", &Hud::JS_HideCurrentTutorial, v8pp::metadata::docs("boolean", {}, "Takes down whichever tutorial is showing, whatever it is.", "True when the HUD took it."));
        hud.function("hideCodexActionHint", &Hud::JS_HideCodexActionHint, v8pp::metadata::docs("boolean", {}, "Takes down the codex action hint.", "True when the HUD took it."));

        hud.function("clearNotifications", &Hud::JS_ClearNotifications,
            v8pp::metadata::docs("boolean", {}, "Empties the queue of notifications waiting to be shown. It is the only piece of the HUD's own state reachable from here; everything else is one-shot.", "True when the HUD took it."));

        hud.function("setElementVisible", &Hud::JS_SetElementVisible,
            v8pp::metadata::docs("boolean",
                {
                    v8pp::metadata::param("element", "string", false, "The element, by the game's own name for it: `Stats` is the health, stamina and nourishment bars along the bottom. One of " + ElementNames() + ", compared exactly."),
                    v8pp::metadata::param("visible", "boolean", false, "False hides the element, true lets the game show it again."),
                },
                "Retains a visibility request for one HUD element, including before the HUD is ready. Hidden clips are suppressed before each HUD render, across movie and level reloads. "
                "Turning an element on releases multiplayer suppression; the current native HUD rules still apply. Suppression is released when its owning resource stops or the session ends. No console variables are changed.",
                "True when the request is retained; application waits for the native HUD when it is not ready yet."));
        hud.function("isElementVisible", &Hud::JS_IsElementVisible,
            v8pp::metadata::docs("boolean", {v8pp::metadata::param("element", "string", false, "The element, by the game's own name for it, as `setElementVisible` takes it.")},
                "Whether multiplayer permits the element to show. True when no resource suppresses it; native gameplay rules can still hide it. This is not a pixel-visibility query.", "True while multiplayer allows the element to show."));

        hud.function("setMonologueEnabled", &Hud::JS_SetMonologueEnabled,
            v8pp::metadata::docs("void", {v8pp::metadata::param("enabled", "boolean", false, "True lets the game voice and subtitle it again; false suppresses it.")},
                "The local character's own monologue -- photo-mode comments, the sharpening remark -- is spoken in Henry's voice and captioned with his name whoever is playing, so multiplayer suppresses it and raises each line as `monologueSuppressed`. Releasing it lasts until the session ends."));
        hud.function("isMonologueEnabled", &Hud::JS_IsMonologueEnabled, v8pp::metadata::docs("boolean", {}, "Whether the game voices and subtitles the local character's monologue.", "False while multiplayer suppresses it, which is the default."));
        hud.function("setStoryCutscenesEnabled", &Hud::JS_SetStoryCutscenesEnabled,
            v8pp::metadata::docs("void", {v8pp::metadata::param("enabled", "boolean", false, "True lets the quests play their cutscenes again; false suppresses them.")},
                "A story cutscene -- the one a quest plays when the player walks into its trigger -- puts the player into the game's cutscene state, can hold the world clock and loads the quest's own scenery, so multiplayer suppresses it and raises each one as `storyCutsceneSuppressed`. "
                "Everything a quest queues is one, its fades and time skips included; fast travel, sleeping and waiting that the player starts are not, and always play. A suppressed cutscene is not played later when this is released. Releasing it lasts until the session ends."));
        hud.function("isStoryCutscenesEnabled", &Hud::JS_IsStoryCutscenesEnabled, v8pp::metadata::docs("boolean", {}, "Whether the quests may play their cutscenes on this machine.", "False while multiplayer suppresses them, which is the default."));

        if (Framework::Scripting::ResourceManager *manager = KCDC::ScriptResources(); manager != nullptr) {
            KCDC::Features::UI::HudScreenEffects::Get().InstallCleanup(*manager);
        }

        auto &param = ClientCatalog().data_type("ScreenEffectParam", "One parameter a screen effect takes.");
        param.add_property("name", "string", "The key to pass it under in a params bag.");
        param.add_property("description", "string", "What it does to the picture.");
        param.add_property("min", "number", "The lowest value it takes. Anything lower is clamped.");
        param.add_property("max", "number", "The highest value it takes. Anything higher is clamped.");
        param.add_property("default", "number", "What `addScreenEffect` uses when the parameter is not given.");
        param.add_property("neutral", "number", "The value at which it leaves the picture alone, which is also where a fade starts and ends.");

        auto &effect = ClientCatalog().data_type("ScreenEffect", "One screen effect `Hud.addScreenEffect` accepts.");
        effect.add_property("name", "string", "What to pass to `addScreenEffect`.");
        effect.add_property("description", "string", "What it does, and how several of it combine.");
        effect.add_property("params", "ScreenEffectParam[]", "Every parameter it takes, with its range.");

        hud.function("screenEffects", &Hud::JS_ScreenEffects, v8pp::metadata::docs("ScreenEffect[]", {}, "Every effect `addScreenEffect` accepts, with each parameter's range, default and neutral value.", "The effects, in a fixed order."));

        hud.function("addScreenEffect", &Hud::JS_AddScreenEffect,
            v8pp::metadata::docs("number",
                {
                    v8pp::metadata::param("effect", "\"colorGrade\" | \"blur\" | \"ghosting\" | \"screenBlood\"", false, "Which effect, as `screenEffects` names it."),
                    v8pp::metadata::param("params", kParamsType, true, "Values for the effect's own parameters, each clamped into its range. A parameter left out takes its default; one the effect does not take is an error."),
                    v8pp::metadata::param("options", "{ fadeInMs?: number; fadeOutMs?: number; durationMs?: number }", true,
                        "`fadeInMs` brings it up from nothing; `fadeOutMs` is how long it takes to go, when its duration runs out or when `removeScreenEffect` is called without a fade of its own; `durationMs` ends it on its own after that long, before its fade-out -- 0, the default, keeps it until it is removed. Fades are up to a minute."),
                },
                "Puts a full-screen effect on this player's own picture -- a colour grade, a blur, ghosting or the game's blood overlay, for intoxication, injury, poison, dreams or a place that should feel wrong -- owned by the calling resource. These are the game's own post-processing passes, driven with the parameters this build actually reads; they are not part of the HUD and showing or hiding HUD elements does not touch them.\n\nThis is presentation, not state. Nothing is replicated, nobody else sees it, and none of it makes a player drunk or hurt: whatever decides that -- usually the server -- keeps it, and a client script asks for the picture to match. Weather and time of day are shared world state and are not touched.\n\nEffects from every resource combine rather than overwrite each other, on top of the game's own grade -- which already desaturates the screen as health runs low -- rather than in its place: brightness, contrast and saturation multiply, the colour cast and the hue shift add, and blur, ghosting and blood each draw the strongest one asked for. The result does not depend on which resource asked first, and removing one effect leaves the game's grade and every other effect as they were.\n\nAn effect belongs to the resource that created it: only that resource can change or remove it, and it goes when the resource stops, when the session ends or when the level changes. A cinematic that resets the game's post-processing clears the screen until the effect next changes. Up to 64 effects are kept across every resource.", "A handle for `updateScreenEffect`, `removeScreenEffect` and `isScreenEffectActive`, or 0 when 64 effects are already on screen."));

        hud.function("updateScreenEffect", &Hud::JS_UpdateScreenEffect,
            v8pp::metadata::docs("boolean",
                {
                    v8pp::metadata::param("handle", "number", false, "What `addScreenEffect` returned."),
                    v8pp::metadata::param("params", kParamsType, false, "New values for some of the effect's parameters. The others keep theirs."),
                    v8pp::metadata::param("options", "{ fadeMs?: number }", true, "`fadeMs` moves the parameters to their new values over that long, from wherever they are now; 0, the default, moves them at once."),
                },
                "Changes an effect while it is on screen -- a drunkenness that deepens with every drink, a wound that worsens.", "False when the handle is gone or belongs to another resource."));

        hud.function("removeScreenEffect", &Hud::JS_RemoveScreenEffect,
            v8pp::metadata::docs("boolean",
                {
                    v8pp::metadata::param("handle", "number", false, "What `addScreenEffect` returned."),
                    v8pp::metadata::param("options", "{ fadeOutMs?: number }", true, "How long it takes to fade out. Left out, it fades over the `fadeOutMs` it was created with; 0 takes it off at once."),
                },
                "Takes an effect off the screen. An effect already fading out carries on from where it is.", "False when the handle is gone or belongs to another resource."));

        hud.function("removeAllScreenEffects", &Hud::JS_RemoveAllScreenEffects, v8pp::metadata::docs("number", {}, "Takes every effect the calling resource owns off the screen at once. Other resources' effects stay.", "How many were removed."));

        hud.function("isScreenEffectActive", &Hud::JS_IsScreenEffectActive,
            v8pp::metadata::docs("boolean", {v8pp::metadata::param("handle", "number", false, "What `addScreenEffect` returned.")}, "Whether an effect is still on screen, fading out included.", "False once it has been removed and faded, has run its duration out, or was dropped by its resource stopping, the session ending or the level changing."));

        auto &screenPoint = ClientCatalog().data_type("ScreenPoint", "Where a world point lands on screen, in the pixels Web views are placed in.");
        screenPoint.add_property("x", "number", "Pixels from the left edge of the window's client area.");
        screenPoint.add_property("y", "number", "Pixels from the top edge of the window's client area.");
        screenPoint.add_property("onScreen", "boolean", "Whether the point is inside the view. One past an edge keeps its coordinates with this false.");

        hud.function("worldToScreen", &Hud::JS_WorldToScreen, v8pp::metadata::docs("ScreenPoint | null", {v8pp::metadata::param("point", "Vector3", false, "World position to project.")}, "Projects a world point through the camera this frame is drawn with, so a resource can pin its own overlay to the world.", "Its screen position, or null when the point is behind the camera or there is no view to place it in."));

        hud.publish(global);

        auto &events = ClientCatalog().data_type("EventMap");
        events.add_property("monologueSuppressed", "[text: string, textKey: string]",
            "Dispatched when a monologue line of the local character -- a photo-mode comment, the sharpening remark -- was kept silent and unsubtitled. `text` is localized; `textKey` is its localization key. Not raised while `Hud.setMonologueEnabled(true)` has released it. Handler promises are not awaited.");
        events.add_property("storyCutsceneSuppressed", "[cutscene: string]",
            "Dispatched when a quest asked to play a story cutscene and multiplayer kept it from playing, so the player stays in control and the world clock runs on. `cutscene` is its name in the game's cutscene table. Not raised while `Hud.setStoryCutscenesEnabled(true)` has released them. Handler promises are not awaited.");
    }

    void Events::StoryCutsceneSuppressed(const std::string &cutscene) {
        EmitReservedEvent("storyCutsceneSuppressed", [&cutscene](v8::Isolate *isolate) {
            return std::vector<v8::Local<v8::Value>> {v8pp::to_v8(isolate, cutscene)};
        });
    }

    void Events::MonologueSuppressed(const std::string &text, const std::string &textKey) {
        EmitReservedEvent("monologueSuppressed", [&text, &textKey](v8::Isolate *isolate) {
            return std::vector<v8::Local<v8::Value>> {v8pp::to_v8(isolate, text), v8pp::to_v8(isolate, textKey)};
        });
    }
} // namespace KCDC::Scripting
