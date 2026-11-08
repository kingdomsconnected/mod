#pragma once

#include <unordered_map>

#include "shared/features/horse/horse_entity.h"
#include "shared/features/player/player_damage.h"
#include "shared/game/horse_gear_catalog.h"

#include <scripting/builtins/entity.h>

#include <v8.h>
#include <v8pp/class.hpp>

#include <cstdint>
#include <memory>
#include <string>

namespace KCDC::Scripting {
    // Server-side handle for a replicated horse. Wraps the horse's NetworkID and resolves the live entity through replication, so a handle to a destroyed horse reads as gone rather than dangling. Transform and virtual world come from the framework's Entity base; what is added here is the horse's identity and who is riding it.
    //
    // Bindings only: every verb is one call into KCDC::Features::Horse::Horses, which the /horse command and the mount RPC handler also use, so a horse spawned from a script and one spawned from chat are seeded the same way and raise the same event.
    class Horse final: public Framework::Scripting::Builtins::Entity {
      public:
        Horse(std::uint64_t networkId): Framework::Scripting::Builtins::Entity(networkId) {}

        // The live horse, or nullptr once it has been despawned.
        Shared::Entities::HorseEntity *ResolveHorse() const;

        // The soul GUID the body was spawned against.
        std::string GetSoul() const;
        std::string GetNavigationStatus() const;

        // The name every client shows, or empty when nobody has renamed it.
        std::string GetName() const;

        // Renames the horse on every client. Empty hands it back to its soul's own name.
        void SetName(const std::string &name);

        // What this horse can carry, or 0 before any client has reported it. Engine-derived, so read-only.
        float GetInventoryCapacity() const;

        // NetworkID of the rider's body, 0 when the saddle is empty.
        std::uint64_t GetRiderId() const;

        // The rider as a handle, or JS null when the saddle is empty.
        v8::Local<v8::Value> GetRiderHandle(v8::Isolate *isolate) const;

        bool IsMounted() const {
            return GetRiderId() != 0;
        }

        // NetworkID of the player this horse belongs to, 0 when it is nobody's.
        std::uint64_t GetOwnerId() const;

        // The owner as a handle, or JS null when the horse is nobody's.
        v8::Local<v8::Value> GetOwnerHandle(v8::Isolate *isolate) const;

        // Whether the horse is despawned when its owner disconnects, rather than left ownerless.
        bool GetDestroyWithOwner() const;
        void SetDestroyWithOwner(bool destroy);

        // Life, as the horse's simulator last reported it. Health and stamina are 0 before any client has run the horse.
        float GetHealth() const;
        void SetHealth(float value);
        float GetMaxHealth() const;
        float GetStamina() const;
        float GetMaxStamina() const;
        bool IsAlive() const;
        bool IsUnconscious() const;

        // What the horse wears, as a JS object keyed by slot.
        v8::Local<v8::Value> GetGearHandle(v8::Isolate *isolate) const;

        // The server's buffs on the horse, as a JS array of names.
        v8::Local<v8::Value> GetBuffsHandle(v8::Isolate *isolate) const;

        // Whether players' own gear changes are refused.
        bool IsGearLocked() const;
        void SetGearLocked(bool locked);

        std::string ToString() const override;

        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);

        static void UnregisterIsolate(v8::Isolate *isolate);
        static v8pp::class_<Horse> &GetClass(v8::Isolate *isolate);

      private:
        inline static std::unordered_map<v8::Isolate *, std::unique_ptr<v8pp::class_<Horse>>> _classes;
    };

    // What this feature tells loaded resources happened to a horse. Event arguments are network IDs; the handles a resource sees are created inside the scripting isolate.
    namespace Events {
        void HorseSpawned(std::uint64_t horseNetworkId);
        void HorseIntentFinished(std::uint64_t horseNetworkId, const char *status);
        void HorseDestroyed(std::uint64_t horseNetworkId);
        void HorseMounted(std::uint64_t horseNetworkId, std::uint64_t playerNetworkId);
        void HorseDismounted(std::uint64_t horseNetworkId, std::uint64_t playerNetworkId);

        // The horse was handed to another player, or left ownerless when `playerNetworkId` is 0.
        void HorseOwnerChanged(std::uint64_t horseNetworkId, std::uint64_t playerNetworkId);

        // Asked before a player's climb into a saddle is accepted. False means a handler refused it, and the player is sent back off.
        bool HorseMounting(std::uint64_t horseNetworkId, std::uint64_t playerNetworkId);

        // Health came off the horse; `attackerNetworkId` is 0 for nobody.
        void HorseDamaged(std::uint64_t horseNetworkId, std::uint64_t attackerNetworkId, float amount, Shared::Game::DamageReason reason);

        // The horse died; `killerNetworkId` is 0 for nobody.
        void HorseDied(std::uint64_t horseNetworkId, std::uint64_t killerNetworkId, Shared::Game::DamageReason reason);

        // Asked before a player's change to a horse's gear is accepted, with the gear it would have. False means a handler refused it, and the copy that changed is dressed back.
        bool HorseGearChanging(std::uint64_t horseNetworkId, std::uint64_t playerNetworkId, const Shared::Game::HorseGear &gear);

        // The gear on the server's record changed; `playerNetworkId` is who changed it, 0 for a script or the soul's own preset.
        void HorseGearChanged(std::uint64_t horseNetworkId, std::uint64_t playerNetworkId);
    } // namespace Events
} // namespace KCDC::Scripting
