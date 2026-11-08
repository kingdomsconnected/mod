#pragma once

#include <v8.h>

#include <cstdint>
#include <string>

namespace KCDC::Scripting {
    // The global `Buffs`: the game's status-effect table, and which parts of
    // it this server decides.
    //
    // Nothing is claimed by default, so buffs behave exactly as they do in
    // single player until a resource says otherwise. Claiming a class makes
    // every client turn down a locally originated buff in it, which is what
    // turns drinking, illness, injury and the wanted state into decisions the
    // server makes rather than ones each player makes for themselves.
    class Buffs {
      public:
        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);

      private:
        static void JS_Find(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_Register(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_Claim(const v8::FunctionCallbackInfo<v8::Value> &info);
    };

    // What this feature tells loaded resources the buff table did. Event arguments are network IDs; the handles a resource sees are created inside the scripting isolate.
    namespace Events {
        void PlayerCustomBuff(std::uint64_t playerNetworkId, const std::string &buff, const char *event, const std::string &reason = {});
        // A buff appeared on a player's own body. `fromServer` says whether it
        // was one this server ordered; anything else is native gameplay.
        void PlayerBuffAdded(std::uint64_t playerNetworkId, const std::string &buff, bool fromServer);

        // A buff left a player's body. `reason` is "server" for a removal this
        // server asked for and "expired" for one the owner's own clock ran out.
        void PlayerBuffRemoved(std::uint64_t playerNetworkId, const std::string &buff, const std::string &reason);

        // The claim gate turned a locally originated buff down on a player's
        // own client. The other half of a claim: without this the claimed
        // gameplay is deleted rather than moved here.
        void PlayerBuffBlocked(std::uint64_t playerNetworkId, const std::string &buff);
    } // namespace Events
} // namespace KCDC::Scripting
