#pragma once

#include <unordered_map>

#include "features/siege/siege_common.h"

#include "shared/features/siege/siege_entity.h"

#include <scripting/builtins/entity.h>

#include <glm/glm.hpp>

#include <v8.h>
#include <v8pp/class.hpp>

#include <cstdint>
#include <memory>
#include <string>

namespace KCDC::Scripting {
    // Server-side handle for a siege engine -- a trebuchet or a cannon. Wraps the engine's NetworkID and resolves the live entity through replication, so a handle to a destroyed engine reads as gone rather than dangling. Transform and virtual world come from the framework's Entity base.
    //
    // Bindings only: every verb is one call into KCDC::Features::Siege::SiegeService, which owns the server clock every client plays the cycle out from and closes every shot.
    class SiegeEngine final: public Framework::Scripting::Builtins::Entity {
      public:
        SiegeEngine(std::uint64_t networkId): Framework::Scripting::Builtins::Entity(networkId) {}

        // The live engine, or nullptr once it has been destroyed.
        Shared::Entities::SiegeEngineEntity *ResolveEngine() const;

        // `trebuchet` or `cannon`.
        std::string GetKind() const;

        // `idle`, `drawing`, `loading`, `ready` or `firing`. Named for the cycle rather than the state, because the Entity base already has a `state` and it is the entity's state bag.
        std::string GetCycle() const;

        bool IsLoaded() const;

        float GetSpeed() const;
        void SetSpeed(float speed);

        float GetDamage() const;
        void SetDamage(float damage);
        float GetDamageRadius() const;
        void SetDamageRadius(float radius);

        bool IsUsable() const;
        void SetUsable(bool usable);

        bool HasAimGuide() const;
        void SetAimGuide(bool aimGuide);

        float GetRange() const;
        void SetRange(float range);

        float GetMinRange() const;
        float GetMaxRange() const;

        // How long a full load takes at the engine's speed, in seconds.
        float GetLoadDuration() const;

        float GetMaxHealth() const;
        void SetMaxHealth(float maxHealth);
        float GetHealth() const;
        void SetHealth(float health);
        bool IsWrecked() const;

        // Shots left to load, -1 for no limit.
        std::int32_t GetAmmo() const;
        void SetAmmo(std::int32_t ammo);

        std::uint32_t GetCrewRequired() const;
        void SetCrewRequired(std::uint32_t required);
        std::uint32_t GetCrewPlaces() const;

        // The player in the engine's place, or null.
        v8::Local<v8::Value> GetOperatorHandle(v8::Isolate *isolate) const;

        // The players at the crew places, in place order, with null for an empty one.
        v8::Local<v8::Value> GetCrewHandles(v8::Isolate *isolate) const;

        std::string ToString() const override;

        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);

        static void UnregisterIsolate(v8::Isolate *isolate);
        static v8pp::class_<SiegeEngine> &GetClass(v8::Isolate *isolate);

      private:
        inline static std::unordered_map<v8::Isolate *, std::unique_ptr<v8pp::class_<SiegeEngine>>> _classes;
    };

    // A `SiegeResult` for scripts: what a siege engine's or a ladder's verb did, and why not when it did nothing.
    v8::Local<v8::Object> SiegeResultObject(v8::Isolate *isolate, const KCDC::Features::Siege::SiegeResult &result);

    // What this feature tells loaded resources happened. Event arguments are network IDs; the handles a resource sees are created inside the scripting isolate.
    namespace Events {
        void SiegeEngineSpawned(std::uint64_t engineNetworkId);

        // Raised while the engine is still resolvable.
        void SiegeEngineDestroyed(std::uint64_t engineNetworkId);

        // The load cycle finished; the engine can fire.
        void SiegeEngineReady(std::uint64_t engineNetworkId);

        // The projectile has just been let go at `target`.
        void SiegeFired(std::uint64_t engineNetworkId, std::uint64_t attackerNetworkId, const glm::vec3 &target);

        // A player asked something of an engine: `operate`, `winch`, `leave`, `load` or `fire`. False when a handler refused it; `leave` cannot be refused.
        bool SiegeEngineUse(std::uint64_t playerNetworkId, std::uint64_t engineNetworkId, const char *action);

        // A harmable engine took `amount`; raised before it is wrecked by it.
        void SiegeEngineDamaged(std::uint64_t engineNetworkId, float amount, std::uint64_t attackerNetworkId);

        // An engine's health ran out.
        void SiegeEngineWrecked(std::uint64_t engineNetworkId, std::uint64_t attackerNetworkId);

        // Where a projectile came down. The engine may have been destroyed since it fired, in which case it reads as null.
        void SiegeImpact(std::uint64_t engineNetworkId, std::uint64_t attackerNetworkId, const glm::vec3 &point);
    } // namespace Events
} // namespace KCDC::Scripting
