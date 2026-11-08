#pragma once

#include <v8.h>

#include <cstdint>
#include <string>
#include <vector>

namespace KCDC::Scripting {
    // Adds setDefaultRelay(bool) / isDefaultRelay() to the framework's global Chat.
    class Chat {
      public:
        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);

      private:
        static void JS_SetDefaultRelay(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_IsDefaultRelay(const v8::FunctionCallbackInfo<v8::Value> &info);
    };

    // What this feature tells loaded resources a player said. Event arguments are network IDs; the handles a resource sees are created inside the scripting isolate.
    namespace Events {
        // A plain chat line, raised after the default relay has had it -- turning the relay off is what makes a resource the only delivery.
        void PlayerChat(std::uint64_t playerNetworkId, const std::string &text);

        // A '/' line no built-in command claimed. Never relayed to anyone else, so a resource that ignores it answers nobody.
        void PlayerCommand(std::uint64_t playerNetworkId, const std::string &command, const std::vector<std::string> &args);
    } // namespace Events
} // namespace KCDC::Scripting
