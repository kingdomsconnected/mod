#include "ui_action_hints_scripting.h"

#include "core/application.h"
#include "core/scripting/script_context.h"
#include "features/ui/ui_action_hints.h"

#include "shared/scripting/catalog.h"

#include <logging/logger.h>
#include <scripting/engine_helpers.h>
#include <scripting/resource/resource_manager.h>

#include <v8pp/convert.hpp>
#include <v8pp/module.hpp>

#include <cmath>
#include <map>
#include <string>

namespace KCDC::Scripting {
    namespace {
        /** Longer than any line the HUD can draw; a cap, not a layout rule. */
        constexpr std::size_t kMaxTextBytes = 256;

        /** Outside it a hold either cannot be told from a tap or is not one a player would sit through. */
        constexpr double kMinHoldSeconds = 0.2;
        constexpr double kMaxHoldSeconds = 10.0;

        struct Handler {
            v8::Global<v8::Function> callback;
            std::string resource;
        };

        // Keyed by hint id: a hint is a number a script may copy freely.
        std::map<std::uint32_t, Handler> gHandlers;
        bool gSinkInstalled    = false;
        bool gCleanupInstalled = false;

        void ThrowError(v8::Isolate *isolate, const std::string &message) {
            isolate->ThrowException(v8::Exception::Error(v8pp::to_v8(isolate, message)));
        }

        bool ReadHintId(v8::Isolate *isolate, v8::Local<v8::Value> value, std::uint32_t &out) {
            if (!value->IsNumber()) {
                return false;
            }
            const double number = v8pp::from_v8<double>(isolate, value);
            if (!std::isfinite(number) || number < 1.0 || number > 4294967295.0 || std::floor(number) != number) {
                return false;
            }
            out = static_cast<std::uint32_t>(number);
            return true;
        }

        bool ReadText(v8::Isolate *isolate, v8::Local<v8::Value> value, std::string &out) {
            if (!value->IsString()) {
                return false;
            }
            out = v8pp::from_v8<std::string>(isolate, value);
            return out.size() <= kMaxTextBytes;
        }

        void Dispatch(std::uint32_t hint) {
            const auto entry = gHandlers.find(hint);
            if (entry == gHandlers.end()) {
                return;
            }
            KCDC::WithScriptContext([&](v8::Isolate *isolate, v8::Local<v8::Context> context, Framework::Scripting::ResourceManager *) {
                // Copied out first: the handler may remove its own hint.
                const std::string resource       = entry->second.resource;
                v8::Local<v8::Function> callback = entry->second.callback.Get(isolate);
                if (callback.IsEmpty()) {
                    return;
                }
                v8::Local<v8::Value> arguments[] = {v8pp::to_v8(isolate, static_cast<double>(hint))};
                v8::TryCatch tryCatch(isolate);
                const v8::MaybeLocal<v8::Value> result = callback->Call(context, context->Global(), 1, arguments);
                if (result.IsEmpty() && tryCatch.HasCaught()) {
                    const std::string error = Framework::Scripting::FormatV8Exception(isolate, tryCatch, "unknown error");
                    Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("[{}] Action hint handler error: {}", resource, error);
                }
            });
        }

        void CleanupResource(const std::string &resource) {
            for (auto entry = gHandlers.begin(); entry != gHandlers.end();) {
                entry = entry->second.resource == resource ? gHandlers.erase(entry) : std::next(entry);
            }
            KCDC::Features::UI::ActionHints::Get().Release(resource);
        }

