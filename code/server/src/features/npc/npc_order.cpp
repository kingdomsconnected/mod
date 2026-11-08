#include "npc_order.h"

#include <algorithm>
#include <cmath>

namespace KCDC::Features::Npc {
    namespace {
        /** The ground-plane distance between two points; KCD2 is Z-up. */
        float GroundDistance(const glm::vec3 &a, const glm::vec3 &b) {
            return glm::distance(glm::vec2(a), glm::vec2(b));
        }

        MafiaNet::Time Milliseconds(float seconds) {
            return static_cast<MafiaNet::Time>(seconds * 1000.0F);
        }
    } // namespace

    Shared::Game::NpcIntent NpcOrder::PatrolLeg(const glm::vec3 &waypoint, Shared::Game::NpcSpeed speed) {
        Shared::Game::NpcIntent leg;
        leg.kind   = Shared::Game::NpcIntentKind::MoveTo;
        leg.point  = waypoint;
        leg.speed  = speed;
        leg.radius = kPatrolArriveRadius;
        return leg;
    }

    float NpcOrder::CornerCut(const glm::vec3 &position, const glm::vec3 &corner, const glm::vec3 &next) {
        const glm::vec2 from(position);
        const glm::vec2 to(next);
        const glm::vec2 point(corner);
        const glm::vec2 span   = to - from;
        const float lengthSq   = glm::dot(span, span);
        const float along      = lengthSq > 1e-6F ? std::clamp(glm::dot(point - from, span) / lengthSq, 0.0F, 1.0F) : 0.0F;
        return glm::distance(point, from + span * along);
    }

    bool NpcOrder::IsUsableRoute(const std::vector<glm::vec3> &route, Shared::Game::NpcSpeed speed, float waitSeconds) {
        if (route.empty() || route.size() > kMaxPatrolPoints || !std::isfinite(waitSeconds)) {
            return false;
        }
        return std::all_of(route.begin(), route.end(), [speed](const glm::vec3 &waypoint) {
            return PatrolLeg(waypoint, speed).IsSane();
        });
    }

    void NpcOrder::Command(const Shared::Game::NpcIntent &goal, Pathfinding mode, const NpcOrderContext &context) {
        ++_generation;
        ClearRoute();
        Begin(goal, mode, context);
    }

    void NpcOrder::Patrol(const std::vector<glm::vec3> &route, bool loop, Shared::Game::NpcSpeed speed, float waitSeconds, Pathfinding mode, const NpcOrderContext &context) {
        Shared::Game::PatrolRouteDefinition definition;
        definition.mode        = loop ? Shared::Game::PatrolMode::Loop : Shared::Game::PatrolMode::Once;
        definition.speed       = speed;
        definition.waitSeconds = std::clamp(waitSeconds, 0.0F, kMaxPatrolWaitSeconds);
        for (const glm::vec3 &position : route) {
            definition.points.push_back({position});
        }
        Patrol(definition, mode, context);
    }

    void NpcOrder::Patrol(const Shared::Game::PatrolRouteDefinition &route, Pathfinding mode, const NpcOrderContext &context) {
        ++_generation;
        ClearRoute();
        _route            = route;
        _cursor           = {};
        _routePathfinding = mode;
        _failedLegs       = 0;
        _resumeAt         = 0;
        IssueLeg(context);
    }

    void NpcOrder::ClearRoute(const char *outcome) {
        if (!_route.points.empty()) {
            auto event        = PatrolState();
            event["finished"] = true;
            event["status"]   = outcome;
            _patrolEvents.push_back(std::move(event));
        }
        _route.points.clear();
        _cursor     = {};
        _resumeAt   = 0;
        _failedLegs = 0;
    }

    bool NpcOrder::Begin(const Shared::Game::NpcIntent &goal, Pathfinding mode, const NpcOrderContext &context) {
        _navigation.Start(goal, mode);
        _waitingForDoor    = false;
        _doorWaitStarted   = 0;
        _needsPlan         = false;
        _publishedRevision = 0;
        _seenStatus        = Shared::Game::NpcIntentStatus::Idle;
        _seenRevision      = 0;
        _finished          = false;
        _outcome           = Shared::Game::NpcIntentStatus::Idle;
        _announcedRevision = 0;
        _lastAdvance       = context.now;
        _pendingOutcome.reset();
        if (_navigation.UsesServer(context.hasSimulator, context.meshReady)) {
            RequestPlan();
        }
        return !_needsPlan;
    }

    void NpcOrder::RequestPlan() {
        _navigation.Invalidate();
        _waitingForDoor  = false;
        _doorWaitStarted = 0;
        _needsPlan = true;
        // The step on the wire is the one being replaced; what the simulator
        // says about it from here on is not about the route being planned.
        _publishedRevision = 0;
    }

