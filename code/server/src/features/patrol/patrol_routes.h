#pragma once

#include "shared/features/patrol/patrol_route.h"

#include <cstdint>
#include <limits>
#include <map>
#include <stdexcept>
#include <unordered_map>

namespace KCDC::Features::Patrol {
    /** Authored definitions only. Executing actors own independent snapshots. */
    class PatrolRoutes final {
      public:
        static PatrolRoutes &Get() {
            static PatrolRoutes routes;
            return routes;
        }

        std::uint32_t Create(const Shared::Game::PatrolRouteDefinition &route) {
            if (!route.IsSane()) {
                throw std::runtime_error("Invalid patrol route definition");
            }
            if (Find(route.id) != 0) {
                throw std::runtime_error("Duplicate patrol route id: " + route.id);
            }
            if (_nextHandle == std::numeric_limits<std::uint32_t>::max()) {
                throw std::runtime_error("Patrol route handles exhausted");
            }
            const auto handle = ++_nextHandle;
            _routes.emplace(handle, route);
            _ids.emplace(route.id, handle);
            return handle;
        }

        const Shared::Game::PatrolRouteDefinition *Find(std::uint32_t handle) const {
            const auto found = _routes.find(handle);
            return found == _routes.end() ? nullptr : &found->second;
        }
        std::uint32_t Find(const std::string &id) const {
            const auto found = _ids.find(id);
            return found == _ids.end() ? 0 : found->second;
        }
        bool Update(std::uint32_t handle, const Shared::Game::PatrolRouteDefinition &definition) {
            const auto found = _routes.find(handle);
            if (found == _routes.end() || found->second.id != definition.id || !definition.IsSane()) {
                return false;
            }
            found->second = definition;
            return true;
        }
        bool Destroy(std::uint32_t handle) {
            const auto found = _routes.find(handle);
            if (found == _routes.end()) {
                return false;
            }
            _ids.erase(found->second.id);
            _routes.erase(found);
            return true;
        }
        void Clear() {
            _routes.clear();
            _ids.clear();
        }
        const std::map<std::uint32_t, Shared::Game::PatrolRouteDefinition> &All() const {
            return _routes;
        }

      private:
        std::map<std::uint32_t, Shared::Game::PatrolRouteDefinition> _routes;
        std::unordered_map<std::string, std::uint32_t> _ids;
        std::uint32_t _nextHandle = 0;
    };
} // namespace KCDC::Features::Patrol
