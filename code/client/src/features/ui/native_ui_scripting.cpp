#include "native_ui_scripting.h"

#include "core/application.h"
#include "core/scripting/script_context.h"
#include "features/ui/native_element.h"
#include "features/ui/native_ui_service.h"
#include "features/ui/ui_game_menus.h"

#include "shared/scripting/catalog.h"

#include "core/ui/native_ui/native_ui_module.h"

#include <logging/logger.h>
#include <scripting/engine_helpers.h>
#include <scripting/resource/resource_manager.h>

#include <v8pp/convert.hpp>
#include <v8pp/module.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <functional>
#include <iterator>
#include <map>
#include <string_view>
#include <tuple>
#include <vector>

namespace KCDC::Scripting {
    std::unique_ptr<v8pp::class_<NativeScreen>> NativeScreen::_class;
    std::unique_ptr<v8pp::class_<NativeClip>> NativeClip::_class;
    std::unique_ptr<v8pp::class_<NativeElement>> NativeElement::_class;

    namespace {
        constexpr std::size_t kMaxArguments    = 16;
        constexpr std::size_t kMaxStringBytes  = 64 * 1024;
        constexpr std::size_t kMaxClipHandlers = 512;

        /** The ActionScript mouse handlers a clip may be given from script. */
        constexpr std::array<std::string_view, 7> kClipEvents {"press", "release", "releaseOutside", "rollOver", "rollOut", "dragOver", "dragOut"};

        /** Screen-level events, all of them raised by the engine or the movie. */
        constexpr std::array<std::string_view, 4> kScreenEvents {"ready", "unload", "input", "event"};

        KCDC::Features::UI::NativeUIService *Screens() {
            return Core::gApplication != nullptr ? &Core::gApplication->GetNativeUIService() : nullptr;
        }

        void ThrowError(v8::Isolate *isolate, const std::string &message) {
            isolate->ThrowException(v8::Exception::Error(v8pp::to_v8(isolate, message)));
        }

        bool MenuArgument(const v8::FunctionCallbackInfo<v8::Value> &info, KCDC::Features::UI::GameMenus::Menu &menu) {
            return info.Length() > 0 && info[0]->IsString() && KCDC::Features::UI::GameMenus::Parse(v8pp::from_v8<std::string>(info.GetIsolate(), info[0]), menu);
        }

        void JS_SetMenuEnabled(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::HandleScope handleScope(info.GetIsolate());
            KCDC::Features::UI::GameMenus::Menu menu;
            if (!MenuArgument(info, menu) || info.Length() < 2 || !info[1]->IsBoolean()) {
                ThrowError(info.GetIsolate(), "NativeUI.setMenuEnabled: expected (menu, enabled) with a known menu name and a boolean");
                return;
            }
            const std::string resource = KCDC::ResolveCallingResource(info.GetIsolate());
            if (resource.empty()) {
                ThrowError(info.GetIsolate(), "NativeUI.setMenuEnabled: must be called from a resource");
                return;
            }
            KCDC::Features::UI::GameMenus::Get().SetEnabled(resource, menu, info[1]->IsTrue());
        }

        void JS_IsMenuEnabled(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::HandleScope handleScope(info.GetIsolate());
            KCDC::Features::UI::GameMenus::Menu menu;
            if (!MenuArgument(info, menu)) {
                ThrowError(info.GetIsolate(), "NativeUI.isMenuEnabled: expected a known menu name");
                return;
            }
            info.GetReturnValue().Set(KCDC::Features::UI::GameMenus::Get().IsEnabled(menu));
        }

        struct Handler {
            v8::Global<v8::Function> callback;
            std::string resource;
        };

        // Handlers live here, not on the handles: a handle is a number a script may copy freely.
        std::map<std::uint32_t, std::map<std::string, std::vector<Handler>>> gScreenHandlers;
        std::map<std::uint32_t, std::map<std::string, std::map<std::string, Handler>>> gClipHandlers;
        std::map<std::string, std::map<std::string, std::vector<Handler>>> gElementHandlers;
        bool gSinkInstalled    = false;
        bool gCleanupInstalled = false;

        v8::Local<v8::Value> ToV8(v8::Isolate *isolate, const KCDC::Core::NativeUIValue &value) {
            if (std::holds_alternative<bool>(value)) {
                return v8pp::to_v8(isolate, std::get<bool>(value));
            }
            if (std::holds_alternative<double>(value)) {
                return v8pp::to_v8(isolate, std::get<double>(value));
            }
            if (std::holds_alternative<std::string>(value)) {
                return v8pp::to_v8(isolate, std::get<std::string>(value));
            }
            return v8::Null(isolate);
        }

        v8::Local<v8::Value> ToV8(v8::Isolate *isolate, const SDK::SFlashVarValue &value) {
            switch (value.type) {
            case SDK::EFlashVariableType::Bool: return v8pp::to_v8(isolate, value.b);
            case SDK::EFlashVariableType::Int: return v8pp::to_v8(isolate, static_cast<double>(value.i));
            case SDK::EFlashVariableType::UInt: return v8pp::to_v8(isolate, static_cast<double>(value.u));
            case SDK::EFlashVariableType::Double: return v8pp::to_v8(isolate, value.d);
            case SDK::EFlashVariableType::Float: return v8pp::to_v8(isolate, static_cast<double>(value.f));
            case SDK::EFlashVariableType::ConstStrPtr: return value.str != nullptr ? v8pp::to_v8(isolate, std::string(value.str)) : v8::Local<v8::Value>(v8::Null(isolate));
            case SDK::EFlashVariableType::Null: return v8::Null(isolate);
            default: return v8::Undefined(isolate);
            }
        }

        /** A string is parked in `storage`, because the native record only borrows the pointer. */
        bool FromV8(v8::Isolate *isolate, v8::Local<v8::Value> value, std::vector<std::unique_ptr<std::string>> &storage, SDK::SFlashVarValue &out) {
            if (value->IsBoolean()) {
                out = SDK::SFlashVarValue::Bool(value->BooleanValue(isolate));
                return true;
            }
            if (value->IsNumber()) {
                out = SDK::SFlashVarValue::Double(v8pp::from_v8<double>(isolate, value));
                return true;
            }
            if (value->IsNull() || value->IsUndefined()) {
                out = SDK::SFlashVarValue {SDK::EFlashVariableType::Null, 0, {}};
                return true;
            }
            if (value->IsString()) {
                std::string text = v8pp::from_v8<std::string>(isolate, value);
                if (text.size() > kMaxStringBytes) {
                    return false;
                }
                storage.emplace_back(std::make_unique<std::string>(std::move(text)));
                out = SDK::SFlashVarValue::String(storage.back()->c_str());
                return true;
            }
            return false;
        }

        bool ReadScalar(v8::Isolate *isolate, v8::Local<v8::Value> from, std::vector<SDK::SFlashVarValue> &values, std::vector<std::unique_ptr<std::string>> &storage) {
            SDK::SFlashVarValue value {};
            if (!FromV8(isolate, from, storage, value)) {
                ThrowError(isolate, "only numbers, strings, booleans and null cross into ActionScript");
                return false;
            }
            values.push_back(value);
            return true;
        }

        // The arguments from `first` onwards, either loose or gathered into one
        // array. The array is the declared form, because the scripting contract
        // cannot spell a rest parameter; loose scalars still work, and an array
        // was rejected outright before, so nothing that ran changes meaning.
        bool ReadArguments(const v8::FunctionCallbackInfo<v8::Value> &info, int first, std::vector<SDK::SFlashVarValue> &values, std::vector<std::unique_ptr<std::string>> &storage) {
            v8::Isolate *isolate = info.GetIsolate();
            const int count      = info.Length();

            if (count == first + 1 && info[first]->IsArray()) {
                v8::Local<v8::Context> context = isolate->GetCurrentContext();
                v8::Local<v8::Array> gathered  = info[first].As<v8::Array>();
                if (gathered->Length() > kMaxArguments) {
                    ThrowError(isolate, "at most " + std::to_string(kMaxArguments) + " arguments may cross into ActionScript");
                    return false;
                }
                for (std::uint32_t index = 0; index < gathered->Length(); ++index) {
                    v8::Local<v8::Value> element;
                    if (!gathered->Get(context, index).ToLocal(&element) || !ReadScalar(isolate, element, values, storage)) {
                        return false;
                    }
                }
                return true;
            }

            if (count > first && static_cast<std::size_t>(count - first) > kMaxArguments) {
                ThrowError(isolate, "at most " + std::to_string(kMaxArguments) + " arguments may cross into ActionScript");
                return false;
            }
            for (int index = first; index < count; ++index) {
                if (!ReadScalar(isolate, info[index], values, storage)) {
                    return false;
                }
            }
            return true;
        }

        double ReadNumber(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> options, const char *key, double fallback, bool &present) {
            present = false;
            v8::Local<v8::Value> value;
            if (!options->Get(context, v8pp::to_v8(isolate, key)).ToLocal(&value) || !value->IsNumber()) {
                return fallback;
            }
            const double result = v8pp::from_v8<double>(isolate, value);
            if (!std::isfinite(result)) {
                return fallback;
            }
            present = true;
            return result;
        }

        std::string ReadString(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> options, const char *key) {
            v8::Local<v8::Value> value;
            if (!options->Get(context, v8pp::to_v8(isolate, key)).ToLocal(&value) || !value->IsString()) {
                return {};
            }
            return v8pp::from_v8<std::string>(isolate, value);
        }

        /** Defaults to a font the game's map knows; a field without one draws empty boxes. */
        KCDC::Core::NativeUI::TextStyle ReadTextStyle(v8::Isolate *isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> options) {
            KCDC::Core::NativeUI::TextStyle style;
            const std::string font = ReadString(isolate, context, options, "font");
            if (!font.empty()) {
                style.font = font;
            }
            bool present      = false;
            const double size = ReadNumber(isolate, context, options, "size", style.size, present);
            if (present && size > 0.0) {
                style.size = size;
            }
            const double color = ReadNumber(isolate, context, options, "color", static_cast<double>(style.color), present);
            if (present && color >= 0.0 && color <= 0xFFFFFF) {
                style.color = static_cast<int>(color);
            }
            v8::Local<v8::Value> value;
            if (options->Get(context, v8pp::to_v8(isolate, "bold")).ToLocal(&value) && value->IsBoolean()) {
                style.bold = value->BooleanValue(isolate);
            }
            if (options->Get(context, v8pp::to_v8(isolate, "italic")).ToLocal(&value) && value->IsBoolean()) {
                style.italic = value->BooleanValue(isolate);
            }
            style.align = ReadString(isolate, context, options, "align");
            return style;
        }

        std::shared_ptr<KCDC::Core::NativeUI> PageOf(std::uint32_t screen) {
            KCDC::Features::UI::NativeUIService *screens = Screens();
            return screens != nullptr ? screens->GetPage(screen) : std::shared_ptr<KCDC::Core::NativeUI> {};
        }

        bool IsKnownEvent(const std::array<std::string_view, 7> &names, const std::string &name) {
            return std::find(names.begin(), names.end(), name) != names.end();
        }

        bool IsKnownEvent(const std::array<std::string_view, 4> &names, const std::string &name) {
            return std::find(names.begin(), names.end(), name) != names.end();
        }