    bool NpcOrder::IssueLeg(const NpcOrderContext &context) {
        if (_cursor.index >= _route.points.size()) {
            return false;
        }
        const auto &point = _route.points[_cursor.index];
        auto leg          = PatrolLeg(point.position, point.speed.value_or(_route.speed));
        leg.radius        = point.radius.value_or(_route.radius);
        return Begin(leg, _routePathfinding, context);
    }

    bool NpcOrder::AdvanceRoute(const NpcOrderContext &context) {
        if (_route.points.empty()) {
            return false;
        }
        if (!_cursor.Advance(_route.mode, _route.points.size())) {
            ClearRoute(_outcome == Shared::Game::NpcIntentStatus::Reached ? "completed" : "failed");
            return false;
        }
        return IssueLeg(context);
    }

    void NpcOrder::Plan(const Navigation::Navmesh &mesh, const Navigation::QueryOptions &options, const glm::vec3 &position) {
        _navigation.Plan(mesh, options, position);
        _needsPlan = false;
        // A route with nothing to walk is an outcome, but not one for this
        // tick: it is announced on the next, so a plan never resolves in the
        // tick that made it.
        if (!_navigation.HasCorner()) {
            _pendingOutcome = _navigation.Status();
        }
    }

    void NpcOrder::Finish(Shared::Game::NpcIntentStatus status, MafiaNet::Time now, NpcOrderStep &step) {
        _finished     = true;
        _outcome      = status;
        step.announce = status;
        if (_route.points.empty()) {
            return;
        }

        auto event        = PatrolState();
        event["finished"] = false;
        event["status"]   = status == Shared::Game::NpcIntentStatus::Reached ? "reached" : status == Shared::Game::NpcIntentStatus::Blocked ? "blocked" : "failed";
        _patrolEvents.push_back(std::move(event));
        const float waitSeconds = _route.points[_cursor.index].waitSeconds.value_or(_route.waitSeconds);

        // A patrol steps on arrival, and gives up on a waypoint it cannot
        // reach rather than grinding at it -- but waits a while first, and
        // gives up on the patrol when a whole lap has gone by without one leg
        // arriving: nothing on it is reachable, and stepping on would only
        // ask the mesh the same questions again.
        if (status == Shared::Game::NpcIntentStatus::Reached) {
            _failedLegs = 0;
            _resumeAt   = now + Milliseconds(waitSeconds);
            return;
        }
        if (++_failedLegs >= _route.points.size()) {
            _outcome      = Shared::Game::NpcIntentStatus::Failed;
            step.announce = Shared::Game::NpcIntentStatus::Failed;
            _patrolEvents.back()["status"] = "failed";
            ClearRoute("failed");
            return;
        }
        _resumeAt = now + std::max(Milliseconds(waitSeconds), kFailedLegBackoffMs);
    }

    nlohmann::json NpcOrder::PatrolState() const {
        if (_route.points.empty()) {
            return nullptr;
        }
        return {{"routeId", _route.id.empty() ? nlohmann::json(nullptr) : nlohmann::json(_route.id)}, {"waypointIndex", _cursor.index}, {"lap", _cursor.lap}, {"direction", _cursor.direction}, {"phase", _finished ? "waiting" : "moving"}};
    }

    std::vector<nlohmann::json> NpcOrder::TakePatrolEvents() {
        std::vector<nlohmann::json> events;
        events.swap(_patrolEvents);
        return events;
    }