        // JS: ActionHint.create(options, handler) -> number
        void JS_Create(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = isolate->GetCurrentContext();

            if (info.Length() < 2 || !info[0]->IsObject() || !info[1]->IsFunction()) {
                ThrowError(isolate, "ActionHint.create expects an options object and a handler");
                return;
            }
            v8::Local<v8::Object> options = info[0].As<v8::Object>();

            KCDC::Features::UI::ActionHints::Options parsed;
            v8::Local<v8::Value> key;
            v8::Local<v8::Value> text;
            if (!options->Get(context, v8pp::to_v8(isolate, "key")).ToLocal(&key) || !key->IsString() || !options->Get(context, v8pp::to_v8(isolate, "text")).ToLocal(&text) || !ReadText(isolate, text, parsed.text)) {
                ThrowError(isolate, "ActionHint.create needs a `key` string and a `text` string of at most 256 bytes");
                return;
            }
            parsed.key = v8pp::from_v8<std::string>(isolate, key);

            v8::Local<v8::Value> mode;
            if (options->Get(context, v8pp::to_v8(isolate, "mode")).ToLocal(&mode) && !mode->IsUndefined()) {
                const std::string name = mode->IsString() ? v8pp::from_v8<std::string>(isolate, mode) : std::string {};
                if (name != "press" && name != "hold") {
                    ThrowError(isolate, "ActionHint.create: `mode` is \"press\" or \"hold\"");
                    return;
                }
                parsed.mode = name == "hold" ? KCDC::Features::UI::ActionHints::Mode::Hold : KCDC::Features::UI::ActionHints::Mode::Press;
            }

            v8::Local<v8::Value> holdDuration;
            if (options->Get(context, v8pp::to_v8(isolate, "holdDuration")).ToLocal(&holdDuration) && !holdDuration->IsUndefined()) {
                const double seconds = holdDuration->IsNumber() ? v8pp::from_v8<double>(isolate, holdDuration) : -1.0;
                if (!std::isfinite(seconds) || seconds < kMinHoldSeconds || seconds > kMaxHoldSeconds) {
                    ThrowError(isolate, "ActionHint.create: `holdDuration` is between 0.2 and 10 seconds");
                    return;
                }
                parsed.holdDuration = static_cast<float>(seconds);
            }

            v8::Local<v8::Value> enabled;
            if (options->Get(context, v8pp::to_v8(isolate, "enabled")).ToLocal(&enabled) && !enabled->IsUndefined()) {
                parsed.enabled = enabled->BooleanValue(isolate);
            }

            v8::Local<v8::Function> callback = info[1].As<v8::Function>();
            const std::string resource       = KCDC::ResolveCallingResource(isolate, callback);
            const std::uint32_t hint         = KCDC::Features::UI::ActionHints::Get().Create(resource, parsed);
            if (hint == 0) {
                std::string keys;
                for (const std::string &name : KCDC::Features::UI::ActionHints::Keys()) {
                    keys += keys.empty() ? name : ", " + name;
                }
                ThrowError(isolate, "ActionHint.create: '" + parsed.key + "' is not a key a hint can use; one of " + keys);
                return;
            }

            Handler &handler = gHandlers[hint];
            handler.callback.Reset(isolate, callback);
            handler.resource = resource;
            info.GetReturnValue().Set(static_cast<double>(hint));
        }

        // JS: ActionHint.setText(hint, text) -> boolean
        void JS_SetText(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);
            std::uint32_t hint = 0;
            std::string text;
            if (info.Length() < 2 || !ReadHintId(isolate, info[0], hint) || !ReadText(isolate, info[1], text)) {
                info.GetReturnValue().Set(false);
                return;
            }
            info.GetReturnValue().Set(KCDC::Features::UI::ActionHints::Get().SetText(hint, text));
        }