        void CallHandler(v8::Isolate *isolate, const Handler &handler, const std::vector<v8::Local<v8::Value>> &arguments, const char *what) {
            v8::Local<v8::Context> context   = isolate->GetCurrentContext();
            v8::Local<v8::Function> callback = handler.callback.Get(isolate);
            if (callback.IsEmpty()) {
                return;
            }
            v8::TryCatch tryCatch(isolate);
            const v8::MaybeLocal<v8::Value> result = callback->Call(context, context->Global(), static_cast<int>(arguments.size()), arguments.empty() ? nullptr : const_cast<v8::Local<v8::Value> *>(arguments.data()));
            if (result.IsEmpty() && tryCatch.HasCaught()) {
                const std::string error = Framework::Scripting::FormatV8Exception(isolate, tryCatch, "unknown error");
                Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("[{}] Native UI {} handler error: {}", handler.resource, what, error);
            }
        }

        void DispatchScreenEvent(std::uint32_t screen, const std::string &event, const std::vector<KCDC::Core::NativeUIValue> &values, const std::string &name, int activationMode) {
            const auto screenEntry = gScreenHandlers.find(screen);
            if (screenEntry == gScreenHandlers.end()) {
                return;
            }
            const auto eventEntry = screenEntry->second.find(event);
            if (eventEntry == screenEntry->second.end() || eventEntry->second.empty()) {
                return;
            }
            KCDC::WithScriptContext([&](v8::Isolate *isolate, v8::Local<v8::Context>, Framework::Scripting::ResourceManager *) {
                std::vector<v8::Local<v8::Value>> arguments;
                if (event == "input") {
                    arguments.push_back(v8pp::to_v8(isolate, name));
                    arguments.push_back(v8pp::to_v8(isolate, static_cast<double>(activationMode)));
                }
                else if (event == "event") {
                    arguments.push_back(v8pp::to_v8(isolate, name));
                    v8::Local<v8::Array> list = v8::Array::New(isolate, static_cast<int>(values.size()));
                    for (std::size_t index = 0; index < values.size(); ++index) {
                        std::ignore = list->Set(isolate->GetCurrentContext(), static_cast<std::uint32_t>(index), ToV8(isolate, values[index]));
                    }
                    arguments.push_back(list);
                }
                // Copied first: a handler may add or remove handlers.
                std::vector<Handler *> handlers;
                for (Handler &handler : eventEntry->second) {
                    handlers.push_back(&handler);
                }
                for (Handler *handler : handlers) {
                    CallHandler(isolate, *handler, arguments, "screen");
                }
            });
        }

        void DispatchClipEvent(std::uint32_t screen, const std::string &target, const std::string &event, const std::vector<KCDC::Core::NativeUIValue> &values) {
            const auto screenEntry = gClipHandlers.find(screen);
            if (screenEntry == gClipHandlers.end()) {
                return;
            }
            const auto clipEntry = screenEntry->second.find(target);
            if (clipEntry == screenEntry->second.end()) {
                return;
            }
            const auto eventEntry = clipEntry->second.find(event);
            if (eventEntry == clipEntry->second.end()) {
                return;
            }
            KCDC::WithScriptContext([&](v8::Isolate *isolate, v8::Local<v8::Context>, Framework::Scripting::ResourceManager *) {
                std::vector<v8::Local<v8::Value>> arguments;
                arguments.push_back(v8pp::class_<NativeClip>::create_object(isolate, static_cast<double>(screen), target));
                for (const KCDC::Core::NativeUIValue &value : values) {
                    arguments.push_back(ToV8(isolate, value));
                }
                CallHandler(isolate, eventEntry->second, arguments, "clip");
            });
        }

        void DispatchElementEvent(const std::string &key, const std::string &event, const std::vector<KCDC::Core::NativeUIValue> &values) {
            const auto elementEntry = gElementHandlers.find(key);
            if (elementEntry == gElementHandlers.end()) {
                return;
            }
            const auto eventEntry = elementEntry->second.find(event);
            if (eventEntry == elementEntry->second.end() || eventEntry->second.empty()) {
                return;
            }
            KCDC::WithScriptContext([&](v8::Isolate *isolate, v8::Local<v8::Context>, Framework::Scripting::ResourceManager *) {
                std::vector<v8::Local<v8::Value>> arguments;
                v8::Local<v8::Array> list = v8::Array::New(isolate, static_cast<int>(values.size()));
                for (std::size_t index = 0; index < values.size(); ++index) {
                    std::ignore = list->Set(isolate->GetCurrentContext(), static_cast<std::uint32_t>(index), ToV8(isolate, values[index]));
                }
                arguments.push_back(list);
                std::vector<Handler *> handlers;
                for (Handler &handler : eventEntry->second) {
                    handlers.push_back(&handler);
                }
                for (Handler *handler : handlers) {
                    CallHandler(isolate, *handler, arguments, "element");
                }
            });
        }

        void OnNativeEvent(const KCDC::Core::NativeUIEvent &event) {
            KCDC::Features::UI::NativeUIService *screens = Screens();
            if (screens == nullptr) {
                return;
            }
            // An element handle keys by name@instance and has no page.
            if (event.page.empty() && event.kind == KCDC::Core::NativeUIEventKind::Element) {
                DispatchElementEvent(event.host, event.name, event.arguments);
                return;
            }
            const std::uint32_t screen = screens->FindScreen(event.host, event.page);
            if (screen == 0) {
                return;
            }
            switch (event.kind) {
            case KCDC::Core::NativeUIEventKind::Ready: DispatchScreenEvent(screen, "ready", {}, {}, 0); break;
            case KCDC::Core::NativeUIEventKind::Unload: DispatchScreenEvent(screen, "unload", {}, {}, 0); break;
            case KCDC::Core::NativeUIEventKind::Action: DispatchScreenEvent(screen, "input", {}, event.name, event.activationMode); break;
            case KCDC::Core::NativeUIEventKind::Element: DispatchScreenEvent(screen, "event", event.arguments, event.name, 0); break;
            case KCDC::Core::NativeUIEventKind::Callback: DispatchClipEvent(screen, event.target, event.name, event.arguments); break;
            }
        }

        void CleanupResource(const std::string &resource) {
            KCDC::Features::UI::GameMenus::Get().Release(resource);
            KCDC::Features::UI::NativeUIService *screens = Screens();
            if (screens != nullptr) {
                screens->CloseResource(resource);
            }
            for (auto screenEntry = gScreenHandlers.begin(); screenEntry != gScreenHandlers.end();) {
                for (auto eventEntry = screenEntry->second.begin(); eventEntry != screenEntry->second.end();) {
                    auto &handlers = eventEntry->second;
                    handlers.erase(std::remove_if(handlers.begin(), handlers.end(),
                                       [&resource](const Handler &handler) {
                                           return handler.resource == resource;
                                       }),
                        handlers.end());
                    eventEntry = handlers.empty() ? screenEntry->second.erase(eventEntry) : std::next(eventEntry);
                }
                screenEntry = screenEntry->second.empty() ? gScreenHandlers.erase(screenEntry) : std::next(screenEntry);
            }
            for (auto screenEntry = gClipHandlers.begin(); screenEntry != gClipHandlers.end();) {
                for (auto clipEntry = screenEntry->second.begin(); clipEntry != screenEntry->second.end();) {
                    for (auto eventEntry = clipEntry->second.begin(); eventEntry != clipEntry->second.end();) {
                        eventEntry = eventEntry->second.resource == resource ? clipEntry->second.erase(eventEntry) : std::next(eventEntry);
                    }
                    clipEntry = clipEntry->second.empty() ? screenEntry->second.erase(clipEntry) : std::next(clipEntry);
                }
                screenEntry = screenEntry->second.empty() ? gClipHandlers.erase(screenEntry) : std::next(screenEntry);
            }
            for (auto elementEntry = gElementHandlers.begin(); elementEntry != gElementHandlers.end();) {
                for (auto eventEntry = elementEntry->second.begin(); eventEntry != elementEntry->second.end();) {
                    auto &handlers = eventEntry->second;
                    handlers.erase(std::remove_if(handlers.begin(), handlers.end(),
                                       [&resource](const Handler &handler) {
                                           return handler.resource == resource;
                                       }),
                        handlers.end());
                    eventEntry = handlers.empty() ? elementEntry->second.erase(eventEntry) : std::next(eventEntry);
                }
                elementEntry = elementEntry->second.empty() ? gElementHandlers.erase(elementEntry) : std::next(elementEntry);
            }
        }

        std::size_t CountClipHandlers(std::uint32_t screen) {
            const auto found = gClipHandlers.find(screen);
            if (found == gClipHandlers.end()) {
                return 0;
            }
            std::size_t count = 0;
            for (const auto &clip : found->second) {
                count += clip.second.size();
            }
            return count;
        }

        /** Installs, or replaces, one ActionScript mouse handler on a clip. */
        bool BindClipHandler(std::uint32_t screen, const std::string &key, const std::string &event, v8::Isolate *isolate, v8::Local<v8::Function> callback, const std::string &resource, std::string &error) {
            std::shared_ptr<KCDC::Core::NativeUI> page   = PageOf(screen);
            KCDC::Features::UI::NativeUIService *screens = Screens();
            if (!page || screens == nullptr) {
                error = "the screen is not composed";
                return false;
            }
            if (CountClipHandlers(screen) >= kMaxClipHandlers) {
                error = "a screen may hold at most " + std::to_string(kMaxClipHandlers) + " handlers";
                return false;
            }
            const std::string callbackKey = screens->NextKey(screen);
            if (callbackKey.empty()) {
                error = "the screen is gone";
                return false;
            }
            KCDC::Core::NativeUIHost *owner = screens->GetHost(screen);
            if (owner == nullptr) {
                error = "the screen has no host";
                return false;
            }
            const std::string hostName = owner->GetElementName();
            const std::string pageName = owner->FindPageName(page.get());
            const std::string member   = "on" + std::string(1, static_cast<char>(std::toupper(static_cast<unsigned char>(event[0])))) + event.substr(1);
            if (!page->CreateCallback(callbackKey, [hostName, pageName, key, event](const SDK::IActionScriptFunction::Params &params, SDK::IActionScriptFunction::IReturnValue *) {
                    KCDC::Core::NativeUIEvent queued;
                    queued.kind      = KCDC::Core::NativeUIEventKind::Callback;
                    queued.host      = hostName;
                    queued.page      = pageName;
                    queued.target    = key;
                    queued.name      = event;
                    queued.arguments = KCDC::Core::NativeUIHost::ConvertArguments(params);
                    KCDC::Core::NativeUIModule::Get().Queue(std::move(queued));
                })) {
                error = "the movie refused a callback";
                return false;
            }
            // `enabled` is what makes a sprite eligible as a mouse target at all.
            page->SetValue(key, "enabled", SDK::SFlashVarValue::Bool(true));
            if (!page->SetObject(key, member.c_str(), callbackKey)) {
                page->RemoveObject(callbackKey);
                error = "the clip refused " + member;
                return false;
            }
            Handler handler;
            handler.callback.Reset(isolate, callback);
            handler.resource = resource;
            gClipHandlers[screen][key].insert_or_assign(event, std::move(handler));
            return true;
        }

