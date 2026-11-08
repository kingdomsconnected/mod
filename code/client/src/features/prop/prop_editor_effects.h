#pragma once

#include "prop_editor_layers.h"
#include "sdk/types.h"
#include "shared/features/vfx/vfx_tuning.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace KCDC::Features::Prop {
    /** One particle effect the editor placed, and the emitter playing it. */
    struct PlacedEffect {
        std::uint64_t id        = 0;
        std::size_t effectIndex = 0; // row of the effect catalog
        std::string name;            // the catalog name, `WH_Particels.fires.torch_a`
        std::string label;           // its last segment, for lists

        SDK::Vec3 position {};
        SDK::Quat rotation {0.0F, 0.0F, 0.0F, 1.0F};
        Shared::Game::VfxTuning tuning;
        std::string group;
        bool editorVisible = true;

        std::uint32_t handle = 0; // VfxService's, 0 while not playing
    };

    /**
     * Every effect the editor has placed. Each plays on a `VfxService`
     * emitter; tuning is read when an emitter starts, so re-tuning restarts
     * it. Effects have no geometry, so they are picked as a marker sphere.
     * Game thread only.
     */
    class EditorEffects final {
      public:
        /** Marker radius at scale 1, in metres. */
        static constexpr float kMarkerRadius = 0.5F;

        /** Returns 0 when the row is unknown or the emitter budget is spent. */
        std::uint64_t Place(std::size_t effectIndex, const SDK::Vec3 &position, const SDK::Quat &rotation, const Shared::Game::VfxTuning &tuning);

        bool SetTransform(std::uint64_t id, const SDK::Vec3 &position, const SDK::Quat &rotation);
        bool SetTuning(std::uint64_t id, const Shared::Game::VfxTuning &tuning);

        /** A copy a metre along the effect's own X. Returns 0 when it failed. */
        std::uint64_t Duplicate(std::uint64_t id);

        bool Remove(std::uint64_t id);
        /** Apply layer visibility in one pass; nullptr restores every effect. */
        void ApplyLayerVisibility(const EditorLayers *layers);
        void SetGroup(std::uint64_t id, const std::string &group);

        /** Plays a removed effect again under its old id, for undo. */
        bool Restore(const PlacedEffect &record);

        void Clear();

        /** Drops every record without stopping anything: the level has gone. */
        void Forget();

        /** Restarts any effect whose emitter was stopped behind the editor's back. */
        void Refresh();

        /** The nearest marker a ray enters, or 0, with its distance. */
        std::uint64_t PickNearest(const SDK::Vec3 &origin, const SDK::Vec3 &direction, float maxDistance, float &distance, const EditorLayers *layers = nullptr) const;

        const PlacedEffect *Find(std::uint64_t id) const;

        /** The marker's box, for outlines and framing. */
        static SDK::AABB BoundsOf(const PlacedEffect &effect);

        const std::vector<PlacedEffect> &Effects() const {
            return _effects;
        }

        std::size_t Count() const {
            return _effects.size();
        }

      private:
        PlacedEffect *FindMutable(std::uint64_t id);

        static bool Start(PlacedEffect &effect);
        static void Stop(PlacedEffect &effect);

        std::vector<PlacedEffect> _effects;
        std::uint64_t _nextId = 1;
    };
} // namespace KCDC::Features::Prop