    NpcOrderStep NpcOrder::Observe(const NpcOrderContext &context) {
        NpcOrderStep step;
        if (_finished) {
            return step;
        }
        if (_navigation.Goal().kind == Shared::Game::NpcIntentKind::Attack && !context.followTarget) {
            Finish(Shared::Game::NpcIntentStatus::Failed, context.now, step);
            step.republish = true;
            return step;
        }
        if (_pendingOutcome) {
            const Shared::Game::NpcIntentStatus outcome = *_pendingOutcome;
            _pendingOutcome.reset();
            Finish(outcome, context.now, step);
            return step;
        }

        if (_waitingForDoor) {
            return step;
        }

        // Only a report about the step currently on the wire is read: a report
        // carrying an older revision answers an order that has been replaced.
        if (_needsPlan || _publishedRevision == 0 || context.intentRevision != _publishedRevision || context.reportRevision != _publishedRevision) {
            return step;
        }
        if (context.reportStatus == _seenStatus && context.reportRevision == _seenRevision) {
            return step;
        }
        _seenStatus   = context.reportStatus;
        _seenRevision = context.reportRevision;

        const Shared::Game::NpcIntent goal = _navigation.Goal();
        switch (context.reportStatus) {
        case Shared::Game::NpcIntentStatus::Reached: {
            if (_navigation.IsPlanned()) {
                _navigation.AdvanceCorner();
                if (_navigation.HasCorner()) {
                    step.republish = true;
                    return step;
                }
                const Shared::Game::NpcIntentStatus outcome = _navigation.Status();
                if (outcome == Shared::Game::NpcIntentStatus::Reached && (goal.kind == Shared::Game::NpcIntentKind::Follow || goal.kind == Shared::Game::NpcIntentKind::Attack)) {
                    // The recovery route used the target's position when it
                    // blocked. Resume following the target as it moves.
                    _navigation.Start(goal, Pathfinding::Auto);
                    step.republish = true;
                    return step;
                }
                Finish(outcome, context.now, step);
                return step;
            }
            if (goal.kind == Shared::Game::NpcIntentKind::Follow || goal.kind == Shared::Game::NpcIntentKind::Attack) {
                // A follow never finishes; catching up is news once per
                // revision, and the order keeps running.
                if (_announcedRevision != context.reportRevision) {
                    _announcedRevision = context.reportRevision;
                    step.announce      = Shared::Game::NpcIntentStatus::Reached;
                }
                return step;
            }
            Finish(Shared::Game::NpcIntentStatus::Reached, context.now, step);
            return step;
        }

        case Shared::Game::NpcIntentStatus::Blocked: {
            if (!_navigation.IsPlanned() && context.hasSimulator && context.active && goal.IsMotion()) {
                std::optional<glm::vec3> destination;
                if (goal.kind == Shared::Game::NpcIntentKind::MoveTo) {
                    destination = goal.point;
                }
                else if (goal.kind == Shared::Game::NpcIntentKind::Follow || goal.kind == Shared::Game::NpcIntentKind::Attack) {
                    destination = context.followTarget;
                }
                else {
                    destination = goal.FleeDestination(context.position);
                }
                if (destination && _navigation.StartFallback(*destination, context.meshReady)) {
                    RequestPlan();
                    return step;
                }
            }
            Finish(Shared::Game::NpcIntentStatus::Blocked, context.now, step);
            step.republish = goal.kind == Shared::Game::NpcIntentKind::Attack;
            return step;
        }

        case Shared::Game::NpcIntentStatus::Failed:
            Finish(Shared::Game::NpcIntentStatus::Failed, context.now, step);
            step.republish = goal.kind == Shared::Game::NpcIntentKind::Attack;
            return step;

        default: return step;
        }
    }

    NpcOrderStep NpcOrder::Advance(const NpcOrderContext &context, glm::vec3 &position) {
        NpcOrderStep step;
        if (_resumeAt != 0 && context.now >= _resumeAt) {
            _resumeAt      = 0;
            step.republish = AdvanceRoute(context);
        }

        if (_finished || _needsPlan || _pendingOutcome || !context.active || (_navigation.Goal().kind == Shared::Game::NpcIntentKind::Attack && !context.hasSimulator) || !_navigation.UsesServer(context.hasSimulator, context.meshReady)) {
            _lastAdvance = context.now;
            return step;
        }
        if (!_navigation.IsPlanned()) {
            // An auto move without a mesh that has just gone dormant: its
            // straight line is planned like any other route.
            RequestPlan();
            _lastAdvance = context.now;
            return step;
        }

        bool waiting = false;
        if (context.doorPassage && _navigation.HasCorner()) {
            // Include the largest dormant tick, so it cannot jump across a shut door.
            const float reach = 2.5F + (context.hasSimulator ? 0.0F : Shared::Game::NpcSpeedMetresPerSecond(_navigation.Goal().speed));
            for (const auto &door : _navigation.Doors()) {
                if (glm::distance(context.position, door.position) > reach || !_navigation.DoorAhead(context.position, door.position)) {
                    continue;
                }
                const auto passage = context.doorPassage(door.guid);
                if (passage == Shared::Game::DoorPassage::Blocked) {
                    _pendingOutcome = Shared::Game::NpcIntentStatus::Blocked;
                    break;
                }
                waiting = waiting || passage == Shared::Game::DoorPassage::Opening;
            }
        }
        if (waiting && !_waitingForDoor) {
            _doorWaitStarted = context.now;
        }
        if (waiting && context.now - _doorWaitStarted >= 5000) {
            _pendingOutcome = Shared::Game::NpcIntentStatus::Blocked;
        }
        if (waiting != _waitingForDoor || _pendingOutcome) {
            _waitingForDoor    = waiting;
            _publishedRevision = 0;
            step.republish     = true;
        }
        if (waiting || _pendingOutcome) {
            _lastAdvance = context.now;
            return step;
        }

        if (context.hasSimulator) {
            // Lead the body through the bends from where it is reported to be,
            // so it never arrives at one and stops for the next.
            bool led = false;
            while (_navigation.HasIntermediateCorner() && GroundDistance(context.position, _navigation.Corner()) < kCornerLeadDistance && CornerCut(context.position, _navigation.Corner(), _navigation.NextCorner()) <= kMaxCornerCut) {
                _navigation.AdvanceCorner();
                led = true;
            }
            step.republish = step.republish || led;
            _lastAdvance   = context.now;
            return step;
        }

        const MafiaNet::Time elapsed = _lastAdvance != 0 && context.now > _lastAdvance ? context.now - _lastAdvance : 0;
        _lastAdvance                 = context.now;
        if (elapsed == 0) {
            return step;
        }
        const std::size_t corner = _navigation.CornerIndex();
        _navigation.Advance(position, Shared::Game::NpcSpeedMetresPerSecond(_navigation.Goal().speed) * std::min(static_cast<float>(elapsed) / 1000.0F, 1.0F));
        if (!_navigation.HasCorner()) {
            _pendingOutcome = _navigation.Status();
        }
        else if (_navigation.CornerIndex() != corner) {
            // Kept current for whoever streams the body in next.
            step.republish = true;
        }
        return step;
    }

