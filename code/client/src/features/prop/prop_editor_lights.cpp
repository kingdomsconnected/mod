#include "prop_editor_lights.h"

#include "core/application.h"
#include "features/light/light_service.h"
#include "common/module_support.h"
#include "shared/pose_math.h"

#include <algorithm>
#include <cmath>

namespace KCDC::Features::Prop {
    namespace {
        KCDC::Features::Light::LightService *Service() {
            auto *application = Core::gApplication.get();
            return application ? &application->GetLightService() : nullptr;
        }

        bool ValidPosition(const SDK::Vec3 &position) {
            return Shared::Game::IsWorldPosition(KCDC::Common::ToGlm(position));
        }
    } // namespace

    bool EditorLights::Start(PlacedLight &light) {
        if (!light.editorVisible) {
            return true;
        }
        auto *service = Service();
        if (!service) {
            return false;
        }
        light.handle = service->Spawn(KCDC::Common::ToGlm(light.position), light.tuning);
        return light.handle != 0;
    }

    void EditorLights::Stop(PlacedLight &light) {
        if (auto *service = Service(); service && light.handle != 0) {
            service->Stop(light.handle);
        }
        light.handle = 0;
    }

    const PlacedLight *EditorLights::Find(std::uint64_t id) const {
        const auto found = std::find_if(_lights.begin(), _lights.end(), [id](const PlacedLight &light) {
            return light.id == id;
        });
        return found != _lights.end() ? &*found : nullptr;
    }

    PlacedLight *EditorLights::FindMutable(std::uint64_t id) {
        return const_cast<PlacedLight *>(static_cast<const EditorLights *>(this)->Find(id));
    }

    std::uint64_t EditorLights::Place(const SDK::Vec3 &position, const Shared::Game::LightTuning &tuning) {
        if (!ValidPosition(position) || !tuning.IsSane()) {
            return 0;
        }
        PlacedLight light;
        light.id       = _nextId++;
        light.position = position;
        light.tuning   = tuning;
        if (!Start(light)) {
            return 0;
        }
        _lights.push_back(std::move(light));
        return _lights.back().id;
    }

    std::uint64_t EditorLights::Import(const SDK::Vec3 &position, const Shared::Game::LightTuning &tuning, bool editorVisible) {
        PlacedLight light;
        light.id            = _nextId;
        light.position      = position;
        light.tuning        = tuning;
        light.editorVisible = editorVisible;
        return Restore(light) ? light.id : 0;
    }

    bool EditorLights::SetTransform(std::uint64_t id, const SDK::Vec3 &position) {
        auto *light = FindMutable(id);
        if (!light || !ValidPosition(position)) {
            return false;
        }
        if (!light->editorVisible) {
            light->position = position;
            return true;
        }
        auto *service = Service();
        if (!service) {
            return false;
        }
        if (service->IsAlive(light->handle)) {
            if (!service->Move(light->handle, KCDC::Common::ToGlm(position))) {
                return false;
            }
            light->position = position;
            return true;
        }
        auto restored     = *light;
        restored.position = position;
        restored.handle   = 0;
        if (!Start(restored)) {
            return false;
        }
        *light = std::move(restored);
        return true;
    }

    bool EditorLights::SetTuning(std::uint64_t id, const Shared::Game::LightTuning &tuning) {
        auto *light = FindMutable(id);
        if (!light || !tuning.IsSane()) {
            return false;
        }
        if (!light->editorVisible) {
            light->tuning = tuning;
            return true;
        }
        auto *service = Service();
        if (!service) {
            return false;
        }
        if (service->IsAlive(light->handle)) {
            if (!service->SetTuning(light->handle, tuning)) {
                return false;
            }
            light->tuning = tuning;
            return true;
        }
        auto restored   = *light;
        restored.tuning = tuning;
        restored.handle = 0;
        if (!Start(restored)) {
            return false;
        }
        *light = std::move(restored);
        return true;
    }

    bool EditorLights::RestoreTransform(std::uint64_t id, const SDK::Vec3 &position) {
        auto *light = FindMutable(id);
        if (!light || !ValidPosition(position)) {
            return false;
        }
        if (!SetTransform(id, position)) {
            Stop(*light);
            light->position = position;
        }
        return true;
    }

