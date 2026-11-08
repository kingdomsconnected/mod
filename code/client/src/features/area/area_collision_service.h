#pragma once

#include "core/feature_registry.h"
#include "sdk/cryengine/physics/i_physical_entity.h"
#include "shared/features/area/area_definition.h"

#include <atomic>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace KCDC::Features::Area {
    /** Owns native bodies on the game thread. The editor owns a separate
     * instance, fed with immutable authored snapshots through its command queue. */
    class AreaCollisionService final: public Core::IFeatureService {
      public:
        void Queue(std::uint64_t networkId);
        void Update() override;
        void Reset() override;
        void NativeContextChanged() override;
        void Forget();
        bool ReplacePreview(const std::vector<Shared::Game::AreaDefinition> &areas, std::string &error);
        /** Game-thread only: ignore selected preview walls during ground probes. */
        void AppendBodies(const std::vector<std::string> &ids, std::vector<SDK::IPhysicalEntity *> &bodies) const;

      private:
        struct Boundary {
            SDK::IPhysicalEntity *body = nullptr;
            std::string shapeJson;
        };
        bool Set(const std::string &key, const Shared::Game::AreaDefinition &definition, std::string &error);
        void Remove(const std::string &key);
        void ObserveContext();
        std::unordered_map<std::string, Boundary> _boundaries;
        std::mutex _mutex;
        std::unordered_set<std::uint64_t> _pending;
        std::atomic<std::uint64_t> _context {1};
        std::uint64_t _observedContext = 1;
    };
} // namespace KCDC::Features::Area
