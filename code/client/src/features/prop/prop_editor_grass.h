#pragma once

#include "features/grass/grass_module.h"
#include "sdk/types.h"
#include "shared/features/grass/grass_clearing.h"
#include "shared/features/level_edit/level_edit_definition.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace KCDC::Features::Prop {
    /** How far around an object its clearing reaches. */
    struct GrassClearingOptions {
        static constexpr float kMinMargin = 0.0F;
        static constexpr float kMaxMargin = 10.0F;

        /** Metres added around the object's footprint, so the grass does not lean in over the sill. */
        float margin = 0.5F;

        bool IsSane() const {
            return std::isfinite(margin) && margin >= kMinMargin && margin <= kMaxMargin;
        }

        bool operator==(const GrassClearingOptions &other) const = default;
    };

    /** How far under an object's lowest point the grass may be rooted and still be under it: a floor set on a slope hangs over lower ground. */
    inline constexpr float kGrassClearingDepth = 2.0F;

    /**
     * The clearing an object makes: its footprint grown by the margin, from a
     * little under its foot to its top.
     *
     * An object standing upright gives a box turned with it, built from its
     * own box and scale, so a house set at an angle clears a house-shaped
     * patch. A tilted one -- or one with no box of its own, as a smart
     * object's assembly -- gives the world-aligned box around it. False when
     * there is no usable box to build either from.
     */
    inline bool GrassClearingUnder(const SDK::Vec3 &position, const SDK::Quat &rotation, const SDK::Vec3 &scale, const SDK::AABB *localBounds, const SDK::AABB &worldBounds, const GrassClearingOptions &options, Shared::Game::GrassClearing &clearing) {
        clearing            = Shared::Game::GrassClearing {};
        const float low     = worldBounds.min.z - kGrassClearingDepth;
        clearing.center.z   = (low + worldBounds.max.z) * 0.5F;
        clearing.halfSize.z = (worldBounds.max.z - low) * 0.5F;

        // The object's up axis after its turn; one that still points up has only a yaw to carry over.
        const float up = 1.0F - 2.0F * (rotation.x * rotation.x + rotation.y * rotation.y);
        if (localBounds != nullptr && up >= 0.999F) {
            const float yaw     = std::atan2(2.0F * (rotation.w * rotation.z + rotation.x * rotation.y), 1.0F - 2.0F * (rotation.y * rotation.y + rotation.z * rotation.z));
            const float centreX = (localBounds->min.x + localBounds->max.x) * 0.5F * scale.x, centreY = (localBounds->min.y + localBounds->max.y) * 0.5F * scale.y;
            const float sine = std::sin(yaw), cosine = std::cos(yaw);
            clearing.yawDegrees = glm::degrees(yaw);
            clearing.center.x   = position.x + centreX * cosine - centreY * sine;
            clearing.center.y   = position.y + centreX * sine + centreY * cosine;
            clearing.halfSize.x = (localBounds->max.x - localBounds->min.x) * 0.5F * std::fabs(scale.x) + options.margin;
            clearing.halfSize.y = (localBounds->max.y - localBounds->min.y) * 0.5F * std::fabs(scale.y) + options.margin;
        }
        else {
            clearing.center.x   = (worldBounds.min.x + worldBounds.max.x) * 0.5F;
            clearing.center.y   = (worldBounds.min.y + worldBounds.max.y) * 0.5F;
            clearing.halfSize.x = (worldBounds.max.x - worldBounds.min.x) * 0.5F + options.margin;
            clearing.halfSize.y = (worldBounds.max.y - worldBounds.min.y) * 0.5F + options.margin;
        }
        return options.IsSane() && clearing.IsSane();
    }

    /** One clearing the editor made, as its scene holds it. */
    struct PlacedClearing {
        std::uint64_t id = 0;
        Shared::Game::GrassClearing box;

        /** Its id in `GrassModule` while it stands; never saved. */
        std::uint64_t applied = 0;
    };

    /**
     * The clearings of the map being edited: boxes of the terrain's merged
     * vegetation -- the tall grass no object edit can name -- taken out so a
     * floor stays clear. A save writes each as a `grass` entry of the map's
     * `world` array, which is the form the server's level edits take it in.
     *
     * The editor's ids are its own and survive undo and redo; `GrassModule`
     * does the clearing and is shared with what the server sent. Game thread
     * only.
     */
    class EditorGrass final {
      public:
        /** Clears `box`. Returns the clearing's id, or 0 for a box that is not sane. */
        std::uint64_t Add(const Shared::Game::GrassClearing &box, KCDC::Features::Grass::GrassChange *change = nullptr);

        /** Puts a removed clearing back under the id it had, for undo and redo. False when the id is taken. */
        bool Restore(const PlacedClearing &clearing);

        /** Gives the grass of one clearing back. */
        bool Remove(std::uint64_t id);

        /** Gives all of it back. */
        void Clear();

        /** Drops every clearing of a level that is gone. The grass module holds no sector, so there is nothing to put back. */
        void Forget();

        const PlacedClearing *Find(std::uint64_t id) const;

        /** The clearing already made of exactly `box`, or null. */
        const PlacedClearing *FindBox(const Shared::Game::GrassClearing &box) const;

        const std::vector<PlacedClearing> &Clearings() const {
            return _clearings;
        }

        /** Appends every clearing to a document's level edits. */
        void ToDocument(std::vector<Shared::Game::LevelEditDefinition> &edits) const;

      private:
        std::vector<PlacedClearing> _clearings;
        std::uint64_t _nextId = 1;
    };
} // namespace KCDC::Features::Prop
