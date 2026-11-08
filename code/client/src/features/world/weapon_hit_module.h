#pragma once

#include "core/feature_registry.h"

#include "sdk/types.h"
#include "sdk/wh/combatmodule/c_combat_actor_collisions.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

namespace KCDC::Features::World {
    /**
     * Where the local player's weapon meets something solid that is not a
     * body: a tree, a wall, a rock, a prop, the ground.
     *
     * Every swing sweeps the weapon's shape through the step it moved
     * (`C_CombatActorCollisions::SweepWeaponShape`, 0x180A7A124), for every
     * fighter, against the world and against bodies alike. The engine keeps
     * a set per attack of the objects and parts its weapon has touched, and
     * leaves a contact with one it had not in the actor's first-contact
     * record, so each thing a swing meets is reported once per attack. A
     * contact with a combat actor carries the victim and goes the way of
     * every hit; this takes the rest, for the local player's swings only.
     *
     * The sweep runs inside the animation module's pose update, which may be
     * a worker thread, so the hook only copies the contact out. `Update`
     * names what was hit on the game thread, the way a trace's hit is named,
     * raises `weaponHit` here and tells the server, which raises
     * `playerWeaponHit`.
     */
    class WeaponHitModule final: public KCDC::Core::IFeatureService {
      public:
        static WeaponHitModule &Get();

        /** Installs the sweep detour. Called once, at boot. */
        static void Install();

        /** Names, raises and reports the contacts the sweeps found since the last frame. Game thread. */
        void Update() override;

        /** Drops contacts from a session that has ended. */
        void Reset() override;

        /** After one sweep: keeps its first new contact if it is the local player's, with something that is not a body. Any thread. */
        void NoteSweep(const SDK::wh::combatmodule::C_CombatActorCollisions &collisions);

      private:
        struct Contact {
            SDK::Vec3 point {};
            SDK::Vec3 normal {};
            SDK::Vec3 direction {};
            std::int32_t surfaceIndex     = 0;
            std::int32_t physicalEntityId = 0;
        };

        // A swing touches a handful of things at most; past this the frame has stalled, and the rest are dropped.
        static constexpr std::size_t kMaxQueuedContacts = 32;

        std::mutex _contactsMutex;
        std::vector<Contact> _contacts;

        // Read by the sweep hook, which is not on the game thread; refreshed on it every frame.
        std::atomic<SDK::EntityId> _localEntityId {0};
    };
} // namespace KCDC::Features::World
