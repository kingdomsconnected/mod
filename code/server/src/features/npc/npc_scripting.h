#pragma once

#include <unordered_map>

#include "shared/features/npc/npc_entity.h"

#include <scripting/builtins/entity.h>

#include <v8.h>
#include <v8pp/class.hpp>

#include <cstdint>
#include <memory>
#include <string>

namespace KCDC::Scripting {
    /**
     * Server-side handle for a server-owned NPC. Wraps the NetworkID and
     * resolves the live entity through replication, so a handle to a despawned
     * NPC reads as gone rather than dangling.
     *
     * Bindings only: every verb is one call into `KCDC::Features::Npc::Npcs`,
     * which the chat commands and the interaction RPC also go through -- so an
     * NPC spawned from a script and one spawned from a command are seeded the
     * same way and raise the same events.
     */
    class Npc final: public Framework::Scripting::Builtins::Entity {
      public:
        Npc(std::uint64_t networkId): Framework::Scripting::Builtins::Entity(networkId) {}

        /** The live NPC, or nullptr once it has been despawned. */
        Shared::Entities::NpcEntity *ResolveNpc() const;

        std::string GetSoul() const;
        std::string GetActorClass() const;
        double GetLevelGuid() const;

        std::string GetName() const;
        void SetName(const std::string &name);

        double GetFaction() const;
        void SetFaction(double faction);

        float GetHealth() const;
        void SetHealth(float health);
        float GetMaxHealth() const;
        bool GetAlive() const;

        bool GetInvulnerable() const;
        void SetInvulnerable(bool value);
        bool GetFrozen() const;
        void SetFrozen(bool value);
        bool GetInteractable() const;
        void SetInteractable(bool value);
        bool GetShowNametag() const;
        void SetShowNametag(bool value);

        /** The tag's parts, colour and text, mirroring `Player`'s `setNametag*` family. */
        bool IsNametagHealthVisible() const;
        void SetNametagHealthVisible(bool visible);
        std::string GetNametagText() const;
        void SetNametagText(const std::string &text);
        std::uint32_t GetNametagColor() const;
        void SetNametagColor(std::uint32_t color);

        bool GetLootable() const;
        void SetLootable(bool value);

        /** `kinematic` or `native`; see the property's own documentation. */
        std::string GetLocomotion() const;
        void SetLocomotion(const std::string &mode);

        /** What the NPC has been told to do: `hold`, `moveTo`, `follow`, ... */
        std::string GetIntent() const;

        /** What has become of the order, as the server has accepted it: `idle`, `running`, `reached`, `blocked`, `failed`. */
        std::string GetStatus() const;
        v8::Local<v8::Value> GetPatrolState(v8::Isolate *isolate) const;

        /** The appearance as a plain object, for the read-only `appearance` property. */
        v8::Local<v8::Value> GetAppearanceHandle(v8::Isolate *isolate) const;
        v8::Local<v8::Value> GetWearingHandle(v8::Isolate *isolate) const;

        /** The player running this NPC as a `Player` handle, or JS null while it is dormant. */
        v8::Local<v8::Value> GetSimulatorHandle(v8::Isolate *isolate) const;

        std::string ToString() const override;

        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);

        static void UnregisterIsolate(v8::Isolate *isolate);
        static v8pp::class_<Npc> &GetClass(v8::Isolate *isolate);

      private:
        inline static std::unordered_map<v8::Isolate *, std::unique_ptr<v8pp::class_<Npc>>> _classes;
    };

    // Event arguments are network IDs; the handles a resource sees are created inside the scripting isolate.
    namespace Events {
        void NpcSpawned(std::uint64_t npcNetworkId);

        /** Raised while the NPC is still resolvable, so a handler can read what it was. */
        void NpcDestroyed(std::uint64_t npcNetworkId);

        /** A move, a look or an animation ended: `reached`, `blocked` or `failed`. */
        void NpcIntentFinished(std::uint64_t npcNetworkId, const char *status);

        void NpcDamaged(std::uint64_t npcNetworkId, std::uint64_t attackerNetworkId, float amount);
        void NpcDied(std::uint64_t npcNetworkId, std::uint64_t attackerNetworkId);
        void NpcRevived(std::uint64_t npcNetworkId);

        /** A player pressed use on an interactable NPC, close enough for the server to agree. */
        void NpcInteracted(std::uint64_t npcNetworkId, std::uint64_t playerNetworkId);

        /** The animation a `playAnimation` request made the NPC play has ended, once per request. */
        void NpcAnimationEnded(std::uint64_t npcNetworkId, const std::string &fragment, Shared::Game::AnimationEndReason reason);

        /** The client running this NPC changed; `playerNetworkId` is 0 when it went dormant. */
        void NpcSimulatorChanged(std::uint64_t npcNetworkId, std::uint64_t playerNetworkId);
    } // namespace Events
} // namespace KCDC::Scripting
