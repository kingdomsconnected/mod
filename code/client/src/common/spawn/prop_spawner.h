#pragma once

#include "sdk/types.h"
#include "actor_spawner.h"

#include "sdk/cryengine/entitysystem/i_entity.h"

#include <function2/function2.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace KCDC::Common::Spawn {
    /** The three outcomes `EntityCommon.PhysicalizeRigid` can reach. */
    enum class PropPhysics : std::uint8_t {
        Rigid,  // PE_RIGID: falls, collides, can be pushed around
        Static, // PE_STATIC: collides, never moves
        None,   // no physics proxy at all -- geometry only
    };

    inline const char *PropPhysicsToString(PropPhysics physics) {
        switch (physics) {
        case PropPhysics::Rigid: return "rigid";
        case PropPhysics::Static: return "static";
        case PropPhysics::None: return "none";
        }
        return "unknown";
    }

    /** The physics `PropPhysicsToString` spells as `name`, or nothing for any other word. */
    inline std::optional<PropPhysics> PropPhysicsFromString(std::string_view name) {
        for (const PropPhysics physics : {PropPhysics::Rigid, PropPhysics::Static, PropPhysics::None}) {
            if (name == PropPhysicsToString(physics)) {
                return physics;
            }
        }
        return std::nullopt;
    }

    /** What QueueSpawn takes, bundled for the game-thread entry point. */
    struct PropSpawnRequest {
        std::string model; // `objects/....cgf`, a path from the prop catalog
        PropPhysics physics = PropPhysics::Rigid;
        float scale         = 1.0F;

        // A rigid body's mass in kilograms, set before it is physicalized; 0 leaves it to the mesh's collision.
        float mass = 0.0F;

        // Player-relative placement, used when `world` is unset.
        PlacementParams placement {};

        // Takes precedence over `placement`, and needs no local player.
        std::optional<WorldPlacement> world;
    };

    /** A live prop owned by this mod. */
    struct SpawnedProp {
        SDK::EntityId entityId = 0;
        std::string model;
        std::string name;
        PropPhysics physics = PropPhysics::Rigid;
        float scale         = 1.0F;
    };

    /** Stage the last spawn attempt reached, surfaced by the debug panel. */
    enum class PropSpawnResult : std::uint8_t {
        None,
        Succeeded,
        NoEntitySystem,
        NoLocalPlayer,
        NoModel,
        ClassNotRegistered,
        SpawnRefused,
        InitFailed,
        InvalidTransform,
        InvalidPhysics,
        ModelLoadFailed,
        NoScriptTable,
        PhysicsUnavailable,
    };

    const char *PropSpawnResultToString(PropSpawnResult result);

    /** Whether the spawn failed because the world was not ready, rather than because the request was wrong. */
    inline bool IsRetryablePropSpawnResult(PropSpawnResult result) {
        return result == PropSpawnResult::NoEntitySystem || result == PropSpawnResult::NoLocalPlayer || result == PropSpawnResult::ClassNotRegistered || result == PropSpawnResult::SpawnRefused || result == PropSpawnResult::ModelLoadFailed;
    }

    /** Result of an immediate spawn; `prop.entityId` is 0 when it failed. */
    struct PropSpawnOutcome {
        SpawnedProp prop;
        PropSpawnResult result = PropSpawnResult::None;
    };

    class PropSpawner;

    /**
     * A prop that has been asked for and has not necessarily been made yet.
     *
     * The actor spawner's `SpawnTicket` with a prop's types; see that one for
     * why replicated bodies are asked for rather than spawned inline. A prop
     * costs a `.cgf` load and a physicalization, so a server handing a joining
     * client a few hundred of them burns the same single frame.
     *
     * Game thread only. The ticket lives until its callback has run or it is
     * returned, whichever comes first.
     */
    class PropSpawnTicket final {
        friend class PropSpawner;

      public:
        /**
         * Called once, on the frame the request ran. `true` means the prop
         * exists and `GetEntityId` names it; `false` means the attempt failed
         * and `GetResult` says how.
         */
        using FinishCallback = fu2::function<void(PropSpawnTicket &, bool) const>;

        SDK::EntityId GetEntityId() const {
            return _entityId;
        }

        /** `None` until the request has run. */
        PropSpawnResult GetResult() const {
            return _result;
        }

      private:
        PropSpawnRequest _request;
        FinishCallback _onFinished;
        SDK::EntityId _entityId = 0;
        PropSpawnResult _result = PropSpawnResult::None;

        // See SpawnTicket::_finished.
        bool _finished = false;
    };

    /**
     * BasicEntity props with model/physics properties set before OnSpawn.
     * Update drains queued commands on the game thread. Stored handles are
     * EntityIds; resolve IEntity pointers on each use.
     */
    class PropSpawner final {
      public:
        // Callable from any thread.

        void QueueSpawn(const PropSpawnRequest &request);
        void QueueDespawn(SDK::EntityId entityId);
        void QueueDespawnAll();

        // Game thread only.

        /**
         * Returns a caller-owned entity outside the debug list. The caller
         * must despawn it; the returned entity ID is zero on failure.
         */
        PropSpawnOutcome SpawnNow(const PropSpawnRequest &request);

        /**
         * Asks for a prop and hands back a ticket for it straight away. The
         * spawner executes the request on a later frame, within the per-frame
         * budget, and calls `onFinished` once when it has.
         *
         * `Return` the ticket before the caller dies: `onFinished` holds it.
         */
        PropSpawnTicket *Request(const PropSpawnRequest &request, PropSpawnTicket::FinishCallback onFinished);

        /**
         * Drops a ticket. A request that has not run yet never will; one that
         * already produced a prop leaves it alone, because by then it belongs
         * to whatever asked for it. Safe with a null ticket.
         */
        void Return(PropSpawnTicket *ticket);

        /** Queues an entity spawned by SpawnNow for removal. */
        void DespawnNow(SDK::EntityId entityId);

        // Game thread only; driven from CCryAction::PostUpdate.
        void Update();

        /**
         * Gives an entity whose slot 0 holds a compound mesh the collider
         * parts the slot path refused it. `CPhysicalProxy::AddSlotGeometry`
         * rejects a compound root whose own physics list is empty, so a
         * multi-node mesh -- the siege ladder, many props -- physicalizes with
         * no part at all; the mesh's own Physicalize handles its children, as
         * the brush path does. True when the entity has parts afterwards.
         * Game thread only.
         */
        static bool EnsureCompoundPhysics(SDK::IEntity *entity, float scale);

        std::vector<SpawnedProp> GetSpawned() const;
        PropSpawnResult GetLastResult() const;
        SDK::EntityId GetLastSpawnedId() const;

      private:
        enum class CommandKind : std::uint8_t {
            Spawn,
            Despawn,
            DespawnAll,
        };

        struct Command {
            CommandKind kind = CommandKind::Spawn;
            PropSpawnRequest request;
            SDK::EntityId entityId = 0;
        };

        void ExecuteSpawn(const PropSpawnRequest &request);
        void ExecuteDespawn(SDK::EntityId entityId);
        void ExecuteDespawnAll();
        void PruneDeadHandles();

        static bool ApplyPropProperties(SDK::IEntity *entity, const PropSpawnRequest &request);

        /**
         * Adds compound child geometry when the entity slot path skipped it,
         * then verifies that a prop requesting physics has collision parts.
         */
        static bool EnsurePhysics(SDK::IEntity *entity, const PropSpawnRequest &request);

        void SetResult(PropSpawnResult result, SDK::EntityId entityId);

        /** Executes one ticket and fires its callback. Game thread only. */
        void ExecuteTicket(PropSpawnTicket &ticket);

        mutable std::mutex _mutex;
        std::vector<Command> _pending;

        // Game thread only, and deliberately not under `_mutex`: executing a
        // ticket calls back into the replica that asked for it, which reaches
        // straight back here to `Return` it.
        std::vector<std::unique_ptr<PropSpawnTicket>> _tickets;
        std::vector<SpawnedProp> _spawned;
        PropSpawnResult _lastResult  = PropSpawnResult::None;
        SDK::EntityId _lastSpawnedId = 0;
        std::uint32_t _nameCounter   = 0;
    };
} // namespace KCDC::Common::Spawn
