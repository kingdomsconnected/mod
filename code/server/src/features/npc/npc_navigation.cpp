#include "npc_navigation.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace KCDC::Features::Npc {
    void NpcNavigation::Start(const Shared::Game::NpcIntent &goal, Pathfinding mode) {
        _goal        = goal;
        _mode        = mode;
        _destination = goal.point;
        Invalidate();
    }

    void NpcNavigation::Invalidate() {
        _path.clear();
        _doors.clear();
        _index    = 0;
        _planned  = false;
        _complete = false;
        _onMesh   = false;
    }

    bool NpcNavigation::UsesServer(bool hasSimulator, bool meshReady) const {
        if (!_goal.IsMotion()) {
            return false;
        }
        switch (_mode) {
        case Pathfinding::Server: return true;
        case Pathfinding::Auto: return _goal.kind == Shared::Game::NpcIntentKind::MoveTo && (meshReady || !hasSimulator);
        case Pathfinding::Game:
        default: return false;
        }
    }

    void NpcNavigation::Plan(const Navigation::Navmesh &mesh, const Navigation::QueryOptions &options, const glm::vec3 &position) {
        Invalidate();
        if (!mesh.IsReady()) {
            // Only auto reaches this: server mode is refused without a mesh
            // before it is accepted, and a fallback needs one to start.
            _path     = {_destination};
            _complete = true;
        }
        else if (auto path = mesh.FindPath(position, _destination, options); path && !path->points.empty()) {
            _path     = std::move(path->points);
            _doors    = std::move(path->doors);
            _complete = path->complete;
            // Arrival for a flee is separation from the threat, at its radius;
            // a path the mesh cut short of that is not an escape.
            if (_goal.kind == Shared::Game::NpcIntentKind::Flee && glm::distance(glm::vec2(_path.back()), glm::vec2(_goal.point)) < _goal.radius) {
                _complete = false;
            }
            _onMesh = true;
            // The first point is the snapped start. Move from the body's pose
            // towards the next corner instead of reporting this as an arrival.
            if (_path.size() > 1 && glm::distance(position, _path.front()) <= 0.5F) {
                _index = 1;
            }
        }
        // Failed queries on a loaded mesh leave the move blocked.
        _planned = true;
    }

    bool NpcNavigation::StartFallback(const glm::vec3 &destination, bool meshReady) {
        if (_mode == Pathfinding::Server || !_goal.IsMotion() || !meshReady) {
            return false;
        }
        _mode        = Pathfinding::Server;
        _destination = destination;
        Invalidate();
        return true;
    }

    Shared::Game::NpcIntent NpcNavigation::SimulatorIntent(const glm::vec3 &position) const {
        auto intent = _goal;
        if (_planned) {
            intent.kind   = Shared::Game::NpcIntentKind::MoveTo;
            intent.point      = HasCorner() ? Corner() : position;
            intent.radius     = CornerRadius();
            intent.pathBeyond = 0.0F;
            for (std::size_t i = _index + 1; i < _path.size(); ++i) {
                intent.pathBeyond += glm::distance(_path[i - 1], _path[i]);
            }
            intent.pathBeyond = std::min(intent.pathBeyond, Shared::Game::NpcIntent::kMaxPathBeyond);
        }
        return intent;
    }

    bool NpcNavigation::DoorAhead(const glm::vec3 &position, const glm::vec3 &door) const {
        auto from = position;
        for (std::size_t i = _index; i < _path.size(); ++i) {
            const auto span      = _path[i] - from;
            const float lengthSq = glm::dot(span, span);
            const float along    = lengthSq > 1e-6F ? glm::dot(door - from, span) / lengthSq : 0.0F;
            if (along >= 0.0F && glm::distance(door, from + span * std::clamp(along, 0.0F, 1.0F)) <= Navigation::Navmesh::kDoorwayReach) {
                return true;
            }
            from = _path[i];
        }
        return false;
    }

    float NpcNavigation::CornerRadius() const {
        if (_complete && _index + 1 == _path.size() && _goal.kind != Shared::Game::NpcIntentKind::Flee) {
            return std::max(_goal.radius, 0.5F);
        }
        // A bend is arrived at close to: the body turns for the next corner
        // from wherever it was declared there, and a wide radius at a tight
        // bend -- the top of a stair, a doorway -- turns it into the rail.
        return _goal.speed == Shared::Game::NpcSpeed::Walk ? kBendRadius : kFastBendRadius;
    }

    float NpcNavigation::SearchHeight() const {
        return _goal.kind == Shared::Game::NpcIntentKind::Flee ? std::clamp(_goal.radius + 1.0F, kSearchHeight, kMaxSearchHeight) : kSearchHeight;
    }

    void NpcNavigation::AdvanceCorner() {
        if (HasCorner()) {
            ++_index;
        }
    }

    Shared::Game::NpcIntentStatus NpcNavigation::Status() const {
        return HasCorner() ? Shared::Game::NpcIntentStatus::Running : (_complete ? Shared::Game::NpcIntentStatus::Reached : Shared::Game::NpcIntentStatus::Blocked);
    }

    void NpcNavigation::Advance(glm::vec3 &position, float distance) {
        while (distance > 0.0F && HasCorner()) {
            const glm::vec3 corner = Corner();
            const glm::vec3 delta  = corner - position;
            const float planar     = std::sqrt(delta.x * delta.x + delta.y * delta.y);
            if (_complete && _index + 1 == _path.size() && glm::distance(position, corner) <= CornerRadius()) {
                AdvanceCorner();
                return;
            }
            if (planar <= distance) {
                position = corner;
                distance -= planar;
                AdvanceCorner();
            }
            else {
                const float fraction = distance / planar;
                position.x += delta.x * fraction;
                position.y += delta.y * fraction;
                position.z = _onMesh ? position.z + delta.z * fraction : corner.z;
                distance   = 0.0F;
            }
        }
    }
} // namespace KCDC::Features::Npc
