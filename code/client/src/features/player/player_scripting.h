#pragma once

#include "features/player/player.h"

#include "shared/features/player/player_entity.h"

#include <scripting/builtins/player.h>

#include <v8.h>
#include <v8pp/class.hpp>

#include <cstdint>
#include <memory>
#include <string>

namespace KCDC::Scripting {
    // Client-side handle for a player this client can see. The same readonly view of the body the server's handle exposes -- it comes from the same shared/scripting/human_bindings.h -- read off the replica this client holds rather than the one the server does. What is added is what only a client knows: whether this is the local player, and which engine entity the body was spawned as.
    //
    // Read-only throughout. The local player is authoritative for its own body, so a client-side setter would work -- which is the problem: it would put the decision of what a player may do to themselves inside a resource that player is running. Every verb lives on the server's Player, where the server can arbitrate it.
    class Player: public Framework::Scripting::Builtins::Player {
      public:
        Player(std::uint64_t networkId): Framework::Scripting::Builtins::Player(networkId) {}
        ~Player() override = default;

        // The live body, or nullptr once the replica is gone.
        Shared::Entities::HumanEntity *ResolveHuman() const;

        // The replica with this client's own view of the body, or nullptr.
        KCDC::Features::Player::Human *ResolveReplica() const;

        // Whether this is the player sitting at this machine.
        bool IsLocal() const;

        // The engine entity the body was spawned as, or 0 across a level load and before a copy exists. Not stable: entity ids are reused.
        std::uint32_t GetEntityId() const;

        // NetworkID of the horse this player is riding, or 0 when on foot.
        std::uint64_t GetHorseId() const;

        bool IsMounted() const {
            return GetHorseId() != 0;
        }

        std::string ToString() const override;

        static v8pp::class_<Player> &GetClass(v8::Isolate *isolate);

        // The local body's stateful status effects. Empty for anyone else: only the owner of a body publishes what it wears.
        v8::Local<v8::Value> GetBuffStates(v8::Isolate *isolate) const;

        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);

      protected:
        static std::unique_ptr<v8pp::class_<Player>> _class;
    };

    // The player sitting at this machine. Published as the `LocalPlayer` global rather than a constructor, and as an accessor rather than a value: the local body does not exist until the session has one, and a resource that started before that would otherwise hold a handle to nothing forever. It reads null until then, and again once the session ends.
    class LocalPlayer final: public Player {
      public:
        LocalPlayer(std::uint64_t networkId): Player(networkId) {}

        static v8pp::class_<LocalPlayer> &GetClass(v8::Isolate *isolate);
        static void RegisterGlobal(v8::Isolate *isolate, v8::Local<v8::Object> global);

        // A handle to the local body, or JS null while there is none.
        static v8::Local<v8::Value> Resolve(v8::Isolate *isolate);

      private:
        static void JS_Get(const v8::FunctionCallbackInfo<v8::Value> &info);

        static std::unique_ptr<v8pp::class_<LocalPlayer>> _class;
    };

    // What this feature tells resources loaded on this machine.
    namespace Events {
        // The local player can play: the session is established and the game has taken its loading screen down. Once per session.
        void LocalPlayerReady();
    } // namespace Events
} // namespace KCDC::Scripting
