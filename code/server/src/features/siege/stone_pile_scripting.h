#pragma once

#include "shared/features/siege/stone_pile_entity.h"

#include <scripting/builtins/entity.h>

#include <glm/glm.hpp>

#include <v8.h>
#include <v8pp/class.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace KCDC::Scripting {
    // Server-side handle for a pile of hurling stones on a battlement. Wraps the pile's NetworkID and resolves the live entity through replication.
    //
    // Bindings only: KCDC::Features::Siege::StonePileService keeps the count and closes every throw.
    class StonePile final: public Framework::Scripting::Builtins::Entity {
      public:
        StonePile(std::uint64_t networkId): Framework::Scripting::Builtins::Entity(networkId) {}

        // The live pile, or nullptr once it has been destroyed.
        Shared::Entities::StonePileEntity *ResolvePile() const;

        std::int32_t GetStones() const;
        void SetStones(std::int32_t stones);

        bool IsUsable() const;
        void SetUsable(bool usable);

        float GetDamage() const;
        void SetDamage(float damage);
        float GetDamageRadius() const;
        void SetDamageRadius(float radius);

        std::string ToString() const override;

        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);

        static void UnregisterIsolate(v8::Isolate *isolate);
        static v8pp::class_<StonePile> &GetClass(v8::Isolate *isolate);

      private:
        inline static std::unordered_map<v8::Isolate *, std::unique_ptr<v8pp::class_<StonePile>>> _classes;
    };

    namespace Events {
        void StonePileSpawned(std::uint64_t pileNetworkId);

        // Raised while the pile is still resolvable.
        void StonePileDestroyed(std::uint64_t pileNetworkId);

        // A player took a stone off the pile and threw it.
        void StonePileThrow(std::uint64_t playerNetworkId, std::uint64_t pileNetworkId);

        // Where a thrown stone came down, as its thrower saw it. The pile may have been destroyed since.
        void StoneImpact(std::uint64_t pileNetworkId, std::uint64_t attackerNetworkId, const glm::vec3 &point);
    } // namespace Events
} // namespace KCDC::Scripting
