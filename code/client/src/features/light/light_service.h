#pragma once

#include "core/feature_registry.h"
#include "sdk/types.h"
#include "shared/features/light/light_tuning.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace Framework::Scripting {
    class ResourceManager;
}

namespace KCDC::Features::Light {
    /** Owns native point-light carriers. All native work runs on the game thread. */
    class LightService final: public KCDC::Core::IFeatureService {
      public:
        static constexpr std::uint32_t kNoHandle    = 0;
        static constexpr std::size_t kMaxLiveLights = 192;

        std::uint32_t Spawn(const glm::vec3 &position, const Shared::Game::LightTuning &tuning, const std::string &owner = {});
        bool Describe(std::uint32_t handle, glm::vec3 &position, Shared::Game::LightTuning &tuning) const;
        void AttachResources(Framework::Scripting::ResourceManager *resources);
        bool Move(std::uint32_t handle, const glm::vec3 &position);
        bool SetTuning(std::uint32_t handle, const Shared::Game::LightTuning &tuning);
        bool Stop(std::uint32_t handle);
        bool IsAlive(std::uint32_t handle) const;
        void QueueStop(std::uint32_t handle);
        void Update() override;
        void Reset() override;
        void NativeContextChanged() override;

      private:
        struct Entry {
            std::uint32_t handle;
            SDK::EntityId entityId;
            glm::vec3 position;
            Shared::Game::LightTuning tuning;
        };
        void ObserveNativeContext();
        const Entry *Find(std::uint32_t handle) const;
        Entry *Find(std::uint32_t handle);
        std::uint32_t SpawnFailed(const char *reason);
        const char *_lastSpawnFailure = nullptr;
        std::vector<Entry> _lights;
        std::uint32_t _nextHandle = 1;
        std::atomic<std::uint64_t> _nativeContext {1};
        std::uint64_t _observedContext = 0;
        std::mutex _pendingStopsMutex;
        std::vector<std::uint32_t> _pendingStops;
        std::unordered_map<std::string, std::vector<std::uint32_t>> _resourceLights;
        void QueueResourceStop(const std::string &owner);
        void DrainPendingStops();
        void ForgetOwnership(std::uint32_t handle);
        Framework::Scripting::ResourceManager *_resources = nullptr;
        std::shared_ptr<int> _cleanupLifetime;
    };
} // namespace KCDC::Features::Light