    bool EditorLights::RestoreTuning(std::uint64_t id, const Shared::Game::LightTuning &tuning) {
        auto *light = FindMutable(id);
        if (!light || !tuning.IsSane()) {
            return false;
        }
        if (!SetTuning(id, tuning)) {
            Stop(*light);
            light->tuning = tuning;
        }
        return true;
    }

    std::uint64_t EditorLights::Duplicate(std::uint64_t id) {
        const auto *source = Find(id);
        if (!source) {
            return 0;
        }
        return Place(SDK::Vec3 {source->position.x + 1.0F, source->position.y, source->position.z}, source->tuning);
    }

    bool EditorLights::Remove(std::uint64_t id) {
        const auto found = std::find_if(_lights.begin(), _lights.end(), [id](const PlacedLight &light) {
            return light.id == id;
        });
        if (found == _lights.end()) {
            return false;
        }
        Stop(*found);
        _lights.erase(found);
        return true;
    }

    void EditorLights::ApplyLayerVisibility(const EditorLayers *layers) {
        for (auto &light : _lights) {
            const bool visible = !layers || layers->Visible("light:" + std::to_string(light.id));
            if (light.editorVisible == visible) {
                continue;
            }
            light.editorVisible = visible;
            if (visible) {
                Start(light);
            }
            else {
                Stop(light);
            }
        }
    }

    void EditorLights::SetGroup(std::uint64_t id, const std::string &group) {
        if (auto *light = FindMutable(id)) {
            light->group = group;
        }
    }

    bool EditorLights::Restore(const PlacedLight &record) {
        if (record.id == 0 || Find(record.id) || !ValidPosition(record.position) || !record.tuning.IsSane()) {
            return false;
        }
        auto light   = record;
        light.handle = 0;
        // Undo restores authored data even if the native budget is temporarily
        // full. Refresh retries the visible light without losing its identity.
        Start(light);
        const auto at = std::find_if(_lights.begin(), _lights.end(), [&light](const PlacedLight &other) {
            return other.id > light.id;
        });
        _lights.insert(at, std::move(light));
        _nextId = std::max(_nextId, record.id + 1);
        return true;
    }

    void EditorLights::Clear() {
        for (auto &light : _lights) {
            Stop(light);
        }
        _lights.clear();
    }

    void EditorLights::Forget() {
        _lights.clear();
    }

    void EditorLights::Refresh() {
        auto *service = Service();
        if (!service) {
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        if (now < _nextRetry) {
            return;
        }
        _nextRetry = now + std::chrono::seconds(1);
        for (auto &light : _lights) {
            if (light.editorVisible && !service->IsAlive(light.handle)) {
                Start(light);
            }
        }
    }

    std::uint64_t EditorLights::PickNearest(const SDK::Vec3 &origin, const SDK::Vec3 &direction, float maxDistance, float &distance, const EditorLayers *layers) const {
        std::uint64_t picked = 0;
        float nearest        = maxDistance;
        for (const auto &light : _lights) {
            if (!light.editorVisible || (layers && !layers->Editable("light:" + std::to_string(light.id)))) {
                continue;
            }
            const SDK::Vec3 offset {light.position.x - origin.x, light.position.y - origin.y, light.position.z - origin.z};
            const float along       = offset.x * direction.x + offset.y * direction.y + offset.z * direction.z;
            const float missSquared = offset.x * offset.x + offset.y * offset.y + offset.z * offset.z - along * along;
            if (missSquared > kMarkerRadius * kMarkerRadius) {
                continue;
            }
            const float chord = std::sqrt(std::max(kMarkerRadius * kMarkerRadius - missSquared, 0.0F));
            if (along + chord < 0.0F) {
                continue;
            }
            const float hit = std::max(0.0F, along - chord);
            if (hit < nearest) {
                nearest = hit;
                picked  = light.id;
            }
        }
        distance = picked ? nearest : maxDistance;
        return picked;
    }

    SDK::AABB EditorLights::BoundsOf(const PlacedLight &light) {
        return SDK::AABB {SDK::Vec3 {light.position.x - kMarkerRadius, light.position.y - kMarkerRadius, light.position.z - kMarkerRadius}, SDK::Vec3 {light.position.x + kMarkerRadius, light.position.y + kMarkerRadius, light.position.z + kMarkerRadius}};
    }
} // namespace KCDC::Features::Prop