        // JS: ActionHint.setEnabled(hint, enabled) -> boolean
        void JS_SetEnabled(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);
            std::uint32_t hint = 0;
            if (info.Length() < 2 || !ReadHintId(isolate, info[0], hint)) {
                info.GetReturnValue().Set(false);
                return;
            }
            info.GetReturnValue().Set(KCDC::Features::UI::ActionHints::Get().SetEnabled(hint, info[1]->BooleanValue(isolate)));
        }

        // JS: ActionHint.remove(hint) -> boolean
        void JS_Remove(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);
            std::uint32_t hint = 0;
            if (info.Length() < 1 || !ReadHintId(isolate, info[0], hint)) {
                info.GetReturnValue().Set(false);
                return;
            }
            gHandlers.erase(hint);
            info.GetReturnValue().Set(KCDC::Features::UI::ActionHints::Get().Remove(hint));
        }

        // JS: ActionHint.keys() -> string[]
        void JS_Keys(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);
            info.GetReturnValue().Set(v8pp::to_v8(isolate, KCDC::Features::UI::ActionHints::Keys()));
        }

        // JS: ActionHint.shownKeys() -> string[]
        void JS_ShownKeys(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);
            info.GetReturnValue().Set(v8pp::to_v8(isolate, KCDC::Features::UI::ActionHints::ShownKeys()));
        }

        // JS: ActionHint.shown() -> { key, map, text, mode, enabled, progress, target }[]
        void JS_Shown(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context                                  = isolate->GetCurrentContext();
            const std::vector<KCDC::Features::UI::ActionHints::Drawn> drawn = KCDC::Features::UI::ActionHints::DrawnHints();
            v8::Local<v8::Array> out                                        = v8::Array::New(isolate, static_cast<int>(drawn.size()));
            for (std::size_t i = 0; i < drawn.size(); ++i) {
                const auto &hint            = drawn[i];
                v8::Local<v8::Object> entry = v8::Object::New(isolate);
                entry->Set(context, v8pp::to_v8(isolate, "key"), v8pp::to_v8(isolate, hint.key)).Check();
                entry->Set(context, v8pp::to_v8(isolate, "map"), v8pp::to_v8(isolate, hint.map)).Check();
                entry->Set(context, v8pp::to_v8(isolate, "text"), v8pp::to_v8(isolate, hint.text)).Check();
                entry->Set(context, v8pp::to_v8(isolate, "mode"), v8pp::to_v8(isolate, hint.mode == KCDC::Features::UI::ActionHints::Mode::Hold ? "hold" : "press")).Check();
                entry->Set(context, v8pp::to_v8(isolate, "enabled"), v8pp::to_v8(isolate, hint.enabled)).Check();
                entry->Set(context, v8pp::to_v8(isolate, "progress"), v8pp::to_v8(isolate, hint.progress)).Check();
                entry->Set(context, v8pp::to_v8(isolate, "target"), v8pp::to_v8(isolate, hint.target)).Check();
                out->Set(context, static_cast<std::uint32_t>(i), entry).Check();
            }
            info.GetReturnValue().Set(out);
        }

        // JS: ActionHint.isKeyInUse(key) -> boolean
        void JS_IsKeyInUse(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);
            if (info.Length() < 1 || !info[0]->IsString()) {
                ThrowError(isolate, "ActionHint.isKeyInUse: key must be a string");
                return;
            }
            info.GetReturnValue().Set(KCDC::Features::UI::ActionHints::IsKeyInUse(v8pp::from_v8<std::string>(isolate, info[0]).c_str()));
        }
    } // namespace

    void Events::ActionHintsChanged() {
        EmitReservedEvent("actionHintsChanged", [](v8::Isolate *) {
            return std::vector<v8::Local<v8::Value>> {};
        });
    }

    void RegisterActionHints(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        if (isolate == nullptr || global.IsEmpty()) {
            return;
        }

        // A new session starts with no hints: the service's sweep took the
        // last session's down, and their handlers belonged to a context that
        // is gone.
        gHandlers.clear();

        if (!gSinkInstalled) {
            KCDC::Features::UI::ActionHints::Get().SetPressSink([](std::uint32_t hint) {
                Dispatch(hint);
            });
            gSinkInstalled = true;
        }
        if (Framework::Scripting::ResourceManager *manager = KCDC::ScriptResources(); manager != nullptr && !gCleanupInstalled) {
            manager->AddOnResourceStopped([](const std::string &resource) {
                CleanupResource(resource);
            });
            gCleanupInstalled = true;
        }

        v8pp::module hints(isolate, ClientCatalog(), "ActionHint",
            "\"Press [key] to ...\" hints, drawn by the game's own HUD beside its own, with a handler run when the player presses the key.\n\n"
            "A hint names one of the game's keys by its control -- `primary_action`, `jump` and the rest of `keys()` -- and never a physical key: the glyph is whatever the player has bound to that control, keyboard or pad, and follows a rebind. A press hint fires on the press; a hold hint draws the game's ring and fires once it fills.\n\n"
            "Hints are drawn while the player is in control of their character, on foot or in the saddle, and the game takes them down in dialogue, minigames and menus. Two hints on one key stack: the newest is drawn and gets the press, and removing it brings back the one under it. A hint normally leaves the game's own action on that key enabled. `chat_focus_follow_init` replaces the native focus/follow toggle while its hint is shown, so a scripted Follow action receives one press.\n\n"
            "Client-only and local: a hint is shown to the player at this machine. Hints go when the resource that made them stops.");

        hints.function("create", &JS_Create,
            v8pp::metadata::docs("number",
                {v8pp::metadata::param("options", "{ key: string; text: string; mode?: \"press\" | \"hold\"; holdDuration?: number; enabled?: boolean }", false,
                     "`key` is one of `keys()`. `text` is what the HUD writes beside the key, as written, at most 256 bytes. `mode` is \"press\" by default. `holdDuration` is how long a hold hint's ring takes to fill, 0.2 to 10 seconds, 1 by default. `enabled` false draws the hint greyed out and keeps the handler from running."),
                    v8pp::metadata::param("handler", "ActionHintHandler", false, "Run with the hint's id each time it fires.")},
                "Puts a hint up. It is drawn from the next frame the player is in control.", "The hint's id. Throws when an option is out of range or the key is not one of `keys()`."));

        hints.function("setText", &JS_SetText,
            v8pp::metadata::docs("boolean", {v8pp::metadata::param("hint", "number", false, "An id from `create`."), v8pp::metadata::param("text", "string", false, "The new text, as written, at most 256 bytes.")}, "Rewrites a hint's text in place.",
                "False when the hint is gone or the text is too long."));

        hints.function("setEnabled", &JS_SetEnabled,
            v8pp::metadata::docs("boolean", {v8pp::metadata::param("hint", "number", false, "An id from `create`."), v8pp::metadata::param("enabled", "boolean", false, "False greys the hint out and its handler stops running.")}, "Greys a hint out, or brings it back.",
                "False when the hint is gone."));

        hints.function("remove", &JS_Remove, v8pp::metadata::docs("boolean", {v8pp::metadata::param("hint", "number", false, "An id from `create`.")}, "Takes a hint down for good.", "False when it was already gone."));

        hints.function("keys", &JS_Keys, v8pp::metadata::docs("string[]", {}, "The controls a hint can be put on.", "Control names, in a fixed order."));

        hints.function("isKeyInUse", &JS_IsKeyInUse,
            v8pp::metadata::docs("boolean", {v8pp::metadata::param("key", "string", false, "A control, such as `use` or `jump`; any of the game's, not only `keys()`.")},
                "Whether the HUD is drawing a hint on that control right now, the game's own hints and interaction prompts included, whoever put it up. Lets a script keep its own UI off a key a hint is waiting on.",
                "True while a drawn hint is on the control, as of the game's last hint refresh, at most a frame old."));

        hints.function("shownKeys", &JS_ShownKeys,
            v8pp::metadata::docs("string[]", {}, "Every control a drawn hint is on right now, interaction prompts included, whoever put it up.", "Control names, each once; empty while no hints are drawn."));

        hints.function("shown", &JS_Shown,
            v8pp::metadata::docs("{ key: string; map: string; text: string; mode: \"press\" | \"hold\"; enabled: boolean; progress: number; target: string }[]", {},
                "Every hint the HUD draws right now -- the game's own, interaction prompts such as talking to someone or opening a door, and every script's -- in the HUD's order, with what it says. What a script needs to draw them in its own UI, with the game's hints hidden through `Hud.setElementVisible(\"Hints\", false)`; the keys keep doing what they do.",
                "One entry per drawn hint, as of the game's last hint refresh, at most a frame old. `key` is what `shownKeys` names: the control a script's hint is on, or the action a game hint triggers (`talk`, `use`), whose key `Libs/Config/keybindSuperactions.xml` binds. `map` is the action map the hint belongs to: the world's prompts are `player`'s, the game's menu screens have their own (`apse`, `apse_inventory`, `help_dialog`). `text` is what the game writes beside the key, in the player's language. `enabled` is false for a hint drawn greyed out. `progress` is how far a hold hint's ring has filled, 0 to 1, and 0 for a hint nobody is holding. `target` is who the prompt is about as the game names them -- a person or an animal, an NPC of the server's under the name it was given -- and empty for anything else."));

        auto &events = ClientCatalog().data_type("EventMap");
        events.add_property("actionHintsChanged", "[]", "Dispatched when what the HUD's hints say changes: one appears or goes, changes its text, is greyed out, or a hold ring moves. `ActionHint.shown()` reads the new set. Handler promises are not awaited.");

        hints.publish(global);
    }
} // namespace KCDC::Scripting
