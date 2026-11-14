#pragma once

#include "sdk/types.h"

#include <algorithm>
#include <cmath>

namespace KCDC::Features::Prop {
    /**
     * Which of the level's plants an object flattens: the ones rooted in its
     * footprint and no taller than `maxHeight`, so leaf litter, fallen
     * branches, tufts and bushes go and a tree stays.
     *
     * Only plants the level placed one by one are reached. The tall grass is
     * merged into the terrain detail: it is not an object and stays.
     */
    struct PlantFlattening {
        static constexpr float kMinMargin    = 0.0F;
        static constexpr float kMaxMargin    = 5.0F;
        static constexpr float kMinMaxHeight = 0.1F;
        static constexpr float kMaxMaxHeight = 6.0F;

        /** Metres added around the object's footprint. */
        float margin = 0.25F;

        /**
         * The tallest plant taken, by its own box. The level's plant boxes are
         * coarse -- litter lying flat reports a metre, a pile of branches up
         * to two and a half -- so the default clears what lies under a floor
         * and stops short of a tree.
         */
        float maxHeight = 3.0F;

        bool IsSane() const {
            return std::isfinite(margin) && std::isfinite(maxHeight) && margin >= kMinMargin && margin <= kMaxMargin && maxHeight >= kMinMaxHeight && maxHeight <= kMaxMaxHeight;
        }

        bool operator==(const PlantFlattening &other) const = default;
    };

    /** How far under an object's lowest point a plant may be rooted and still be under it: a prop set on a slope hangs over lower ground. */
    inline constexpr float kPlantSweepDepth = 2.0F;

    /**
     * The column an object sweeps for plants: its world box grown sideways
     * by the margin, from a little under its foot to its top.
     */
    inline SDK::AABB PlantSweepBox(const SDK::AABB &objectBounds, const PlantFlattening &options) {
        return SDK::AABB {SDK::Vec3 {objectBounds.min.x - options.margin, objectBounds.min.y - options.margin, objectBounds.min.z - kPlantSweepDepth}, SDK::Vec3 {objectBounds.max.x + options.margin, objectBounds.max.y + options.margin, objectBounds.max.z}};
    }

    /** Whether a plant rooted at `position`, `plantBounds` around it, is one the sweep flattens. A plant with no box has no height to judge and stays. */
    inline bool IsFlattenedPlant(const SDK::AABB &sweep, const SDK::Vec3 &position, const SDK::AABB &plantBounds, bool hasBounds, const PlantFlattening &options) {
        if (!hasBounds || position.x < sweep.min.x || position.x > sweep.max.x || position.y < sweep.min.y || position.y > sweep.max.y || position.z < sweep.min.z || position.z > sweep.max.z) {
            return false;
        }
        return plantBounds.max.z - plantBounds.min.z <= options.maxHeight;
    }
} // namespace KCDC::Features::Prop