        void UnbindClipHandler(std::uint32_t screen, const std::string &key, const std::string &event) {
            std::shared_ptr<KCDC::Core::NativeUI> page = PageOf(screen);
            if (page) {
                const std::string member = "on" + std::string(1, static_cast<char>(std::toupper(static_cast<unsigned char>(event[0])))) + event.substr(1);
                // Deleted, not nulled: the native eligibility cache keeps a handler's name until the member goes.
                page->DeleteMember(key, member.c_str());
            }
            const auto screenEntry = gClipHandlers.find(screen);
            if (screenEntry == gClipHandlers.end()) {
                return;
            }
            const auto clipEntry = screenEntry->second.find(key);
            if (clipEntry == screenEntry->second.end()) {
                return;
            }
            clipEntry->second.erase(event);
            if (clipEntry->second.empty()) {
                screenEntry->second.erase(clipEntry);
            }
        }

        // JS: NativeUI.createScreen({ library?, movie?, assets?, layer? })
        void JS_CreateScreen(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = isolate->GetCurrentContext();

            KCDC::Features::UI::NativeUIService *screens = Screens();
            if (screens == nullptr) {
                ThrowError(isolate, "NativeUI.createScreen: the client is not running");
                return;
            }
            const std::string resource = KCDC::ResolveCallingResource(isolate);
            if (resource.empty()) {
                ThrowError(isolate, "NativeUI.createScreen: the calling resource could not be identified");
                return;
            }

            std::string library;
            std::string movie;
            std::vector<std::string> assets;
            int layer = 0;
            if (info.Length() >= 1 && info[0]->IsObject()) {
                v8::Local<v8::Object> options = info[0].As<v8::Object>();
                library                       = ReadString(isolate, context, options, "library");
                movie                         = ReadString(isolate, context, options, "movie");
                bool present                  = false;
                layer                         = static_cast<int>(ReadNumber(isolate, context, options, "layer", 0.0, present));
                v8::Local<v8::Value> list;
                if (options->Get(context, v8pp::to_v8(isolate, "assets")).ToLocal(&list) && list->IsArray()) {
                    v8::Local<v8::Array> array = list.As<v8::Array>();
                    for (std::uint32_t index = 0; index < array->Length(); ++index) {
                        v8::Local<v8::Value> entry;
                        if (array->Get(context, index).ToLocal(&entry) && entry->IsString()) {
                            assets.push_back(v8pp::from_v8<std::string>(isolate, entry));
                        }
                    }
                }
            }

            std::string error;
            const std::uint32_t id = movie.empty() ? screens->OpenLibraryScreen(resource, library, error) : screens->OpenMovieScreen(resource, movie, assets, layer, error);
            if (id == 0) {
                ThrowError(isolate, "NativeUI.createScreen: " + (error.empty() ? std::string("the screen could not be opened") : error));
                return;
            }
            info.GetReturnValue().Set(v8pp::class_<NativeScreen>::create_object(isolate, static_cast<double>(id)));
        }

        // JS: NativeUI.element(name, instanceId?)
        void JS_Element(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);
            if (info.Length() < 1 || !info[0]->IsString()) {
                ThrowError(isolate, "NativeUI.element: expected (name, instanceId?)");
                return;
            }
            const std::string name     = v8pp::from_v8<std::string>(isolate, info[0]);
            const int instanceId       = info.Length() >= 2 && info[1]->IsNumber() ? static_cast<int>(v8pp::from_v8<double>(isolate, info[1])) : 0;
            const std::string resource = KCDC::ResolveCallingResource(isolate);
            if (resource.empty()) {
                ThrowError(isolate, "NativeUI.element: the calling resource could not be identified");
                return;
            }
            std::string error;
            KCDC::Features::UI::NativeElement *element = KCDC::Features::UI::NativeElement::Acquire(name, instanceId, error);
            if (element == nullptr) {
                ThrowError(isolate, "NativeUI.element: " + error);
                return;
            }
            std::ignore = resource;
            info.GetReturnValue().Set(v8pp::class_<NativeElement>::create_object(isolate, name, static_cast<double>(instanceId)));
        }

        // JS: NativeUI.libraries()
        void JS_Libraries(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context                                    = isolate->GetCurrentContext();
            const std::vector<KCDC::Core::NativeUIAssets::Library> &libraries = KCDC::Core::NativeUIAssets::Libraries();
            v8::Local<v8::Array> result                                       = v8::Array::New(isolate, static_cast<int>(libraries.size()));
            for (std::size_t index = 0; index < libraries.size(); ++index) {
                const KCDC::Core::NativeUIAssets::Library &library = libraries[index];
                v8::Local<v8::Object> entry                        = v8::Object::New(isolate);
                std::ignore                                        = entry->Set(context, v8pp::to_v8(isolate, "name"), v8pp::to_v8(isolate, library.name));
                std::ignore                                        = entry->Set(context, v8pp::to_v8(isolate, "movie"), v8pp::to_v8(isolate, library.movie));
                v8::Local<v8::Array> exports                       = v8::Array::New(isolate, static_cast<int>(library.exports.size()));
                for (std::size_t slot = 0; slot < library.exports.size(); ++slot) {
                    std::ignore = exports->Set(context, static_cast<std::uint32_t>(slot), v8pp::to_v8(isolate, library.exports[slot]));
                }
                std::ignore = entry->Set(context, v8pp::to_v8(isolate, "exports"), exports);
                std::ignore = result->Set(context, static_cast<std::uint32_t>(index), entry);
            }
            info.GetReturnValue().Set(result);
        }

        NativeScreen *SelfScreen(const v8::FunctionCallbackInfo<v8::Value> &info) {
            return v8pp::class_<NativeScreen>::unwrap_object(info.GetIsolate(), info.This());
        }
        NativeClip *SelfClip(const v8::FunctionCallbackInfo<v8::Value> &info) {
            return v8pp::class_<NativeClip>::unwrap_object(info.GetIsolate(), info.This());
        }
        NativeElement *SelfElement(const v8::FunctionCallbackInfo<v8::Value> &info) {
            return v8pp::class_<NativeElement>::unwrap_object(info.GetIsolate(), info.This());
        }

        // JS: screen.close()
        void JS_ScreenClose(const v8::FunctionCallbackInfo<v8::Value> &info) {
            NativeScreen *self                           = SelfScreen(info);
            KCDC::Features::UI::NativeUIService *screens = Screens();
            if (self == nullptr || screens == nullptr) {
                info.GetReturnValue().Set(false);
                return;
            }
            gScreenHandlers.erase(self->GetId());
            gClipHandlers.erase(self->GetId());
            info.GetReturnValue().Set(screens->CloseScreen(self->GetId()));
        }

        // JS: screen.setVisible(visible)
        void JS_ScreenSetVisible(const v8::FunctionCallbackInfo<v8::Value> &info) {
            NativeScreen *self                           = SelfScreen(info);
            KCDC::Features::UI::NativeUIService *screens = Screens();
            const bool visible                           = info.Length() < 1 || info[0]->BooleanValue(info.GetIsolate());
            info.GetReturnValue().Set(self != nullptr && screens != nullptr && screens->SetVisible(self->GetId(), visible));
        }

        // JS: screen.focus()
        void JS_ScreenFocus(const v8::FunctionCallbackInfo<v8::Value> &info) {
            NativeScreen *self                           = SelfScreen(info);
            KCDC::Features::UI::NativeUIService *screens = Screens();
            info.GetReturnValue().Set(self != nullptr && screens != nullptr && screens->Focus(self->GetId()));
        }

        // JS: screen.blur()
        void JS_ScreenBlur(const v8::FunctionCallbackInfo<v8::Value> &info) {
            NativeScreen *self                           = SelfScreen(info);
            KCDC::Features::UI::NativeUIService *screens = Screens();
            if (self != nullptr && screens != nullptr) {
                screens->Blur(self->GetId());
            }
        }

        // JS: screen.hasFocus()
        void JS_ScreenHasFocus(const v8::FunctionCallbackInfo<v8::Value> &info) {
            NativeScreen *self                           = SelfScreen(info);
            KCDC::Features::UI::NativeUIService *screens = Screens();
            info.GetReturnValue().Set(self != nullptr && screens != nullptr && screens->HasFocus(self->GetId()));
        }

        // JS: screen.stage() -> { width, height } | null
        void JS_ScreenStage(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate                         = info.GetIsolate();
            NativeScreen *self                           = SelfScreen(info);
            KCDC::Features::UI::NativeUIService *screens = Screens();
            KCDC::Core::NativeUIHost *host               = self != nullptr && screens != nullptr ? screens->GetHost(self->GetId()) : nullptr;
            int width                                    = 0;
            int height                                   = 0;
            if (host == nullptr || !host->GetStageSize(width, height)) {
                info.GetReturnValue().SetNull();
                return;
            }
            v8::Local<v8::Object> result   = v8::Object::New(isolate);
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            std::ignore                    = result->Set(context, v8pp::to_v8(isolate, "width"), v8pp::to_v8(isolate, static_cast<double>(width)));
            std::ignore                    = result->Set(context, v8pp::to_v8(isolate, "height"), v8pp::to_v8(isolate, static_cast<double>(height)));
            info.GetReturnValue().Set(result);
        }

        // JS: screen.screenToStage(x, y) -> { x, y } | null
        void JS_ScreenToStage(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            NativeScreen *self   = SelfScreen(info);
            if (info.Length() < 2 || !info[0]->IsNumber() || !info[1]->IsNumber()) {
                ThrowError(isolate, "screen.screenToStage: expected (x, y) as viewport fractions");
                return;
            }
            KCDC::Features::UI::NativeUIService *screens = Screens();
            KCDC::Core::NativeUIHost *host               = self != nullptr && screens != nullptr ? screens->GetHost(self->GetId()) : nullptr;
            float stageX                                 = 0.0f;
            float stageY                                 = 0.0f;
            if (host == nullptr || !host->ScreenToStage(static_cast<float>(v8pp::from_v8<double>(isolate, info[0])), static_cast<float>(v8pp::from_v8<double>(isolate, info[1])), stageX, stageY)) {
                info.GetReturnValue().SetNull();
                return;
            }
            v8::Local<v8::Object> result   = v8::Object::New(isolate);
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            std::ignore                    = result->Set(context, v8pp::to_v8(isolate, "x"), v8pp::to_v8(isolate, static_cast<double>(stageX)));
            std::ignore                    = result->Set(context, v8pp::to_v8(isolate, "y"), v8pp::to_v8(isolate, static_cast<double>(stageY)));
            info.GetReturnValue().Set(result);
        }

        // JS: screen.on(event, handler)
        void JS_ScreenOn(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            NativeScreen *self   = SelfScreen(info);
            if (self == nullptr || info.Length() < 2 || !info[0]->IsString() || !info[1]->IsFunction()) {
                ThrowError(isolate, "screen.on: expected (event, handler)");
                return;
            }
            const std::string event = v8pp::from_v8<std::string>(isolate, info[0]);
            if (!IsKnownEvent(kScreenEvents, event)) {
                ThrowError(isolate, "screen.on: unknown event " + event);
                return;
            }
            v8::Local<v8::Function> callback = info[1].As<v8::Function>();
            Handler handler;
            handler.callback.Reset(isolate, callback);
            handler.resource = KCDC::ResolveCallingResource(isolate, callback);
            gScreenHandlers[self->GetId()][event].emplace_back(std::move(handler));
        }

