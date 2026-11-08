#include "social_chat_scripting.h"

#include "core/scripting/script_context.h"
#include "core/scripting/wrap.h"
#include "core/server.h"

#include "shared/scripting/catalog.h"

#include <scripting/scripting_catalog.h>

#include <v8pp/convert.hpp>
#include <v8pp/module.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace KCDC::Scripting {
    void Chat::JS_SetDefaultRelay(const v8::FunctionCallbackInfo<v8::Value> &info) {
        v8::Isolate *isolate = info.GetIsolate();
        v8::HandleScope handleScope(isolate);
        if (info.Length() < 1) {
            isolate->ThrowError(v8pp::to_v8(isolate, "Chat.setDefaultRelay: expected (enabled)"));
            return;
        }

        if (auto *server = Server::Get()) {
            server->SetDefaultChatRelay(info[0]->BooleanValue(isolate));
        }
    }

    void Chat::JS_IsDefaultRelay(const v8::FunctionCallbackInfo<v8::Value> &info) {
        auto *server = Server::Get();
        info.GetReturnValue().Set(server != nullptr && server->IsDefaultChatRelay());
    }

    void Chat::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        if (isolate == nullptr || global.IsEmpty()) {
            return;
        }

        auto &events = ServerCatalog().data_type("EventMap");
        events.add_property("playerChat", "[player: Player, text: string]", "Dispatched when a player submits a plain chat line. Turn `Chat.setDefaultRelay(false)` off to own delivery yourself.");
        events.add_property("playerCommand", "[player: Player, command: string, args: string[]]", "Dispatched when a player's chat line begins with `/` and no built-in command claimed it. A `/` line is never relayed to anyone else.");

        auto context   = isolate->GetCurrentContext();
        auto &metadata = Framework::Scripting::GetScriptingCatalog(isolate).global_object("Chat", "Server-side delivery of structured chat messages to connected clients.");

        // Attach onto the framework's own global Chat, which carries sendToAll and sendToPlayer, rather than shadowing it.
        v8::Local<v8::Object> chat;
        v8::Local<v8::Value> existing;
        if (global->Get(context, v8pp::to_v8(isolate, "Chat")).ToLocal(&existing) && existing->IsObject()) {
            chat = existing.As<v8::Object>();
        }
        else {
            chat = v8::Object::New(isolate);
            v8pp::publish(isolate, global, metadata, chat);
        }

        v8pp::bind_function(isolate, chat, metadata, "setDefaultRelay", &Chat::JS_SetDefaultRelay,
            v8pp::metadata::docs("void", {v8pp::metadata::param("enabled", "boolean", false, "True to relay chat automatically; false to answer playerChat yourself.")},
                "Enables or disables the built-in player-to-player chat relay."));
        v8pp::bind_function(isolate, chat, metadata, "isDefaultRelay", &Chat::JS_IsDefaultRelay,
            v8pp::metadata::docs("boolean", {}, "Checks whether the built-in chat relay is enabled.", "True when chat is relayed automatically."));
    }

    void Events::PlayerChat(std::uint64_t playerNetworkId, const std::string &text) {
        EmitReservedEvent("playerChat", [playerNetworkId, &text](v8::Isolate *isolate) {
            std::vector<v8::Local<v8::Value>> args;
            args.push_back(WrapPlayer(isolate, playerNetworkId));
            args.push_back(v8pp::to_v8(isolate, text));
            return args;
        });
    }

    void Events::PlayerCommand(std::uint64_t playerNetworkId, const std::string &command, const std::vector<std::string> &args) {
        EmitReservedEvent("playerCommand", [playerNetworkId, &command, &args](v8::Isolate *isolate) {
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            v8::Local<v8::Array> argv      = v8::Array::New(isolate, static_cast<int>(args.size()));
            for (std::size_t i = 0; i < args.size(); ++i) {
                argv->Set(context, static_cast<std::uint32_t>(i), v8pp::to_v8(isolate, args[i])).Check();
            }

            std::vector<v8::Local<v8::Value>> eventArgs;
            eventArgs.push_back(WrapPlayer(isolate, playerNetworkId));
            eventArgs.push_back(v8pp::to_v8(isolate, command));
            eventArgs.push_back(argv);
            return eventArgs;
        });
    }
} // namespace KCDC::Scripting
