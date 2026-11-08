#pragma once

#include "prop_editor_layers.h"
#include "sdk/types.h"
#include "shared/features/light/light_tuning.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace KCDC::Features::Prop {
    /** One authored point light and its native service handle. */
    struct PlacedLight {
        std::uint64_t id = 0;
        SDK::Vec3 position {};
        Shared::Game::LightTuning tuning;
        std::string group;
        bool editorVisible   = true;
        std::uint32_t handle = 0;
    };

    /** Game-thread owner of authored lights. Markers are independent of illumination radius. */
    class EditorLights final {
      public:
        static constexpr float kMarkerRadius = 0.5F;

        std::uint64_t Place(const SDK::Vec3 &position, const Shared::Game::LightTuning &tuning);
        /** Retains imported authoring data while native creation is unavailable. */
        std::uint64_t Import(const SDK::Vec3 &position, const Shared::Game::LightTuning &tuning, bool editorVisible);
        bool SetTransform(std::uint64_t id, const SDK::Vec3 &position);
        bool SetTuning(std::uint64_t id, const Shared::Game::LightTuning &tuning);
        /** History restores authored values immediately; Refresh retries native failures. */
        bool RestoreTransform(std::uint64_t id, const SDK::Vec3 &position);
        bool RestoreTuning(std::uint64_t id, const Shared::Game::LightTuning &tuning);
        std::uint64_t Duplicate(std::uint64_t id);
        bool Remove(std::uint64_t id);
        void ApplyLayerVisibility(const EditorLayers *layers);
        void SetGroup(std::uint64_t id, const std::string &group);
        bool Restore(const PlacedLight &record);
        void Clear();
        /** Drops records after the engine has destroyed the level. */
        void Forget();
        void Refresh();
        std::uint64_t PickNearest(const SDK::Vec3 &origin, const SDK::Vec3 &direction, float maxDistance, float &distance, const EditorLayers *layers = nullptr) const;
        const PlacedLight *Find(std::uint64_t id) const;
        static SDK::AABB BoundsOf(const PlacedLight &light);
        const std::vector<PlacedLight> &Lights() const {
            return _lights;
        }
        std::size_t Count() const {
            return _lights.size();
        }

      private:
        PlacedLight *FindMutable(std::uint64_t id);
        static bool Start(PlacedLight &light);
        static void Stop(PlacedLight &light);
        std::vector<PlacedLight> _lights;
        std::uint64_t _nextId = 1;
        std::chrono::steady_clock::time_point _nextRetry {};
    };
} // namespace KCDC::Features::Prop