    Shared::Game::NpcIntent NpcOrder::SimulatorIntent(const glm::vec3 &position) const {
        if ((_finished || _waitingForDoor || _pendingOutcome) && _navigation.Goal().IsMotion()) {
            Shared::Game::NpcIntent hold;
            hold.kind      = Shared::Game::NpcIntentKind::Hold;
            hold.speed     = _navigation.Goal().speed;
            hold.stanceTag = _navigation.Goal().stanceTag;
            return hold;
        }
        return _navigation.SimulatorIntent(position);
    }

    void NpcOrder::OnTeleported() {
        if (!_finished && _navigation.IsPlanned()) {
            RequestPlan();
        }
        _lastAdvance = 0;
    }

    void NpcOrder::OnSimulatorChanged(MafiaNet::Time now) {
        _lastAdvance  = now;
        _seenStatus   = Shared::Game::NpcIntentStatus::Idle;
        _seenRevision = 0;
    }

    Shared::Game::NpcIntentStatus NpcOrder::Status(const NpcOrderContext &context) const {
        if (_finished) {
            return _outcome;
        }
        const bool current                  = !_needsPlan && _publishedRevision != 0 && context.intentRevision == _publishedRevision && context.reportRevision == _publishedRevision;
        const Shared::Game::NpcIntent &goal = _navigation.Goal();
        if (goal.IsMotion()) {
            if ((goal.kind == Shared::Game::NpcIntentKind::Follow || goal.kind == Shared::Game::NpcIntentKind::Attack) && !_navigation.IsPlanned() && current && context.reportStatus == Shared::Game::NpcIntentStatus::Reached) {
                return Shared::Game::NpcIntentStatus::Reached;
            }
            return Shared::Game::NpcIntentStatus::Running;
        }
        return current ? context.reportStatus : Shared::Game::NpcIntentStatus::Idle;
    }

    NpcOrders &NpcOrders::Get() {
        static NpcOrders instance;
        return instance;
    }

    NpcOrder *NpcOrders::Find(std::uint64_t networkId) {
        const auto found = _orders.find(networkId);
        return found != _orders.end() ? &found->second : nullptr;
    }

    void NpcOrders::CollectIds(std::vector<std::uint64_t> &ids) const {
        ids.clear();
        ids.reserve(_orders.size());
        for (const auto &entry : _orders) {
            ids.push_back(entry.first);
        }
    }

    void NpcOrders::Erase(std::uint64_t networkId) {
        _orders.erase(networkId);
        _queued.erase(networkId);
    }

    void NpcOrders::Track(std::uint64_t networkId) {
        const NpcOrder *order = Find(networkId);
        if (order != nullptr && order->NeedsPlan() && _queued.insert(networkId).second) {
            _planQueue.push_back(networkId);
        }
    }

    std::size_t NpcOrders::RunPlans(std::size_t budget, bool meshReady, const fu2::function<bool(std::uint64_t, NpcOrder &) const> &plan) {
        std::size_t planned = 0;
        std::size_t spent   = 0;
        while (!_planQueue.empty() && (!meshReady || spent < budget)) {
            const std::uint64_t networkId = _planQueue.front();
            _planQueue.pop_front();
            _queued.erase(networkId);
            NpcOrder *order = Find(networkId);
            if (order == nullptr || !order->NeedsPlan() || !plan(networkId, *order)) {
                continue;
            }
            ++planned;
            if (meshReady) {
                ++spent;
            }
        }
        return planned;
    }
} // namespace KCDC::Features::Npc