        // JS: screen.off(event)
        void JS_ScreenOff(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            NativeScreen *self   = SelfScreen(info);
            if (self == nullptr || info.Length() < 1 || !info[0]->IsString()) {
                ThrowError(isolate, "screen.off: expected (event)");
                return;
            }
            const auto found = gScreenHandlers.find(self->GetId());
            if (found != gScreenHandlers.end()) {
                found->second.erase(v8pp::from_v8<std::string>(isolate, info[0]));
            }
        }

        v8::Local<v8::Value> MakeClip(v8::Isolate *isolate, std::uint32_t screen, const std::string &key) {
            return v8pp::class_<NativeClip>::create_object(isolate, static_cast<double>(screen), key);
        }

        // JS: clip.attach(exportName)
        void JS_ClipAttach(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate                         = info.GetIsolate();
            NativeClip *self                             = SelfClip(info);
            KCDC::Features::UI::NativeUIService *screens = Screens();
            if (self == nullptr || screens == nullptr || info.Length() < 1 || !info[0]->IsString()) {
                ThrowError(isolate, "clip.attach: expected (exportName)");
                return;
            }
            std::shared_ptr<KCDC::Core::NativeUI> page = PageOf(self->GetScreen());
            if (!page) {
                info.GetReturnValue().SetNull();
                return;
            }
            const std::string exportName = v8pp::from_v8<std::string>(isolate, info[0]);
            const std::string key        = screens->NextKey(self->GetScreen());
            if (key.empty() || !page->CreateClip(key, self->GetKey(), key.c_str(), exportName.c_str())) {
                info.GetReturnValue().SetNull();
                return;
            }
            info.GetReturnValue().Set(MakeClip(isolate, self->GetScreen(), key));
        }

        // JS: clip.createClip()
        void JS_ClipCreate(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate                         = info.GetIsolate();
            NativeClip *self                             = SelfClip(info);
            KCDC::Features::UI::NativeUIService *screens = Screens();
            std::shared_ptr<KCDC::Core::NativeUI> page   = self != nullptr ? PageOf(self->GetScreen()) : nullptr;
            if (self == nullptr || screens == nullptr || !page) {
                info.GetReturnValue().SetNull();
                return;
            }
            const std::string key = screens->NextKey(self->GetScreen());
            if (key.empty() || !page->CreateClip(key, self->GetKey(), key.c_str())) {
                info.GetReturnValue().SetNull();
                return;
            }
            info.GetReturnValue().Set(MakeClip(isolate, self->GetScreen(), key));
        }

        // JS: clip.createText({ x, y, width, height, text?, html? })
        void JS_ClipCreateText(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate                         = info.GetIsolate();
            NativeClip *self                             = SelfClip(info);
            KCDC::Features::UI::NativeUIService *screens = Screens();
            if (self == nullptr || screens == nullptr || info.Length() < 1 || !info[0]->IsObject()) {
                ThrowError(isolate, "clip.createText: expected ({ x, y, width, height, text?, html? })");
                return;
            }
            std::shared_ptr<KCDC::Core::NativeUI> page = PageOf(self->GetScreen());
            if (!page) {
                info.GetReturnValue().SetNull();
                return;
            }
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            v8::Local<v8::Object> options  = info[0].As<v8::Object>();
            bool present                   = false;
            const double x                 = ReadNumber(isolate, context, options, "x", 0.0, present);
            const double y                 = ReadNumber(isolate, context, options, "y", 0.0, present);
            const double width             = ReadNumber(isolate, context, options, "width", 0.0, present);
            const double height            = ReadNumber(isolate, context, options, "height", 0.0, present);
            const std::string key          = screens->NextKey(self->GetScreen());
            if (key.empty() || !page->CreateTextField(key, self->GetKey(), key.c_str(), x, y, width, height)) {
                info.GetReturnValue().SetNull();
                return;
            }
            page->SetTextStyle(key, ReadTextStyle(isolate, context, options));
            const std::string text = ReadString(isolate, context, options, "text");
            const std::string html = ReadString(isolate, context, options, "html");
            if (!html.empty()) {
                page->SetValue(key, "html", SDK::SFlashVarValue::Bool(true));
                page->SetText(key, html.c_str(), true);
            }
            else if (!text.empty()) {
                page->SetText(key, text.c_str(), false);
            }
            info.GetReturnValue().Set(MakeClip(isolate, self->GetScreen(), key));
        }

        // JS: clip.set({ x?, y?, rotation?, scaleX?, scaleY?, alpha?, visible? })
        void JS_ClipSet(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            NativeClip *self     = SelfClip(info);
            if (self == nullptr || info.Length() < 1 || !info[0]->IsObject()) {
                ThrowError(isolate, "clip.set: expected ({ x?, y?, rotation?, scaleX?, scaleY?, alpha?, visible? })");
                return;
            }
            std::shared_ptr<KCDC::Core::NativeUI> page = PageOf(self->GetScreen());
            if (!page) {
                info.GetReturnValue().Set(false);
                return;
            }
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            v8::Local<v8::Object> options  = info[0].As<v8::Object>();
            SDK::SFlashDisplayInfo display;
            bool present   = false;
            const double x = ReadNumber(isolate, context, options, "x", 0.0, present);
            if (present) {
                display.SetX(static_cast<float>(x));
            }
            const double y = ReadNumber(isolate, context, options, "y", 0.0, present);
            if (present) {
                display.SetY(static_cast<float>(y));
            }
            const double rotation = ReadNumber(isolate, context, options, "rotation", 0.0, present);
            if (present) {
                display.SetRotation(static_cast<float>(rotation));
            }
            // Multipliers here, percentages in the movie: 1 would mean one percent.
            const double scaleX = ReadNumber(isolate, context, options, "scaleX", 1.0, present);
            if (present) {
                display.SetXScale(static_cast<float>(scaleX * 100.0));
            }
            const double scaleY = ReadNumber(isolate, context, options, "scaleY", 1.0, present);
            if (present) {
                display.SetYScale(static_cast<float>(scaleY * 100.0));
            }
            const double alpha = ReadNumber(isolate, context, options, "alpha", 1.0, present);
            if (present) {
                display.SetAlpha(static_cast<float>(alpha * 100.0));
            }
            v8::Local<v8::Value> visible;
            if (options->Get(context, v8pp::to_v8(isolate, "visible")).ToLocal(&visible) && visible->IsBoolean()) {
                display.SetVisible(visible->BooleanValue(isolate));
            }
            info.GetReturnValue().Set(page->SetDisplayInfo(self->GetKey(), display));
        }

        // JS: clip.get(member)
        void JS_ClipGet(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            NativeClip *self     = SelfClip(info);
            if (self == nullptr || info.Length() < 1 || !info[0]->IsString()) {
                ThrowError(isolate, "clip.get: expected (member)");
                return;
            }
            std::shared_ptr<KCDC::Core::NativeUI> page = PageOf(self->GetScreen());
            if (!page) {
                info.GetReturnValue().SetNull();
                return;
            }
            const std::string member = v8pp::from_v8<std::string>(isolate, info[0]);
            SDK::SFlashVarValue value {};
            std::string text;
            if (!page->GetValue(self->GetKey(), member.c_str(), value, text)) {
                info.GetReturnValue().SetNull();
                return;
            }
            info.GetReturnValue().Set(ToV8(isolate, value));
        }

        // JS: clip.setMember(member, value)
        void JS_ClipSetMember(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            NativeClip *self     = SelfClip(info);
            if (self == nullptr || info.Length() < 2 || !info[0]->IsString()) {
                ThrowError(isolate, "clip.setMember: expected (member, value)");
                return;
            }
            std::shared_ptr<KCDC::Core::NativeUI> page = PageOf(self->GetScreen());
            if (!page) {
                info.GetReturnValue().Set(false);
                return;
            }
            std::vector<std::unique_ptr<std::string>> storage;
            SDK::SFlashVarValue value {};
            if (!FromV8(isolate, info[1], storage, value)) {
                ThrowError(isolate, "clip.setMember: only numbers, strings, booleans and null cross into ActionScript");
                return;
            }
            info.GetReturnValue().Set(page->SetValue(self->GetKey(), v8pp::from_v8<std::string>(isolate, info[0]).c_str(), value));
        }

        // JS: clip.setTextStyle({ font?, size?, color?, bold?, italic?, align? })
        void JS_ClipSetTextStyle(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            NativeClip *self     = SelfClip(info);
            if (self == nullptr || info.Length() < 1 || !info[0]->IsObject()) {
                ThrowError(isolate, "clip.setTextStyle: expected ({ font?, size?, color?, bold?, italic?, align? })");
                return;
            }
            std::shared_ptr<KCDC::Core::NativeUI> page = PageOf(self->GetScreen());
            if (!page) {
                info.GetReturnValue().Set(false);
                return;
            }
            info.GetReturnValue().Set(page->SetTextStyle(self->GetKey(), ReadTextStyle(isolate, isolate->GetCurrentContext(), info[0].As<v8::Object>())));
        }

        // JS: clip.setText(text, html?)
        void JS_ClipSetText(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            NativeClip *self     = SelfClip(info);
            if (self == nullptr || info.Length() < 1 || !info[0]->IsString()) {
                ThrowError(isolate, "clip.setText: expected (text, html?)");
                return;
            }
            std::shared_ptr<KCDC::Core::NativeUI> page = PageOf(self->GetScreen());
            if (!page) {
                info.GetReturnValue().Set(false);
                return;
            }
            const std::string text = v8pp::from_v8<std::string>(isolate, info[0]);
            const bool html        = info.Length() >= 2 && info[1]->BooleanValue(isolate);
            info.GetReturnValue().Set(page->SetText(self->GetKey(), text.c_str(), html));
        }

        // JS: clip.getText(html?)
        void JS_ClipGetText(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate                       = info.GetIsolate();
            NativeClip *self                           = SelfClip(info);
            std::shared_ptr<KCDC::Core::NativeUI> page = self != nullptr ? PageOf(self->GetScreen()) : nullptr;
            if (!page) {
                info.GetReturnValue().SetNull();
                return;
            }
            const bool html = info.Length() >= 1 && info[0]->BooleanValue(isolate);
            std::string text;
            if (!page->GetText(self->GetKey(), text, html)) {
                info.GetReturnValue().SetNull();
                return;
            }
            info.GetReturnValue().Set(v8pp::to_v8(isolate, text));
        }

        // JS: clip.goto(frameOrLabel, play?)
        void JS_ClipGoto(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            NativeClip *self     = SelfClip(info);
            if (self == nullptr || info.Length() < 1) {
                ThrowError(isolate, "clip.goto: expected (frameOrLabel, play?)");
                return;
            }
            std::shared_ptr<KCDC::Core::NativeUI> page = PageOf(self->GetScreen());
            if (!page) {
                info.GetReturnValue().Set(false);
                return;
            }
            const bool play = info.Length() >= 2 && info[1]->BooleanValue(isolate);
            if (info[0]->IsNumber()) {
                const double frame = v8pp::from_v8<double>(isolate, info[0]);
                info.GetReturnValue().Set(frame >= 1.0 && page->GotoFrame(self->GetKey(), static_cast<unsigned int>(frame), play));
                return;
            }
            if (!info[0]->IsString()) {
                ThrowError(isolate, "clip.goto: the frame is a one-based number or a label");
                return;
            }
            info.GetReturnValue().Set(page->GotoLabel(self->GetKey(), v8pp::from_v8<std::string>(isolate, info[0]).c_str(), play));
        }

