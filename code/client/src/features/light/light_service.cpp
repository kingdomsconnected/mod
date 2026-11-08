#include "light_service.h"
#include "light.h"

#include "common/module_support.h"
#include "sdk/cryengine/entitysystem/i_entity.h"
#include "sdk/cryengine/entitysystem/i_entity_class_registry.h"
#include "sdk/cryengine/entitysystem/i_entity_system.h"
#include "sdk/cryengine/renderer/s_render_light.h"
#include "shared/pose_math.h"

#include <algorithm>
#include <core_modules.h>
#include <cstdio>
#include <logging/logger.h>
#include <networking/replication/replication_manager.h>
#include <scripting/resource/resource_manager.h>

namespace KCDC::Features::Light {
    namespace {
        constexpr int kLightSlot = 0;

        bool ApplyLight(SDK::IEntity *entity, const Shared::Game::LightTuning &tuning) {
            SDK::SRenderLight light;
            light.m_flags = SDK::SRenderLight::kPoint | (tuning.castShadows ? SDK::SRenderLight::kCastShadows : 0);
            light.SetPosition(entity->GetWorldPos());
            light.SetLightColor({tuning.color.x * tuning.intensity, tuning.color.y * tuning.intensity, tuning.color.z * tuning.intensity, 1.0F});
            light.SetRadius(tuning.radius, 0.05F);
            // LoadLight owns a native copy before this descriptor is destroyed.
            return entity->LoadLight(kLightSlot, &light) == kLightSlot && entity->GetSlotCount() > kLightSlot;
        }
    } // namespace

    std::uint32_t LightService::SpawnFailed(const char *reason) {
        if (_lastSpawnFailure != reason) {
            Framework::Logging::GetLogger("Light")->warn("Light creation failed: {}", reason);
            _lastSpawnFailure = reason;
        }
        return kNoHandle;
    }

    std::uint32_t LightService::Spawn(const glm::vec3 &position, const Shared::Game::LightTuning &tuning, const std::string &owner) {
        ObserveNativeContext();
        DrainPendingStops();
        auto *entities = SDK::IEntitySystem::GetInstance();
        if (entities == nullptr) {
            return SpawnFailed("entity system unavailable");
        }
        if (!Shared::Game::IsWorldPosition(position) || !tuning.IsSane()) {
            return SpawnFailed("invalid position or tuning");
        }
        if (_lights.size() >= kMaxLiveLights) {
            return SpawnFailed("native light budget exhausted");
        }
        auto *registry    = entities->GetClassRegistry();
        auto *entityClass = registry != nullptr ? registry->FindClass(SDK::EntityClassNames::Default) : nullptr;
        if (entityClass == nullptr) {
            return SpawnFailed("Default carrier class is not registered");
        }
        char name[64];
        std::snprintf(name, sizeof(name), "KCDC_Light_%u", _nextHandle);
        SDK::SEntitySpawnParams params {};
        params.entityClass = entityClass;
        params.name        = name;
        params.position    = KCDC::Common::ToNative(position);
        auto *entity       = entities->SpawnEntity(params, true);
        if (entity == nullptr) {
            return SpawnFailed("SpawnEntity refused the Light entity");
        }
        const auto entityId = entity->GetId();
        if (!ApplyLight(entity, tuning)) {
            entities->RemoveEntity(entityId, false);
            return SpawnFailed("Native LoadLight did not allocate a render slot");
        }
        std::uint32_t handle = _nextHandle++;
        if (handle == kNoHandle) {
            handle = _nextHandle++;
        }
        _lights.push_back({handle, entityId, position, tuning});
        if (!owner.empty()) {
            std::scoped_lock lock(_pendingStopsMutex);
            _resourceLights[owner].push_back(handle);
        }
        _lastSpawnFailure = nullptr;
        return handle;
    }

    const LightService::Entry *LightService::Find(std::uint32_t handle) const {
        if (_observedContext != _nativeContext.load()) {
            return nullptr;
        }
        const auto it = std::find_if(_lights.begin(), _lights.end(), [handle](const Entry &entry) {
            return entry.handle == handle;
        });
        return it != _lights.end() ? &*it : nullptr;
    }

    bool LightService::Move(std::uint32_t handle, const glm::vec3 &position) {
        ObserveNativeContext();
        auto *entry  = Find(handle);
        auto *entity = entry != nullptr ? KCDC::Common::ResolveEntity(entry->entityId) : nullptr;
        if (entity == nullptr || !Shared::Game::IsWorldPosition(position)) {
            return false;
        }
        KCDC::Common::PlaceEntity(entity, position, glm::quat(1.0F, 0.0F, 0.0F, 0.0F));
        entry->position = position;
        return true;
    }

    bool LightService::SetTuning(std::uint32_t handle, const Shared::Game::LightTuning &tuning) {
        ObserveNativeContext();
        auto *entry  = Find(handle);
        auto *entity = entry != nullptr ? KCDC::Common::ResolveEntity(entry->entityId) : nullptr;
        if (entity == nullptr || !tuning.IsSane()) {
            return false;
        }
        if (!ApplyLight(entity, tuning)) {
            return false;
        }
        entry->tuning = tuning;
        return true;
    }

