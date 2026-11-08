#pragma once

#include "npc_navigation.h"
#include "shared/features/door/door_policy.h"

#include "features/navigation/navmesh.h"
#include "shared/features/npc/npc_types.h"
#include "shared/features/patrol/patrol_route.h"

#include <mafianet/get_time.h>
#include <mafianet/types.h>

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <function2/function2.hpp>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace KCDC::Features::Npc {
    /** What one tick sees of an NPC, gathered by `Npcs` so an order never reaches into replication itself. */
    struct NpcOrderContext {
        MafiaNet::Time now = 0;

        /** The body's position: the simulator's last report, or the server's own dead reckoning while dormant. */
        glm::vec3 position {0.0F};

        bool hasSimulator = false;

        /** Alive and not frozen: a body that can carry out a move at all. */
        bool active = true;

        bool meshReady = false;

        /** What the simulator last reported, straight off the replicated fields. */
        Shared::Game::NpcIntentStatus reportStatus = Shared::Game::NpcIntentStatus::Idle;
        std::uint32_t reportRevision               = 0;

        /** The revision the entity currently carries. */
        std::uint32_t intentRevision = 0;

        /** Where a follow's target stands, when it can be seen. */
        std::optional<glm::vec3> followTarget;

        /** Opens an unlocked route door; called only on approach, never during a mesh query. */
        fu2::function<Shared::Game::DoorPassage(std::uint64_t) const> doorPassage;
    };

    /** What a tick asks `Npcs` to do with the entity. */
    struct NpcOrderStep {
        /** Raise `npcIntentDone` with this; at most once per revision, and once per order when terminal. */
        std::optional<Shared::Game::NpcIntentStatus> announce;

        /** The simulator's intent changed: start a revision and force it. */
        bool republish = false;
    };

    /**
     * One NPC's standing order on the server: the goal a script gave it, the
     * patrol it belongs to, and what has become of it.
     *
     * Only the server decides an order's outcome, and it decides it here: the
     * simulator's replicated status pair is read as a measurement and never
     * written back. An outcome is accepted once and announced once; a
     * handover or a stale report cannot reopen it.
     *
     * Holds no entity and calls no service, so the whole state machine runs
     * in a test without a server.
     */
    class NpcOrder final {
      public:
        /** Replaces the order, cancelling any patrol. */
        void Command(const Shared::Game::NpcIntent &goal, Pathfinding mode, const NpcOrderContext &context);

        /** Starts walking `route`. The caller has validated it; see `IsUsableRoute`. */
        void Patrol(const std::vector<glm::vec3> &route, bool loop, Shared::Game::NpcSpeed speed, float waitSeconds, Pathfinding mode, const NpcOrderContext &context);
        void Patrol(const Shared::Game::PatrolRouteDefinition &route, Pathfinding mode, const NpcOrderContext &context);

        nlohmann::json PatrolState() const;
        std::vector<nlohmann::json> TakePatrolEvents();
        std::uint64_t Generation() const {
            return _generation;
        }

        /** Stops a patrol without changing what the body is doing this moment. */
        void ClearRoute(const char *outcome = "cancelled");

        /** Reads the simulator's report and any outcome the server reached on the previous tick. */
        NpcOrderStep Observe(const NpcOrderContext &context);

        /** Steps the patrol, leads a simulator through bends, and dead-reckons a dormant body. */
        NpcOrderStep Advance(const NpcOrderContext &context, glm::vec3 &position);

        /** Whether a mesh query is waiting for its turn in `NpcOrders`'s budget. */
        bool NeedsPlan() const {
            return _needsPlan;
        }

        void Plan(const Navigation::Navmesh &mesh, const Navigation::QueryOptions &options, const glm::vec3 &position);

        /** The intent the simulator should run now. A finished move holds where it stands. */
        Shared::Game::NpcIntent SimulatorIntent(const glm::vec3 &position) const;

        /** Records the revision that carries `SimulatorIntent`; only reports against it are read. */
        void Published(std::uint32_t revision) {
            _publishedRevision = revision;
        }

        /** The body was moved outright; a server route from where it was is replanned. */
        void OnTeleported();

        /** A new simulator re-derives from the intent; what it reports about a finished order is not news. */
        void OnSimulatorChanged(MafiaNet::Time now);

        /**
         * What scripts read as `npc.status`.
         *
         * `running` for as long as a move is in hand -- planning, walking
         * corners, or waiting on a report that has not caught up -- and the
         * outcome only once the server has accepted and announced it.
         */
        Shared::Game::NpcIntentStatus Status(const NpcOrderContext &context) const;

        const Shared::Game::NpcIntent &Goal() const {
            return _navigation.Goal();
        }

        const NpcNavigation &Navigation() const {
            return _navigation;
        }

        bool IsPatrolling() const {
            return !_route.points.empty();
        }

        bool IsFinished() const {
            return _finished;
        }

        std::uint32_t NextActionSequence() {
            return ++_actionSequence;
        }

        /** Whether a patrol is short enough, every waypoint inside the world, and its wait a real number. */
        static bool IsUsableRoute(const std::vector<glm::vec3> &route, Shared::Game::NpcSpeed speed, float waitSeconds);

        /**
         * How close a simulated body comes to a bend before it is handed the
         * next one. Inside this the body is already turning, and handing it
         * the next corner now keeps it walking instead of arriving, stopping
         * and waiting a round trip for the server to answer.
         */
        static constexpr float kCornerLeadDistance = 1.75F;

        /**
         * The most a lead may cut a bend by: how far the corner may lie from
         * the straight line the body would take from where it stands to the
         * corner after it.
         *
         * A route bends at a corner because something stands on its inside,
         * so every lead cuts towards it. Within the mesh's own margin the cut
         * is harmless; at a sharp bend or a doorway it walks the body into the
         * wall or the jamb, and there the corner is handed over when the body
         * reports arriving at it instead.
         */
        static constexpr float kMaxCornerCut = 0.1F;

        /** How far `corner` lies from the straight walk from `position` to `next`, on the ground. */
        static float CornerCut(const glm::vec3 &position, const glm::vec3 &corner, const glm::vec3 &next);

        /** The least a patrol waits after a leg it could not walk, so an unreachable waypoint is not retried every tick. */
        static constexpr MafiaNet::Time kFailedLegBackoffMs = 2000;

        static constexpr std::size_t kMaxPatrolPoints = 256;
        static constexpr float kMaxPatrolWaitSeconds  = 3600.0F;
        static constexpr float kPatrolArriveRadius    = 1.5F;

        /** The intent a patrol leg runs. */
        static Shared::Game::NpcIntent PatrolLeg(const glm::vec3 &waypoint, Shared::Game::NpcSpeed speed);

      private:
        /** Starts the navigation for a goal, and asks for a plan when the server routes it. True when it can be published now. */
        bool Begin(const Shared::Game::NpcIntent &goal, Pathfinding mode, const NpcOrderContext &context);
        void RequestPlan();
        void Finish(Shared::Game::NpcIntentStatus status, MafiaNet::Time now, NpcOrderStep &step);
        bool IssueLeg(const NpcOrderContext &context);
        bool AdvanceRoute(const NpcOrderContext &context);

        NpcNavigation _navigation;

        bool _needsPlan = false;
        bool _waitingForDoor            = false;
        MafiaNet::Time _doorWaitStarted = 0;

        /** The revision carrying the current step; 0 while one is being planned. */
        std::uint32_t _publishedRevision = 0;

        /** The last report read, so a repeat is not taken as news. */
        Shared::Game::NpcIntentStatus _seenStatus = Shared::Game::NpcIntentStatus::Idle;
        std::uint32_t _seenRevision               = 0;

        /** An outcome the server reached itself, announced on the next tick rather than the one that produced it. */
        std::optional<Shared::Game::NpcIntentStatus> _pendingOutcome;

        /** The accepted outcome. Latched: nothing reopens it but a new order or the next patrol leg. */
        bool _finished                         = false;
        Shared::Game::NpcIntentStatus _outcome = Shared::Game::NpcIntentStatus::Idle;

        /** Which revision a follow's arrival was announced for. */
        std::uint32_t _announcedRevision = 0;

        // The patrol, if it is walking one.
        Shared::Game::PatrolRouteDefinition _route;
        Shared::Game::PatrolCursor _cursor;
        std::vector<nlohmann::json> _patrolEvents;
        std::uint64_t _generation     = 0;
        Pathfinding _routePathfinding = Pathfinding::Auto;
        std::size_t _failedLegs       = 0;

        /** When a wait at a waypoint is over. Zero when it is not waiting. */
        MafiaNet::Time _resumeAt = 0;

        /** Dead-reckoning clock for a dormant NPC, in server time. */
        MafiaNet::Time _lastAdvance = 0;

        /** One-shot action sequence, so a receiver can tell a repeat from a new gesture. */
        std::uint32_t _actionSequence = 0;
    };

    /**
     * Every NPC's order, and the queue their mesh queries wait in.
     *
     * A route is an A* over the level's mesh. Run the moment it is asked
     * for, a script ordering a crowd or a dozen patrols stepping at once
     * would spend the tick on pathfinding, so routes queue here and are
     * planned first come, first served, a few a tick; an order waiting its
     * turn reads as `running`.
     */
    class NpcOrders final {
      public:
        static NpcOrders &Get();

        NpcOrder &For(std::uint64_t networkId) {
            return _orders[networkId];
        }

        NpcOrder *Find(std::uint64_t networkId);

        /** Copies the current IDs so event callbacks can add or erase orders during an update. */
        void CollectIds(std::vector<std::uint64_t> &ids) const;

        void Erase(std::uint64_t networkId);

        /** Queues the order's plan if it needs one and is not queued already. */
        void Track(std::uint64_t networkId);

        /**
         * Plans queued orders, oldest first. `plan` answers false for an NPC
         * that has gone. Only mesh queries count against `budget`: a straight
         * line costs nothing. Returns how many were planned.
         */
        std::size_t RunPlans(std::size_t budget, bool meshReady, const fu2::function<bool(std::uint64_t, NpcOrder &) const> &plan);

        std::size_t QueuedPlans() const {
            return _planQueue.size();
        }

        /** Mesh queries per tick, each bounded by `kSearchNodes`: a cap on the tick, not on the crowd. */
        static constexpr std::size_t kPlansPerTick = 4;

        /** The node budget of one NPC route query, against the mesh's own 65535. */
        static constexpr int kSearchNodes = 4096;

        /** Across the ground, how far a point may lie from the mesh and still be on it. */
        static constexpr float kSearchRadius = 2.0F;

      private:
        std::unordered_map<std::uint64_t, NpcOrder> _orders;
        std::deque<std::uint64_t> _planQueue;
        std::unordered_set<std::uint64_t> _queued;
    };
} // namespace KCDC::Features::Npc