        // JS: clip.invoke(method, ...args)
        void JS_ClipInvoke(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            NativeClip *self     = SelfClip(info);
            if (self == nullptr || info.Length() < 1 || !info[0]->IsString()) {
                ThrowError(isolate, "clip.invoke: expected (method, ...args)");
                return;
            }
            std::shared_ptr<KCDC::Core::NativeUI> page = PageOf(self->GetScreen());
            if (!page) {
                info.GetReturnValue().SetNull();
                return;
            }
            std::vector<SDK::SFlashVarValue> values;
            std::vector<std::unique_ptr<std::string>> storage;
            if (!ReadArguments(info, 1, values, storage)) {
                return;
            }
            SDK::SFlashVarValue result {};
            if (!page->InvokeScalars(self->GetKey(), v8pp::from_v8<std::string>(isolate, info[0]).c_str(), values, &result)) {
                info.GetReturnValue().SetNull();
                return;
            }
            info.GetReturnValue().Set(ToV8(isolate, result));
        }

        // JS: clip.on(event, handler)
        void JS_ClipOn(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            NativeClip *self     = SelfClip(info);
            if (self == nullptr || info.Length() < 2 || !info[0]->IsString() || !info[1]->IsFunction()) {
                ThrowError(isolate, "clip.on: expected (event, handler)");
                return;
            }
            const std::string event = v8pp::from_v8<std::string>(isolate, info[0]);
            if (!IsKnownEvent(kClipEvents, event)) {
                ThrowError(isolate, "clip.on: unknown event " + event);
                return;
            }
            v8::Local<v8::Function> callback = info[1].As<v8::Function>();
            std::string error;
            if (!BindClipHandler(self->GetScreen(), self->GetKey(), event, isolate, callback, KCDC::ResolveCallingResource(isolate, callback), error)) {
                ThrowError(isolate, "clip.on: " + error);
                return;
            }
            info.GetReturnValue().Set(true);
        }

        // JS: clip.off(event)
        void JS_ClipOff(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            NativeClip *self     = SelfClip(info);
            if (self == nullptr || info.Length() < 1 || !info[0]->IsString()) {
                ThrowError(isolate, "clip.off: expected (event)");
                return;
            }
            UnbindClipHandler(self->GetScreen(), self->GetKey(), v8pp::from_v8<std::string>(isolate, info[0]));
        }

        /** The clip a mask/hit-area argument names, or empty to clear. */
        bool ReadClipArgument(const v8::FunctionCallbackInfo<v8::Value> &info, std::uint32_t screen, std::string &key) {
            key.clear();
            if (info.Length() < 1 || info[0]->IsNull() || info[0]->IsUndefined()) {
                return true;
            }
            NativeClip *other = v8pp::class_<NativeClip>::unwrap_object(info.GetIsolate(), info[0]);
            if (other == nullptr || other->GetScreen() != screen) {
                return false;
            }
            key = other->GetKey();
            return true;
        }

        // JS: clip.setMask(clip | null)
        void JS_ClipSetMask(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate                       = info.GetIsolate();
            NativeClip *self                           = SelfClip(info);
            std::shared_ptr<KCDC::Core::NativeUI> page = self != nullptr ? PageOf(self->GetScreen()) : nullptr;
            std::string mask;
            if (self == nullptr || !ReadClipArgument(info, self->GetScreen(), mask)) {
                ThrowError(isolate, "clip.setMask: expected (clip from this screen, or null)");
                return;
            }
            info.GetReturnValue().Set(page && page->SetMask(self->GetKey(), mask));
        }

        // JS: clip.setHitArea(clip | null)
        void JS_ClipSetHitArea(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate                       = info.GetIsolate();
            NativeClip *self                           = SelfClip(info);
            std::shared_ptr<KCDC::Core::NativeUI> page = self != nullptr ? PageOf(self->GetScreen()) : nullptr;
            std::string area;
            if (self == nullptr || !ReadClipArgument(info, self->GetScreen(), area)) {
                ThrowError(isolate, "clip.setHitArea: expected (clip from this screen, or null)");
                return;
            }
            info.GetReturnValue().Set(page && page->SetHitArea(self->GetKey(), area));
        }

        // JS: clip.load(url)
        void JS_ClipLoad(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            NativeClip *self     = SelfClip(info);
            if (self == nullptr || info.Length() < 1 || !info[0]->IsString()) {
                ThrowError(isolate, "clip.load: expected (url)");
                return;
            }
            std::shared_ptr<KCDC::Core::NativeUI> page = PageOf(self->GetScreen());
            info.GetReturnValue().Set(page && page->LoadClip(self->GetKey(), v8pp::from_v8<std::string>(isolate, info[0]).c_str()));
        }

        // JS: clip.remove()
        void JS_ClipRemove(const v8::FunctionCallbackInfo<v8::Value> &info) {
            NativeClip *self                           = SelfClip(info);
            std::shared_ptr<KCDC::Core::NativeUI> page = self != nullptr ? PageOf(self->GetScreen()) : nullptr;
            if (self == nullptr || !page) {
                info.GetReturnValue().Set(false);
                return;
            }
            const auto screenEntry = gClipHandlers.find(self->GetScreen());
            if (screenEntry != gClipHandlers.end()) {
                screenEntry->second.erase(self->GetKey());
            }
            info.GetReturnValue().Set(page->RemoveObject(self->GetKey()));
        }

        // JS: element.call(function, ...args)
        void JS_ElementCall(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            NativeElement *self  = SelfElement(info);
            if (self == nullptr || info.Length() < 1 || !info[0]->IsString()) {
                ThrowError(isolate, "element.call: expected (function, ...args)");
                return;
            }
            std::string error;
            KCDC::Features::UI::NativeElement *element = KCDC::Features::UI::NativeElement::Acquire(self->GetName(), self->GetInstanceId(), error);
            if (element == nullptr) {
                info.GetReturnValue().SetNull();
                return;
            }
            std::vector<SDK::SFlashVarValue> values;
            std::vector<std::unique_ptr<std::string>> storage;
            if (!ReadArguments(info, 1, values, storage)) {
                return;
            }
            SDK::SFlashVarValue result {};
            std::string text;
            if (!element->Call(v8pp::from_v8<std::string>(isolate, info[0]), values, result, text)) {
                info.GetReturnValue().SetNull();
                return;
            }
            info.GetReturnValue().Set(ToV8(isolate, result));
        }

        // JS: element.setVariable(name, value)
        void JS_ElementSetVariable(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            NativeElement *self  = SelfElement(info);
            if (self == nullptr || info.Length() < 2 || !info[0]->IsString()) {
                ThrowError(isolate, "element.setVariable: expected (name, value)");
                return;
            }
            std::string error;
            KCDC::Features::UI::NativeElement *element = KCDC::Features::UI::NativeElement::Acquire(self->GetName(), self->GetInstanceId(), error);
            std::vector<std::unique_ptr<std::string>> storage;
            SDK::SFlashVarValue value {};
            if (element == nullptr || !FromV8(isolate, info[1], storage, value)) {
                info.GetReturnValue().Set(false);
                return;
            }
            info.GetReturnValue().Set(element->SetVariable(v8pp::from_v8<std::string>(isolate, info[0]), value));
        }

        // JS: element.getVariable(name)
        void JS_ElementGetVariable(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            NativeElement *self  = SelfElement(info);
            if (self == nullptr || info.Length() < 1 || !info[0]->IsString()) {
                ThrowError(isolate, "element.getVariable: expected (name)");
                return;
            }
            std::string error;
            KCDC::Features::UI::NativeElement *element = KCDC::Features::UI::NativeElement::Acquire(self->GetName(), self->GetInstanceId(), error);
            SDK::SFlashVarValue value {};
            std::string text;
            if (element == nullptr || !element->GetVariable(v8pp::from_v8<std::string>(isolate, info[0]), value, text)) {
                info.GetReturnValue().SetNull();
                return;
            }
            info.GetReturnValue().Set(ToV8(isolate, value));
        }

        // JS: element.setArray(name, values)
        void JS_ElementSetArray(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            NativeElement *self  = SelfElement(info);
            if (self == nullptr || info.Length() < 2 || !info[0]->IsString() || !info[1]->IsArray()) {
                ThrowError(isolate, "element.setArray: expected (name, values)");
                return;
            }
            std::string error;
            KCDC::Features::UI::NativeElement *element = KCDC::Features::UI::NativeElement::Acquire(self->GetName(), self->GetInstanceId(), error);
            if (element == nullptr) {
                info.GetReturnValue().Set(false);
                return;
            }
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            v8::Local<v8::Array> list      = info[1].As<v8::Array>();
            std::vector<SDK::SFlashVarValue> values;
            std::vector<std::unique_ptr<std::string>> storage;
            for (std::uint32_t index = 0; index < list->Length(); ++index) {
                v8::Local<v8::Value> entry;
                SDK::SFlashVarValue value {};
                if (!list->Get(context, index).ToLocal(&entry) || !FromV8(isolate, entry, storage, value)) {
                    ThrowError(isolate, "element.setArray: only numbers, strings, booleans and null cross into ActionScript");
                    return;
                }
                values.push_back(value);
            }
            info.GetReturnValue().Set(element->SetArray(v8pp::from_v8<std::string>(isolate, info[0]), values));
        }

        // JS: element.getArray(name)
        void JS_ElementGetArray(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            NativeElement *self  = SelfElement(info);
            if (self == nullptr || info.Length() < 1 || !info[0]->IsString()) {
                ThrowError(isolate, "element.getArray: expected (name)");
                return;
            }
            std::string error;
            KCDC::Features::UI::NativeElement *element = KCDC::Features::UI::NativeElement::Acquire(self->GetName(), self->GetInstanceId(), error);
            std::vector<KCDC::Core::NativeUIValue> values;
            if (element == nullptr || !element->GetArray(v8pp::from_v8<std::string>(isolate, info[0]), values)) {
                info.GetReturnValue().SetNull();
                return;
            }
            v8::Local<v8::Array> result    = v8::Array::New(isolate, static_cast<int>(values.size()));
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            for (std::size_t index = 0; index < values.size(); ++index) {
                std::ignore = result->Set(context, static_cast<std::uint32_t>(index), ToV8(isolate, values[index]));
            }
            info.GetReturnValue().Set(result);
        }

