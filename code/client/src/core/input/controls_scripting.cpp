#include "core/scripting/script_context.h"
#include "core/input/controls_scripting.h"

#include "core/application.h"
#include "core/input/native_keybinds.h"

#include "shared/scripting/catalog.h"

#include <integrations/client/scripting/module.h>
#include <scripting/resource/resource_manager.h>

#include <v8pp/convert.hpp>
#include <v8pp/module.hpp>

#include <algorithm>
#include <input/resource_holds.h>
#include <map>
#include <string>

namespace KCDC::Scripting {
    namespace {
        class ControlsOwnership final {
          public:
            static ControlsOwnership &Get() {
                static ControlsOwnership ownership;
                return ownership;
            }

            void SetHold(v8::Isolate *isolate, bool take, bool cursor) {
                v8::HandleScope handleScope(isolate);
                EnsureCleanupInstalled();
                Core::Application *application = Core::gApplication.get();
                const std::string resource     = KCDC::ResolveCallingResource(isolate);
                if (application == nullptr || resource.empty()) {
                    return;
                }
                Framework::Input::ResourceHolds &holds = cursor ? _cursorHolds : _controlHolds;
                if (take) {
                    holds.Acquire(resource);
                }
                else if (!holds.Release(resource)) {
                    return;
                }
                if (cursor) {
                    application->HoldCursor(take);
                }
                else {
                    application->LockControls(take, /*withCursor=*/false);
                }
            }

            void SetActionHold(v8::Isolate *isolate, const std::string &action, bool take) {
                EnsureCleanupInstalled();
                const std::string resource = KCDC::ResolveCallingResource(isolate);
                if (resource.empty()) {
                    return;
                }
                Framework::Input::ResourceHolds &holds = _actionHolds[action];
                if (take) {
                    holds.Acquire(resource);
                }
                else if (!holds.Release(resource)) {
                    return;
                }
                Core::NativeKeybinds::Get().SetActionBlocked(action, holds.IsHeld());
            }

            void CleanupResource(const std::string &resource) {
                for (auto &[action, holds] : _actionHolds) {
                    if (holds.TakeAll(resource) > 0) {
                        Core::NativeKeybinds::Get().SetActionBlocked(action, holds.IsHeld());
                    }
                }
                const int controls = _controlHolds.TakeAll(resource);
                const int cursors  = _cursorHolds.TakeAll(resource);
                if (Core::Application *application = Core::gApplication.get()) {
                    for (int remaining = controls; remaining > 0; --remaining) {
                        application->LockControls(false, /*withCursor=*/false);
                    }
                    for (int remaining = cursors; remaining > 0; --remaining) {
                        application->HoldCursor(false);
                    }
                }
            }

          private:
            void EnsureCleanupInstalled() {
                Framework::Scripting::ResourceManager *manager = KCDC::ScriptResources();
                if (manager == nullptr || _cleanupInstalled) {
                    return;
                }
                manager->AddOnResourceStateChanged([](const std::string &resource, Framework::Scripting::ResourceState, Framework::Scripting::ResourceState state) {
                    if (state == Framework::Scripting::ResourceState::Stopping || state == Framework::Scripting::ResourceState::Stopped || state == Framework::Scripting::ResourceState::Error) {
                        ControlsOwnership::Get().CleanupResource(resource);
                    }
                });
                _cleanupInstalled = true;
            }
            Framework::Input::ResourceHolds _controlHolds;
            Framework::Input::ResourceHolds _cursorHolds;
            std::map<std::string, Framework::Input::ResourceHolds> _actionHolds;
            bool _cleanupInstalled = false;
        };

        void JS_Disable(const v8::FunctionCallbackInfo<v8::Value> &info) {
            ControlsOwnership::Get().SetHold(info.GetIsolate(), true, false);
        }

        void JS_Enable(const v8::FunctionCallbackInfo<v8::Value> &info) {
            ControlsOwnership::Get().SetHold(info.GetIsolate(), false, false);
        }

        void JS_IsEnabled(const v8::FunctionCallbackInfo<v8::Value> &info) {
            Core::Application *application = Core::gApplication.get();
            info.GetReturnValue().Set(application != nullptr && !application->AreControlsLocked());
        }

        void JS_ShowCursor(const v8::FunctionCallbackInfo<v8::Value> &info) {
            ControlsOwnership::Get().SetHold(info.GetIsolate(), true, true);
        }