    bool LightService::Stop(std::uint32_t handle) {
        ObserveNativeContext();
        const auto it = std::find_if(_lights.begin(), _lights.end(), [handle](const Entry &entry) {
            return entry.handle == handle;
        });
        if (it == _lights.end()) {
            return false;
        }
        if (auto *entities = SDK::IEntitySystem::GetInstance()) {
            entities->RemoveEntity(it->entityId, false);
        }
        _lights.erase(it);
        ForgetOwnership(handle);
        return true;
    }

    bool LightService::IsAlive(std::uint32_t handle) const {
        const auto *entry = Find(handle);
        return entry != nullptr && KCDC::Common::ResolveEntity(entry->entityId) != nullptr;
    }

    bool LightService::Describe(std::uint32_t handle, glm::vec3 &position, Shared::Game::LightTuning &tuning) const {
        const auto *entry = Find(handle);
        if (entry == nullptr || !IsAlive(handle)) {
            return false;
        }
        position = entry->position;
        tuning   = entry->tuning;
        return true;
    }

    void LightService::AttachResources(Framework::Scripting::ResourceManager *resources) {
        if (resources == nullptr || (_resources == resources && _cleanupLifetime)) {
            return;
        }
        _resources                        = resources;
        _cleanupLifetime                  = std::make_shared<int>(0);
        const std::weak_ptr<int> lifetime = _cleanupLifetime;
        resources->AddOnResourceStopped([this, lifetime](const std::string &owner) {
            if (!lifetime.expired()) {
                QueueResourceStop(owner);
            }
        });
        resources->AddOnResourceError([this, lifetime](const std::string &owner, const std::string &) {
            if (!lifetime.expired()) {
                QueueResourceStop(owner);
            }
        });
    }

    void LightService::QueueResourceStop(const std::string &owner) {
        std::scoped_lock lock(_pendingStopsMutex);
        const auto it = _resourceLights.find(owner);
        if (it == _resourceLights.end()) {
            return;
        }
        // Capture handles now: a restarted resource may spawn again before the next tick.
        _pendingStops.insert(_pendingStops.end(), it->second.begin(), it->second.end());
        _resourceLights.erase(it);
    }

    void LightService::ForgetOwnership(std::uint32_t handle) {
        std::scoped_lock lock(_pendingStopsMutex);
        for (auto it = _resourceLights.begin(); it != _resourceLights.end();) {
            std::erase(it->second, handle);
            if (it->second.empty()) {
                it = _resourceLights.erase(it);
            }
            else {
                ++it;
            }
        }
    }

    LightService::Entry *LightService::Find(std::uint32_t handle) {
        if (_observedContext != _nativeContext.load()) {
            return nullptr;
        }
        const auto it = std::find_if(_lights.begin(), _lights.end(), [handle](const Entry &entry) {
            return entry.handle == handle;
        });
        return it != _lights.end() ? &*it : nullptr;
    }

    void LightService::QueueStop(std::uint32_t handle) {
        if (handle != kNoHandle) {
            std::scoped_lock lock(_pendingStopsMutex);
            _pendingStops.push_back(handle);
        }
    }

    void LightService::NativeContextChanged() {
        ++_nativeContext;
    }

    void LightService::ObserveNativeContext() {
        const auto context = _nativeContext.load();
        if (_observedContext == context) {
            return;
        }
        _observedContext = context;
        // The old world's entities have already been destroyed. Their IDs may
        // now name somebody else's entities, so never release them here.
        _lights.clear();
        {
            std::scoped_lock lock(_pendingStopsMutex);
            _pendingStops.clear();
            _resourceLights.clear();
        }
        if (auto *replication = Framework::CoreModules::GetReplication()) {
            replication->ForEach<Light>([](Light *light) {
                light->Unbind();
            });
        }
    }

    void LightService::Reset() {
        ObserveNativeContext();
        if (auto *entities = SDK::IEntitySystem::GetInstance()) {
            for (const auto &entry : _lights) {
                entities->RemoveEntity(entry.entityId, false);
            }
        }
        _lights.clear();
        std::scoped_lock lock(_pendingStopsMutex);
        _pendingStops.clear();
        _resourceLights.clear();
        // ResourceManager retains its callbacks across reconnects. Keep this
        // subscription too, instead of appending expired callbacks each time.
    }

    void LightService::Update() {
        ObserveNativeContext();
        // A native entity can disappear without a level transition. Release
        // its budget entry before replicas or the editor try to recreate it.
        std::erase_if(_lights, [this](const Entry &entry) {
            if (KCDC::Common::ResolveEntity(entry.entityId) != nullptr) {
                return false;
            }
            ForgetOwnership(entry.handle);
            return true;
        });
        DrainPendingStops();
        if (auto *replication = Framework::CoreModules::GetReplication()) {
            replication->ForEach<Light>([](Light *light) {
                light->Frame();
            });
        }
    }

    void LightService::DrainPendingStops() {
        std::vector<std::uint32_t> stops;
        {
            std::scoped_lock lock(_pendingStopsMutex);
            stops.swap(_pendingStops);
        }
        for (const auto handle : stops) {
            Stop(handle);
        }
    }
} // namespace KCDC::Features::Light