        // JS: element.setClip(name, { x?, ... })
        void JS_ElementSetClip(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            NativeElement *self  = SelfElement(info);
            if (self == nullptr || info.Length() < 2 || !info[0]->IsString() || !info[1]->IsObject()) {
                ThrowError(isolate, "element.setClip: expected (name, { x?, y?, rotation?, scaleX?, scaleY?, alpha?, visible? })");
                return;
            }
            std::string error;
            KCDC::Features::UI::NativeElement *element = KCDC::Features::UI::NativeElement::Acquire(self->GetName(), self->GetInstanceId(), error);
            if (element == nullptr) {
                info.GetReturnValue().Set(false);
                return;
            }
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            v8::Local<v8::Object> options  = info[1].As<v8::Object>();
            SDK::SFlashDisplayInfo display;
            bool present   = false;
            const double x = ReadNumber(isolate, context, options, "x", 0.0, present);
            if (present) {
                display.SetX(static_cast<float>(x));
            }
            const double y = ReadNumber(isolate, context, options, "y", 0.0, present);
            if (present) {
                display.SetY(static_cast<float>(y));
            }
            const double rotation = ReadNumber(isolate, context, options, "rotation", 0.0, present);
            if (present) {
                display.SetRotation(static_cast<float>(rotation));
            }
            const double scaleX = ReadNumber(isolate, context, options, "scaleX", 1.0, present);
            if (present) {
                display.SetXScale(static_cast<float>(scaleX * 100.0));
            }
            const double scaleY = ReadNumber(isolate, context, options, "scaleY", 1.0, present);
            if (present) {
                display.SetYScale(static_cast<float>(scaleY * 100.0));
            }
            const double alpha = ReadNumber(isolate, context, options, "alpha", 1.0, present);
            if (present) {
                display.SetAlpha(static_cast<float>(alpha * 100.0));
            }
            v8::Local<v8::Value> visible;
            if (options->Get(context, v8pp::to_v8(isolate, "visible")).ToLocal(&visible) && visible->IsBoolean()) {
                display.SetVisible(visible->BooleanValue(isolate));
            }
            info.GetReturnValue().Set(element->SetMovieClipDisplay(v8pp::from_v8<std::string>(isolate, info[0]), display));
        }

        bool WithElement(const v8::FunctionCallbackInfo<v8::Value> &info, const std::function<bool(KCDC::Features::UI::NativeElement &)> &body) {
            NativeElement *self = SelfElement(info);
            if (self == nullptr) {
                return false;
            }
            std::string error;
            KCDC::Features::UI::NativeElement *element = KCDC::Features::UI::NativeElement::Acquire(self->GetName(), self->GetInstanceId(), error);
            return element != nullptr && body(*element);
        }

        /** The display fields of one of the element's declared clips. */
        void JS_ElementGetClip(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            NativeElement *self  = SelfElement(info);
            if (self == nullptr || info.Length() < 1 || !info[0]->IsString()) {
                ThrowError(isolate, "element.getClip: expected (name)");
                return;
            }
            std::string error;
            KCDC::Features::UI::NativeElement *element = KCDC::Features::UI::NativeElement::Acquire(self->GetName(), self->GetInstanceId(), error);
            SDK::SFlashDisplayInfo display;
            if (element == nullptr || !element->GetMovieClipDisplay(v8pp::from_v8<std::string>(isolate, info[0]), display)) {
                info.GetReturnValue().SetNull();
                return;
            }
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            v8::Local<v8::Object> result   = v8::Object::New(isolate);
            std::ignore                    = result->Set(context, v8pp::to_v8(isolate, "x"), v8pp::to_v8(isolate, static_cast<double>(display.GetX())));
            std::ignore                    = result->Set(context, v8pp::to_v8(isolate, "y"), v8pp::to_v8(isolate, static_cast<double>(display.GetY())));
            std::ignore                    = result->Set(context, v8pp::to_v8(isolate, "rotation"), v8pp::to_v8(isolate, static_cast<double>(display.GetRotation())));
            // Back out of the movie's percentages, the way `set` goes into them.
            std::ignore = result->Set(context, v8pp::to_v8(isolate, "scaleX"), v8pp::to_v8(isolate, static_cast<double>(display.GetXScale()) / 100.0));
            std::ignore = result->Set(context, v8pp::to_v8(isolate, "scaleY"), v8pp::to_v8(isolate, static_cast<double>(display.GetYScale()) / 100.0));
            std::ignore = result->Set(context, v8pp::to_v8(isolate, "alpha"), v8pp::to_v8(isolate, static_cast<double>(display.GetAlpha()) / 100.0));
            std::ignore = result->Set(context, v8pp::to_v8(isolate, "visible"), v8pp::to_v8(isolate, display.GetVisible()));
            info.GetReturnValue().Set(result);
        }

        // JS: element.gotoClip(name, frameOrLabel, play?)
        void JS_ElementGotoClip(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            NativeElement *self  = SelfElement(info);
            if (self == nullptr || info.Length() < 2 || !info[0]->IsString()) {
                ThrowError(isolate, "element.gotoClip: expected (name, frameOrLabel, play?)");
                return;
            }
            std::string error;
            KCDC::Features::UI::NativeElement *element = KCDC::Features::UI::NativeElement::Acquire(self->GetName(), self->GetInstanceId(), error);
            if (element == nullptr) {
                info.GetReturnValue().Set(false);
                return;
            }
            const std::string name = v8pp::from_v8<std::string>(isolate, info[0]);
            const bool play        = info.Length() >= 3 && info[2]->BooleanValue(isolate);
            if (info[1]->IsNumber()) {
                const double frame = v8pp::from_v8<double>(isolate, info[1]);
                info.GetReturnValue().Set(frame >= 1.0 && element->GotoMovieClipFrame(name, static_cast<unsigned int>(frame), nullptr, play));
                return;
            }
            if (!info[1]->IsString()) {
                ThrowError(isolate, "element.gotoClip: the frame is a one-based number or a label");
                return;
            }
            info.GetReturnValue().Set(element->GotoMovieClipFrame(name, 0, v8pp::from_v8<std::string>(isolate, info[1]).c_str(), play));
        }

        // JS: element.show() / hide() / requestHide()
        void JS_ElementShow(const v8::FunctionCallbackInfo<v8::Value> &info) {
            info.GetReturnValue().Set(WithElement(info, [](KCDC::Features::UI::NativeElement &element) {
                return element.Show();
            }));
        }

        void JS_ElementHide(const v8::FunctionCallbackInfo<v8::Value> &info) {
            info.GetReturnValue().Set(WithElement(info, [](KCDC::Features::UI::NativeElement &element) {
                return element.Hide();
            }));
        }

        void JS_ElementRequestHide(const v8::FunctionCallbackInfo<v8::Value> &info) {
            info.GetReturnValue().Set(WithElement(info, [](KCDC::Features::UI::NativeElement &element) {
                return element.RequestHide();
            }));
        }

        void JS_ElementReload(const v8::FunctionCallbackInfo<v8::Value> &info) {
            info.GetReturnValue().Set(WithElement(info, [](KCDC::Features::UI::NativeElement &element) {
                return element.Reload();
            }));
        }

        void JS_ElementUnload(const v8::FunctionCallbackInfo<v8::Value> &info) {
            info.GetReturnValue().Set(WithElement(info, [](KCDC::Features::UI::NativeElement &element) {
                return element.Unload();
            }));
        }

        // JS: element.on(event, handler)
        void JS_ElementOn(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            NativeElement *self  = SelfElement(info);
            if (self == nullptr || info.Length() < 2 || !info[0]->IsString() || !info[1]->IsFunction()) {
                ThrowError(isolate, "element.on: expected (event, handler)");
                return;
            }
            std::string error;
            KCDC::Features::UI::NativeElement *element = KCDC::Features::UI::NativeElement::Acquire(self->GetName(), self->GetInstanceId(), error);
            if (element == nullptr || !element->Listen()) {
                ThrowError(isolate, "element.on: " + (error.empty() ? std::string("the element refused a listener") : error));
                return;
            }
            v8::Local<v8::Function> callback = info[1].As<v8::Function>();
            Handler handler;
            handler.callback.Reset(isolate, callback);
            handler.resource = KCDC::ResolveCallingResource(isolate, callback);
            gElementHandlers[element->GetKey()][v8pp::from_v8<std::string>(isolate, info[0])].emplace_back(std::move(handler));
        }