        void JS_HideCursor(const v8::FunctionCallbackInfo<v8::Value> &info) {
            ControlsOwnership::Get().SetHold(info.GetIsolate(), false, true);
        }

        /** An action map action's name: letters, digits and underscores, as the game's and the mod's are. */
        bool ReadActionName(const v8::FunctionCallbackInfo<v8::Value> &info, const char *function, std::string &out) {
            v8::Isolate *isolate = info.GetIsolate();
            if (info.Length() >= 1 && info[0]->IsString()) {
                out              = v8pp::from_v8<std::string>(isolate, info[0]);
                const bool valid = !out.empty() && out.size() <= 64 && std::all_of(out.begin(), out.end(), [](char c) {
                    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
                });
                if (valid) {
                    return true;
                }
            }
            isolate->ThrowError(v8pp::to_v8(isolate, std::string("Controls.") + function + ": expected (action), an action name of 1-64 letters, digits or underscores"));
            return false;
        }

        void JS_DisableAction(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::HandleScope handleScope(info.GetIsolate());
            std::string action;
            if (ReadActionName(info, "disableAction", action)) {
                ControlsOwnership::Get().SetActionHold(info.GetIsolate(), action, true);
            }
        }

        void JS_EnableAction(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::HandleScope handleScope(info.GetIsolate());
            std::string action;
            if (ReadActionName(info, "enableAction", action)) {
                ControlsOwnership::Get().SetActionHold(info.GetIsolate(), action, false);
            }
        }

        void JS_IsCursorVisible(const v8::FunctionCallbackInfo<v8::Value> &info) {
            Core::Application *application = Core::gApplication.get();
            info.GetReturnValue().Set(application != nullptr && application->IsCursorHeld());
        }
    } // namespace

    void RegisterControls(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        if (isolate == nullptr || global.IsEmpty()) {
            return;
        }

        v8pp::module controls(isolate, ClientCatalog(), "Controls",
            "Whether this client's gameplay input reaches the game, and whether the mod draws a pointer.\n\n"
            "A focused web view takes both already, so a panel opened with `Web.focusView` needs nothing here. This is for what focus cannot say: a pointer without freezing the player, a freeze without a pointer, or no pointer at all for a page that draws its own.\n\n"
            "Every hold is counted and belongs to the resource that took it, so two resources cannot release each other's, and one that stops gives back exactly what it took.");

        controls.function("disable", &JS_Disable, v8pp::metadata::docs("void", {}, "Takes this client's gameplay input away from the game without drawing a pointer. The camera stops turning and the character stops moving."));

        controls.function("enable", &JS_Enable, v8pp::metadata::docs("void", {}, "Gives back one hold taken by `disable`. Input returns once nothing else holds it -- a focused panel, the chat line, an overlay."));

        controls.function("isEnabled", &JS_IsEnabled, v8pp::metadata::docs("boolean", {}, "Whether gameplay input is reaching the game right now.", "False while anything holds it, this resource or not."));

        controls.function("showCursor", &JS_ShowCursor, v8pp::metadata::docs("void", {}, "Draws the mod's pointer without taking input. Its shape follows the topmost focused page, so a page's own `cursor` styling is what the player sees."));

        controls.function("hideCursor", &JS_HideCursor, v8pp::metadata::docs("void", {}, "Gives back one hold taken by `showCursor`."));

        controls.function("isCursorVisible", &JS_IsCursorVisible, v8pp::metadata::docs("boolean", {}, "Whether the mod's pointer is drawn right now.", "True while anything holds it, this resource or not."));

        controls.function("disableAction", &JS_DisableAction,
            v8pp::metadata::docs("void",
                {v8pp::metadata::param("action", "string", false, "An action of the game's action maps: the game's own, or one of the mod's keys, such as `kcdc_npc_interact` (Talk to NPC).")},
                "Keeps one game action from doing anything, whatever key the player has it on, while the key itself still reaches `Key.bind`. For a resource whose own key shares a game key, or that takes an action's job over."));

        controls.function("enableAction", &JS_EnableAction,
            v8pp::metadata::docs("void", {v8pp::metadata::param("action", "string", false, "The action given to `disableAction`.")}, "Gives back one hold taken by `disableAction`. The action works again once nothing else holds it."));

        controls.publish(global);
    }
} // namespace KCDC::Scripting
