#pragma once

#include "features/navigation/navmesh.h"
#include "shared/features/patrol/patrol_route.h"

namespace KCDC::Features::Patrol {
    struct PatrolLeg {
        std::size_t fromIndex;
        std::size_t toIndex;
        std::optional<Navigation::Navmesh::Path> path;
    };
    struct PatrolValidation {
        bool available = false;
        bool complete  = false;
        std::vector<PatrolLeg> legs;
    };
    // Include closing and reverse legs: reachability can be directional.
    inline PatrolValidation ValidatePatrol(const Shared::Game::PatrolRouteDefinition &route, const Navigation::Navmesh &mesh, const Navigation::QueryOptions &options) {
        PatrolValidation result;
        result.available = mesh.IsReady();
        if (!result.available || !route.IsSane(false)) {
            return result;
        }
        result.complete = true;
        const auto add  = [&](std::size_t from, std::size_t to) {
            auto path       = mesh.FindPath(route.points[from].position, route.points[to].position, options);
            result.complete = result.complete && path && path->complete;
            result.legs.push_back({from, to, std::move(path)});
        };
        if (route.points.size() == 1) {
            add(0, 0);
        }
        else {
            for (std::size_t i = 1; i < route.points.size(); ++i) {
                add(i - 1, i);
            }
            if (route.mode == Shared::Game::PatrolMode::Loop) {
                add(route.points.size() - 1, 0);
            }
            if (route.mode == Shared::Game::PatrolMode::PingPong) {
                for (std::size_t i = route.points.size() - 1; i > 0; --i) {
                    add(i, i - 1);
                }
            }
        }
        return result;
    }
} // namespace KCDC::Features::Patrol