        // JS: element.off(event)
        void JS_ElementOff(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            NativeElement *self  = SelfElement(info);
            if (self == nullptr || info.Length() < 1 || !info[0]->IsString()) {
                ThrowError(isolate, "element.off: expected (event)");
                return;
            }
            const auto found = gElementHandlers.find(KCDC::Features::UI::NativeElement::MakeKey(self->GetName(), self->GetInstanceId()));
            if (found != gElementHandlers.end()) {
                found->second.erase(v8pp::from_v8<std::string>(isolate, info[0]));
            }
        }
    } // namespace

    bool NativeScreen::IsValid() const {
        KCDC::Features::UI::NativeUIService *screens = Screens();
        return screens != nullptr && !screens->GetResource(_id).empty();
    }

    bool NativeScreen::IsReady() const {
        KCDC::Features::UI::NativeUIService *screens = Screens();
        return screens != nullptr && screens->IsMounted(_id);
    }

    v8::Local<v8::Value> NativeScreen::GetRoot() const {
        v8::Isolate *isolate = v8::Isolate::GetCurrent();
        if (isolate == nullptr) {
            return {};
        }
        if (!IsReady()) {
            return v8::Null(isolate);
        }
        return v8pp::class_<NativeClip>::create_object(isolate, static_cast<double>(_id), std::string("$root"));
    }

    std::string NativeScreen::ToString() const {
        return "NativeScreen{ id: " + std::to_string(_id) + (IsReady() ? ", ready }" : IsValid() ? ", opening }" : ", closed }");
    }

    bool NativeClip::IsValid() const {
        std::shared_ptr<KCDC::Core::NativeUI> page = PageOf(_screen);
        return page && page->HasObject(_key);
    }

    std::string NativeClip::ToString() const {
        return "NativeClip{ screen: " + std::to_string(_screen) + ", key: " + _key + (IsValid() ? " }" : ", gone }");
    }

    bool NativeElement::IsValid() const {
        std::string error;
        return KCDC::Features::UI::NativeElement::Acquire(_name, _instanceId, error) != nullptr;
    }

    bool NativeElement::IsVisible() const {
        std::string error;
        KCDC::Features::UI::NativeElement *element = KCDC::Features::UI::NativeElement::Acquire(_name, _instanceId, error);
        return element != nullptr && element->IsVisible();
    }

    std::string NativeElement::ToString() const {
        return "NativeElement{ " + _name + "@" + std::to_string(_instanceId) + " }";
    }

    v8pp::class_<NativeClip> &NativeClip::GetClass(v8::Isolate *isolate) {
        if (!_class) {
            _class = std::make_unique<v8pp::class_<NativeClip>>(isolate, ClientCatalog(), "NativeClip", "One display object inside a native screen: a clip, an attached sprite or a text field.");
            _class->auto_wrap_objects(true);
            _class->ctor<double, std::string>(v8pp::metadata::docs("void", {v8pp::metadata::param("screen", "number", false, "Screen this handle belongs to."), v8pp::metadata::param("key", "string", false, "Handle key inside that screen.")},
                "Rebuilds a handle for a display object that already exists. Clips come from `screen.root` and the create calls on it."));
            _class->function("toString", &NativeClip::ToString, v8pp::metadata::docs("string", {}, "Formats this handle for logging."));

            _class->prototype_function("attach", &JS_ClipAttach,
                v8pp::metadata::docs("NativeClip | null", {v8pp::metadata::param("exportName", "string", false, "Linkage name of a sprite the screen's library exports.")},
                    "Attaches one of the game's own exported sprites as a child of this clip. Which names exist depends on the library the screen was opened with.", "The new child, or null when the library has no such export."));
            _class->prototype_function("createClip", &JS_ClipCreate, v8pp::metadata::docs("NativeClip | null", {}, "Creates an empty child clip, for grouping and for drawing into.", "The new child, or null when the screen is not composed."));
            _class->prototype_function("createText", &JS_ClipCreateText,
                v8pp::metadata::docs("NativeClip | null",
                    {v8pp::metadata::param("options", "{ x: number; y: number; width: number; height: number; text?: string; html?: string }", false, "Where the field goes in this clip's own coordinates, how big it is, and what it starts with.")},
                    "Creates a native text field as a child of this clip.", "The new field, or null when the screen is not composed."));
            _class->prototype_function("set", &JS_ClipSet,
                v8pp::metadata::docs("boolean",
                    {v8pp::metadata::param("options", "{ x?: number; y?: number; rotation?: number; scaleX?: number; scaleY?: number; alpha?: number; visible?: boolean }", false,
                        "Only the fields present are applied. Position is in stage units, rotation in degrees, scale as a multiplier where 1 is natural size, alpha from 0 to 1.")},
                    "Moves, scales, rotates, fades or hides this display object.", "True when the movie accepted the update."));
            _class->prototype_function("get", &JS_ClipGet,
                v8pp::metadata::docs("any", {v8pp::metadata::param("member", "string", false, "ActionScript property name, such as `_width` or `_currentframe`.")}, "Reads one scalar property off this display object. Property access can run an ActionScript getter.",
                    "The value, or null when it is absent or not a scalar."));
            _class->prototype_function("setMember", &JS_ClipSetMember,
                v8pp::metadata::docs("boolean", {v8pp::metadata::param("member", "string", false, "ActionScript property name."), v8pp::metadata::param("value", "string | number | boolean | null", false, "What to assign.")}, "Assigns one scalar property on this display object.",
                    "True when the movie accepted it."));
            _class->prototype_function("setTextStyle", &JS_ClipSetTextStyle,
                v8pp::metadata::docs("boolean",
                    {v8pp::metadata::param("options", "{ font?: string; size?: number; color?: number; bold?: boolean; italic?: boolean; align?: 'left' | 'center' | 'right' | 'justify' }", false,
                        "`font` is one of the game's own mapped names -- DefaultFont, DefaultFontBold, DefaultFontItalic, LightFont, LightFontBold, LightFontItalic, DisplayFont, Manuscript -- and not a face name. `color` is 0xRRGGBB.")},
                    "Sets how this field draws, for the text it holds and the text it is given next. A field created through `createText` already has the default applied; this restyles it.", "True when the movie accepted the format."));
            _class->prototype_function("setText", &JS_ClipSetText,
                v8pp::metadata::docs("boolean", {v8pp::metadata::param("text", "string", false, "The text to show."), v8pp::metadata::param("html", "boolean", true, "Parse it as the engine's HTML subset instead of plain text.")},
                    "Writes this field's text. On a clip rather than a field it assigns the `text` property, which is not the same as writing a field inside it.", "True when the movie accepted the write."));
            _class->prototype_function("getText", &JS_ClipGetText,
                v8pp::metadata::docs("string | null", {v8pp::metadata::param("html", "boolean", true, "Read the generated markup instead of the plain text.")}, "Reads this field's text back.", "The text, or null when there is none to read."));
            _class->prototype_function("goto", &JS_ClipGoto,
                v8pp::metadata::docs("boolean", {v8pp::metadata::param("frameOrLabel", "number | string", false, "A one-based frame number, or a frame label."), v8pp::metadata::param("play", "boolean", true, "Keep playing from there instead of stopping.")},
                    "Moves this clip's own timeline. A timeline change can replace named children, so reacquire anything below it afterwards.", "True when the frame or label resolved."));
            _class->prototype_function("invoke", &JS_ClipInvoke,
                v8pp::metadata::docs("any", {v8pp::metadata::param("method", "string", false, "Method name on this object."), v8pp::metadata::param("args", "(string | number | boolean | null)[]", true, "Scalar arguments. They may also be passed loose, one per parameter.")},
                    "Calls a method on this display object. Only scalars cross, and the result is a scalar.", "What ActionScript returned, or null when the call failed."));
            _class->prototype_function("on", &JS_ClipOn,
                v8pp::metadata::docs("boolean",
                    {v8pp::metadata::param("event", "'press' | 'release' | 'releaseOutside' | 'rollOver' | 'rollOut' | 'dragOver' | 'dragOut'", false, "Which mouse handler to install."),
                        v8pp::metadata::param("handler", "NativeClipHandler<NativeClip>", false, "Called on the next tick after the movie raises it.")},
                    "Installs one ActionScript mouse handler on this clip and enables it as a mouse target. The screen also has to hold input focus for the mouse to reach it.", "True when the handler was installed."));
            _class->prototype_function("off", &JS_ClipOff, v8pp::metadata::docs("void", {v8pp::metadata::param("event", "string", false, "The handler to take off.")}, "Removes one mouse handler, deleting the ActionScript member rather than assigning null to it."));
            _class->prototype_function("setMask", &JS_ClipSetMask,
                v8pp::metadata::docs("boolean", {v8pp::metadata::param("mask", "NativeClip | null", false, "A clip from this same screen, or null to clear.")}, "Binds another clip as this one's mask. One mask belongs to one target: binding it elsewhere detaches it from here.",
                    "True when the movie took the binding."));
            _class->prototype_function("setHitArea", &JS_ClipSetHitArea,
                v8pp::metadata::docs("boolean", {v8pp::metadata::param("area", "NativeClip | null", false, "A clip from this same screen, or null to clear.")}, "Makes another clip the region this one is clicked through, for a target whose artwork is the wrong shape to click.",
                    "True when the movie took the binding."));
            _class->prototype_function("load", &JS_ClipLoad,
                v8pp::metadata::docs("boolean", {v8pp::metadata::param("url", "string", false, "`img://` or `imgps://` for the engine's image loader, or a movie path inside the mounted archive.")},
                    "Loads an external image or movie into this clip, replacing what it holds. A missing asset installs the engine's placeholder rather than failing, so a true result is not an existence check.", "True when the request was accepted."));
            _class->prototype_function("remove", &JS_ClipRemove, v8pp::metadata::docs("boolean", {}, "Takes this display object out of the screen, with its handlers.", "True when it was still there to remove."));

            _class->property("screen", &NativeClip::GetScreenNumber, v8pp::metadata::property_docs("number", "Identifier of the screen this handle belongs to."));
            _class->property("key", &NativeClip::GetKeyString, v8pp::metadata::property_docs("string", "The screen-local key this handle resolves through."));
            _class->property("valid", &NativeClip::IsValid, v8pp::metadata::property_docs("boolean", "Whether the screen still holds this object. A handle to a removed object keeps reading and every call on it fails cleanly."));
        }
        return *_class;
    }

    v8pp::class_<NativeScreen> &NativeScreen::GetClass(v8::Isolate *isolate) {
        if (!_class) {
            _class = std::make_unique<v8pp::class_<NativeScreen>>(isolate, ClientCatalog(), "NativeScreen", "A screen composed inside one of the game's own Scaleform movies.");
            _class->auto_wrap_objects(true);
            _class->ctor<double>(v8pp::metadata::docs("void", {v8pp::metadata::param("id", "number", false, "Identifier of an existing screen.")}, "Rebuilds a handle for a screen that already exists; use `NativeUI.createScreen()` to open one."));
            _class->function("toString", &NativeScreen::ToString, v8pp::metadata::docs("string", {}, "Formats this handle for logging."));

            _class->prototype_function("close", &JS_ScreenClose, v8pp::metadata::docs("boolean", {}, "Closes the screen, removing its subtree and every handler on it.", "True when it was still open."));
            _class->prototype_function("setVisible", &JS_ScreenSetVisible,
                v8pp::metadata::docs("boolean", {v8pp::metadata::param("visible", "boolean", true, "Defaults to true.")}, "Shows or hides this screen's own subtree, leaving anything else in the same movie alone.", "True when the movie accepted it."));
            _class->prototype_function("focus", &JS_ScreenFocus,
                v8pp::metadata::docs("boolean", {}, "Takes keyboard, controller and mouse input for this screen. One screen holds it at a time, and the game takes it back whenever a higher-priority context wants it.", "True when the screen now holds focus."));
            _class->prototype_function("blur", &JS_ScreenBlur, v8pp::metadata::docs("void", {}, "Gives up input focus, if this screen holds it."));
            _class->prototype_function("hasFocus", &JS_ScreenHasFocus, v8pp::metadata::docs("boolean", {}, "Whether this screen currently holds input focus.", "True while it does."));
            _class->prototype_function("stage", &JS_ScreenStage,
                v8pp::metadata::docs("{ width: number; height: number } | null", {}, "The movie's authored stage size, which is what positions inside the screen are expressed in. It is independent of the player's resolution.",
                    "The stage size, or null before the movie is composed."));
            _class->prototype_function("screenToStage", &JS_ScreenToStage,
                v8pp::metadata::docs("{ x: number; y: number } | null", {v8pp::metadata::param("x", "number", false, "Horizontal viewport fraction, 0 to 1."), v8pp::metadata::param("y", "number", false, "Vertical viewport fraction, 0 to 1.")},
                    "Converts a point on the player's screen into this movie's stage units, through the element's own viewport mapping.", "The stage point, or null before the movie is composed."));
            _class->prototype_function("on", &JS_ScreenOn,
                v8pp::metadata::docs("void", {v8pp::metadata::param("event", "'ready' | 'unload' | 'input' | 'event'", false, "Which notification to listen for."), v8pp::metadata::param("handler", "NativeScreenHandler", false, "Called on the next tick after it is raised.")},
                    "Listens for one of this screen's notifications. `ready` fires when the movie has composed the screen, which is not the same tick as the call that opened it; `unload` when the engine takes the movie away; `input` carries a focus action and its activation mode; `event` carries a declared movie event and its arguments."));
            _class->prototype_function("off", &JS_ScreenOff, v8pp::metadata::docs("void", {v8pp::metadata::param("event", "string", false, "The notification to stop listening for.")}, "Removes every handler this resource put on one notification."));

            _class->property("id", &NativeScreen::GetIdNumber, v8pp::metadata::property_docs("number", "Identifier of this screen, unique for the lifetime of the session."));
            _class->property("valid", &NativeScreen::IsValid, v8pp::metadata::property_docs("boolean", "Whether the screen is still open."));
            _class->property("ready", &NativeScreen::IsReady, v8pp::metadata::property_docs("boolean", "Whether the movie has composed it yet. A screen is asked for before its movie is ready, so this is false for the first few frames and `root` is null until it turns true."));
            _class->property("root", &NativeScreen::GetRoot, v8pp::metadata::property_docs("NativeClip | null", "The screen's own empty root clip, and the parent everything else is created under. Null until `ready`."));
        }
        return *_class;
    }

    v8pp::class_<NativeElement> &NativeElement::GetClass(v8::Isolate *isolate) {
        if (!_class) {
            _class = std::make_unique<v8pp::class_<NativeElement>>(isolate, ClientCatalog(), "NativeElement", "A handle on one of the game's own UI elements, driven where it stands.");
            _class->auto_wrap_objects(true);
            _class->ctor<std::string, double>(v8pp::metadata::docs("void", {v8pp::metadata::param("name", "string", false, "Element name, as its XML declares it."), v8pp::metadata::param("instanceId", "number", false, "Instance id.")},
                "Rebuilds a handle for an element; use `NativeUI.element()` to get one."));
            _class->function("toString", &NativeElement::ToString, v8pp::metadata::docs("string", {}, "Formats this handle for logging."));

            _class->prototype_function("call", &JS_ElementCall,
                v8pp::metadata::docs("any", {v8pp::metadata::param("fn", "string", false, "A function the element's XML declares, by its display name or its ActionScript name."), v8pp::metadata::param("args", "(string | number | boolean | null)[]", true, "Scalar arguments. They may also be passed loose, one per parameter.")},
                    "Calls one of the element's declared functions. The element has to be loaded: show it first, or call this on one the game already has up.", "What ActionScript returned, or null when the call failed."));
            _class->prototype_function("setVariable", &JS_ElementSetVariable,
                v8pp::metadata::docs("boolean", {v8pp::metadata::param("name", "string", false, "A variable the element's XML declares."), v8pp::metadata::param("value", "string | number | boolean | null", false, "What to assign.")},
                    "Assigns one of the element's declared variables.", "True when the movie accepted it."));
            _class->prototype_function("getVariable", &JS_ElementGetVariable,
                v8pp::metadata::docs("any", {v8pp::metadata::param("name", "string", false, "A variable the element's XML declares.")}, "Reads one of the element's declared variables.", "The value, or null when it is absent."));
            _class->prototype_function("setArray", &JS_ElementSetArray,
                v8pp::metadata::docs("boolean", {v8pp::metadata::param("name", "string", false, "An array the element's XML declares."), v8pp::metadata::param("values", "(string | number | boolean | null)[]", false, "The new contents.")},
                    "Replaces one of the element's declared arrays. This is how the game's own list screens are filled.", "True when the movie accepted it."));
            _class->prototype_function("getArray", &JS_ElementGetArray,
                v8pp::metadata::docs("any[] | null", {v8pp::metadata::param("name", "string", false, "An array the element's XML declares.")}, "Reads one of the element's declared arrays.", "Its contents, or null when it is absent."));
            _class->prototype_function("setClip", &JS_ElementSetClip,
                v8pp::metadata::docs("boolean",
                    {v8pp::metadata::param("name", "string", false, "A movie clip the element's XML declares."),
                        v8pp::metadata::param("options", "{ x?: number; y?: number; rotation?: number; scaleX?: number; scaleY?: number; alpha?: number; visible?: boolean }", false, "Only the fields present are applied.")},
                    "Moves one of the element's declared movie clips. Two of the game's elements declare any: the HUD and the lockpicking screen.", "True when the movie accepted it."));
            _class->prototype_function("getClip", &JS_ElementGetClip,
                v8pp::metadata::docs("{ x: number; y: number; rotation: number; scaleX: number; scaleY: number; alpha: number; visible: boolean } | null", {v8pp::metadata::param("name", "string", false, "A movie clip the element's XML declares.")},
                    "Reads one of the element's declared movie clips back. Scale and alpha come back as multipliers, the way `setClip` takes them.", "Its display fields, or null when the clip is absent."));
            _class->prototype_function("gotoClip", &JS_ElementGotoClip,
                v8pp::metadata::docs("boolean",
                    {v8pp::metadata::param("name", "string", false, "A movie clip the element's XML declares."), v8pp::metadata::param("frameOrLabel", "number | string", false, "A one-based frame number, or a frame label."),
                        v8pp::metadata::param("play", "boolean", true, "Keep playing from there instead of stopping.")},
                    "Moves a declared movie clip's timeline, which is how a clip with several authored looks is switched between them.", "True when the frame or label resolved."));
            _class->prototype_function("show", &JS_ElementShow, v8pp::metadata::docs("boolean", {}, "Shows the element, loading its movie if it was not loaded.", "True when the element took it."));
            _class->prototype_function("hide", &JS_ElementHide, v8pp::metadata::docs("boolean", {}, "Hides the element at once.", "True when the element took it."));
            _class->prototype_function("requestHide", &JS_ElementRequestHide, v8pp::metadata::docs("boolean", {}, "Asks the element to play its own hide transition instead of vanishing.", "True when the element took it."));
            _class->prototype_function("reload", &JS_ElementReload, v8pp::metadata::docs("boolean", {}, "Rebuilds the element's movie from cached assets. Everything composed into it is gone afterwards.", "True when the element took it."));
            _class->prototype_function("unload", &JS_ElementUnload, v8pp::metadata::docs("boolean", {}, "Drops the element's movie. The next `show` loads it again.", "True when the element took it."));
            _class->prototype_function("on", &JS_ElementOn,
                v8pp::metadata::docs("void", {v8pp::metadata::param("event", "string", false, "An event the element's XML declares, by its display name."), v8pp::metadata::param("handler", "NativeElementHandler", false, "Called on the next tick after the movie raises it.")},
                    "Listens for one of the element's declared events. This is how a click on a game screen reaches script."));
            _class->prototype_function("off", &JS_ElementOff, v8pp::metadata::docs("void", {v8pp::metadata::param("event", "string", false, "The event to stop listening for.")}, "Removes every handler this resource put on one event."));

            _class->property("name", &NativeElement::GetNameString, v8pp::metadata::property_docs("string", "The element's name, as its XML declares it."));
            _class->property("instanceId", &NativeElement::GetInstanceIdNumber, v8pp::metadata::property_docs("number", "Which instance this handle drives. Instance 0 is the one the game itself uses; any other number is a private copy of the same element."));
            _class->property("valid", &NativeElement::IsValid, v8pp::metadata::property_docs("boolean", "Whether the element is still registered."));
            _class->property("visible", &NativeElement::IsVisible, v8pp::metadata::property_docs("boolean", "Whether the element is visible right now."));
        }
        return *_class;
    }

    void NativeScreen::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        GetClass(isolate).publish(global);
    }

    void NativeClip::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        GetClass(isolate).publish(global);
    }

    void NativeElement::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        GetClass(isolate).publish(global);
    }

    void DropNativeUIHandlers() {
        gScreenHandlers.clear();
        gClipHandlers.clear();
        gElementHandlers.clear();
    }

    void RegisterNativeUI(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        if (isolate == nullptr || global.IsEmpty()) {
            return;
        }
        // The v8pp classes outlive the session: they belong to the isolate, which a reconnect keeps. These callbacks do not -- they are v8::Global handles into the context that was just thrown away.
        gScreenHandlers.clear();
        gClipHandlers.clear();
        gElementHandlers.clear();

        NativeScreen::Register(isolate, global);
        NativeClip::Register(isolate, global);
        NativeElement::Register(isolate, global);

        KCDC::Features::UI::NativeUIService *screens = Screens();
        if (screens != nullptr && !gSinkInstalled) {
            screens->AddSink([](const KCDC::Core::NativeUIEvent &event) {
                OnNativeEvent(event);
            });
            gSinkInstalled = true;
        }
        if (Framework::Scripting::ResourceManager *manager = KCDC::ScriptResources(); manager != nullptr && !gCleanupInstalled) {
            manager->AddOnResourceStopped([](const std::string &resource) {
                CleanupResource(resource);
            });
            manager->AddOnResourceStateChanged([](const std::string &resource, Framework::Scripting::ResourceState, Framework::Scripting::ResourceState state) {
                if (state == Framework::Scripting::ResourceState::Stopping || state == Framework::Scripting::ResourceState::Error) {
                    KCDC::Features::UI::GameMenus::Get().Release(resource);
                }
            });
            gCleanupInstalled = true;
        }

        v8pp::module native(isolate, ClientCatalog(), "NativeUI",
            "The game's own Scaleform UI, opened from script. This is the second way to draw a UI at this client, beside `Web`: a web view is a browser and lays out anything, a native screen is composed out of the game's own movies, sits inside the game's own layer stack and is navigable with a controller.");
        native.function("setMenuEnabled", &JS_SetMenuEnabled,
            v8pp::metadata::docs("void",
                {v8pp::metadata::param("menu", "\"inventory\" | \"player\" | \"map\" | \"codex\" | \"journal\" | \"crafting\"", false, "The vanilla menu, including its detail pages."),
                    v8pp::metadata::param("enabled", "boolean", false, "False prevents opening the menu; true releases this resource's restriction. All menus are enabled by default.")},
                "Prevents a vanilla menu from opening or being selected through native navigation. An already-open disabled menu closes on the next client tick. Requests are idempotent and resource-owned: a menu stays disabled while any resource disables it. Restrictions clear when the resource stops or the session ends. Key bindings are unchanged. Tab labels remain in the native screen. The custom character creator can still use the player stage."));
        native.function("isMenuEnabled", &JS_IsMenuEnabled,
            v8pp::metadata::docs("boolean", {v8pp::metadata::param("menu", "\"inventory\" | \"player\" | \"map\" | \"codex\" | \"journal\" | \"crafting\"", false, "The vanilla menu to query.")},
                "Whether every resource permits this vanilla menu. Native gameplay rules may still prevent it from opening. Throws for unknown menu names.", "True when no resource disables the menu."));
        native.function("createScreen", &JS_CreateScreen,
            v8pp::metadata::docs("NativeScreen",
                {v8pp::metadata::param("options", "{ library?: string; movie?: string; assets?: string[]; layer?: number }", true,
                    "`library` names which of the game's movies the screen may attach exported sprites from, empty for the default. `movie` is a path inside this resource to a movie it ships, which opens a screen in that instead. `assets` are further files that movie loads, and `layer` is where it sits in the game's own stack.")},
                "Opens a screen. It is composed a few frames later, when its movie reports ready -- listen for `ready` rather than drawing into it straight away.", "The screen handle."));
        native.function("libraries", &JS_Libraries,
            v8pp::metadata::docs("{ name: string; movie: string; exports: string[] }[]", {},
                "Lists the game movies a screen can be opened against and the linkage names each one exports, read from the bundle rather than from a list kept in step by hand. `name` is what `createScreen({library})` takes, and the empty one is the default host.",
                "One entry per bundled library."));
        native.function("element", &JS_Element,
            v8pp::metadata::docs("NativeElement",
                {v8pp::metadata::param("name", "string", false, "Element name, as the game's own XML declares it: `Menu`, `hud`, `ApseModalDialog` and the rest."), v8pp::metadata::param("instanceId", "number", true, "Which instance to drive; 0, the game's own, by default.")},
                "Takes a handle on one of the game's own UI elements. Picking an instance id other than 0 gives a private copy of that element, which is how a mod puts its own content on a game screen without fighting the game's copy.", "The element handle."));
        native.publish(global);
    }
} // namespace KCDC::Scripting
