#pragma once

#include "shared/features/siege/siege_ladder_entity.h"

#include <scripting/builtins/entity.h>

#include <v8.h>
#include <v8pp/class.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace KCDC::Scripting {
    // Server-side handle for a siege ladder leaning on a wall. Wraps the ladder's NetworkID and resolves the live entity through replication, so a handle to a destroyed ladder reads as gone rather than dangling.
    //
    // Bindings only: every verb is one call into KCDC::Features::Siege::SiegeLadderService, which owns the push and the raise on the server clock.
    class SiegeLadder final: public Framework::Scripting::Builtins::Entity {
      public:
        SiegeLadder(std::uint64_t networkId): Framework::Scripting::Builtins::Entity(networkId) {}

        // The live ladder, or nullptr once it has been destroyed.
        Shared::Entities::SiegeLadderEntity *ResolveLadder() const;

        // `siege`, `tall`, `short` or `low`.
        std::string GetKind() const;

        // `standing`, `pushed`, `down`, `raising` or `broken`. Named for the ladder's pose, because the Entity base already has a `state`.
        std::string GetPose() const;

        float GetHeight() const;
        bool IsClimbable() const;

        bool IsUsable() const;
        void SetUsable(bool usable);

        // The player who last pushed it, or null.
        v8::Local<v8::Value> GetPusherHandle(v8::Isolate *isolate) const;

        std::string ToString() const override;

        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);

        static void UnregisterIsolate(v8::Isolate *isolate);
        static v8pp::class_<SiegeLadder> &GetClass(v8::Isolate *isolate);

      private:
        inline static std::unordered_map<v8::Isolate *, std::unique_ptr<v8pp::class_<SiegeLadder>>> _classes;
    };

    namespace Events {
        void SiegeLadderSpawned(std::uint64_t ladderNetworkId);

        // Raised while the ladder is still resolvable.
        void SiegeLadderDestroyed(std::uint64_t ladderNetworkId);

        // A player asked to `push` or `raise` a ladder from its prompt. False when a handler refused it.
        bool SiegeLadderUse(std::uint64_t playerNetworkId, std::uint64_t ladderNetworkId, const char *action);

        // A pushed ladder hit the ground; `pusherNetworkId` is 0 for a script's push.
        void SiegeLadderFell(std::uint64_t ladderNetworkId, std::uint64_t pusherNetworkId);

        // A raised ladder leans on its wall again.
        void SiegeLadderRaised(std::uint64_t ladderNetworkId);
    } // namespace Events
} // namespace KCDC::Scripting
