#pragma once

#include "shared/game/area_catalog.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace KCDC::Shared::Game {
    /**
     * The engine's two point-in-polygon tests, and the three shapes a script
     * or the World Builder can draw.
     *
     * Tests follow `CArea::CalcPointWithinNonCached` for `AreaBox`,
     * `AreaShape` and `AreaSphere`, so server containment matches the game.
     */

    /**
     * `wh::xgenaimodule::IsPointInPolygon2D` (0x1805CD140): even-odd, edge
     * (i, i+1) wrapping to 0, an edge counting when the point's y lies in
     * [min, max) of its ends and the crossing is right of the point.
     */
    inline bool IsPointInTriggerAreaPolygon(const LevelAreaPoint *points, std::size_t count, float x, float y) {
        bool inside = false;
        for (std::size_t index = 0; index < count; ++index) {
            const LevelAreaPoint &a = points[index];
            const LevelAreaPoint &b = points[index + 1 == count ? 0 : index + 1];
            const bool straddles    = (b.y <= y && y < a.y) || (a.y <= y && y < b.y);
            if (straddles && (y - b.y) * (a.x - b.x) / (a.y - b.y) + b.x > x) {
                inside = !inside;
            }
        }
        return inside;
    }

    /**
     * `CArea`'s shape test: a segment counts when it is not horizontal, the
     * point's y lies in (min, max] of its ends, the point is not right of it
     * (`PointOutBBox2DVertical`), and the point is left of the line -- a
     * vertical segment always is, once the box test has passed.
     */
    template <typename Point>
    inline bool IsPointInCryShapePolygon(const Point *points, std::size_t count, float x, float y) {
        std::size_t crossings = 0;
        for (std::size_t index = 0; index < count; ++index) {
            const Point &a = points[index];
            const Point &b = points[index + 1 == count ? 0 : index + 1];
            if (a.y == b.y) {
                continue;
            }
            if (y <= std::min(a.y, b.y) || y > std::max(a.y, b.y) || x > std::max(a.x, b.x)) {
                continue;
            }
            if (a.x == b.x) {
                ++crossings;
                continue;
            }
            const float slope     = (b.y - a.y) / (b.x - a.x);
            const float intercept = a.y - slope * a.x;
            if (x < (y - intercept) / slope) {
                ++crossings;
            }
        }
        return (crossings & 1U) != 0;
    }

    inline glm::vec3 ToVec3(const LevelAreaPoint &point) {
        return glm::vec3(point.x, point.y, point.z);
    }

    /** How far a point is from a box, 0 inside it. */
    inline float DistanceToBox(const glm::vec3 &point, const glm::vec3 &min, const glm::vec3 &max) {
        return glm::length(point - glm::clamp(point, min, max));
    }

    /** Where a level area is, for a script or a marker: the middle of its box. */
    inline glm::vec3 LevelAreaCentre(const LevelArea &area) {
        return (ToVec3(area.boundsMin) + ToVec3(area.boundsMax)) * 0.5F;
    }

    inline bool IsInsideBounds(const LevelAreaPoint &min, const LevelAreaPoint &max, const glm::vec3 &point) {
        return point.x >= min.x && point.y >= min.y && point.z >= min.z && point.x <= max.x && point.y <= max.y && point.z <= max.z;
    }

    /**
     * Whether a level area other than a union contains a point. A union has
     * no polygon: it contains whatever one of its members does, which the
     * caller resolves, having the members indexed.
     */
    inline bool LevelAreaContains(const LevelArea &area, const glm::vec3 &point) {
        if (area.areaClass == LevelAreaClass::AreaUnion || !IsInsideBounds(area.boundsMin, area.boundsMax, point)) {
            return false;
        }
        const LevelAreaPoint *points = g_LevelAreaPoints.data() + area.firstPoint;
        if (area.areaClass == LevelAreaClass::TriggerArea) {
            return IsPointInTriggerAreaPolygon(points, area.pointCount, point.x, point.y);
        }
        return IsPointInCryShapePolygon(points, area.pointCount, point.x, point.y);
    }

    /** The shapes a script or the World Builder can draw: CryEngine's AreaBox, AreaShape and AreaSphere. */
    enum class AreaShapeType : std::uint8_t {
        Box,
        Shape,
        Sphere,
    };

    inline const char *AreaShapeTypeName(AreaShapeType type) {
        switch (type) {
        case AreaShapeType::Box: return "box";
        case AreaShapeType::Shape: return "shape";
        case AreaShapeType::Sphere: return "sphere";
        }
        return "box";
    }

    inline bool ParseAreaShapeType(std::string_view name, AreaShapeType &type) {
        if (name == "box") {
            type = AreaShapeType::Box;
        }
        else if (name == "shape") {
            type = AreaShapeType::Shape;
        }
        else if (name == "sphere") {
            type = AreaShapeType::Sphere;
        }
        else {
            return false;
        }
        return true;
    }

    /**
     * One authored area, placed the way the engine places its own: a pose,
     * and a shape in the pose's space.
     *
     * - box: `boxMin`/`boxMax` in local space, as `AreaBox`'s `BoxMin`/`BoxMax`.
     * - shape: `points` in local space (only x/y/z offsets from `position`,
     *   turned by `rotation`), closed; the floor is the lowest world point and
     *   `height` above it is the ceiling, 0 for none, as `AreaShape`.
     * - sphere: `radius` around `position`, as `AreaSphere`.
     */
    struct AreaShape {
        static constexpr float kMaxExtent      = 16384.0F;
        static constexpr std::size_t kMaxPoints = 256;

        AreaShapeType type = AreaShapeType::Box;
        glm::vec3 position {0.0F};
        glm::quat rotation {1.0F, 0.0F, 0.0F, 0.0F};
        glm::vec3 boxMin {-1.0F};
        glm::vec3 boxMax {1.0F};
        std::vector<glm::vec3> points;
        float height = 0.0F;
        float radius = 1.0F;

        /** A shape's floor, its lowest world corner, as `CArea` takes `m_origin`. */
        static float Floor(const std::vector<glm::vec3> &worldPoints) {
            float floor = worldPoints.empty() ? 0.0F : worldPoints.front().z;
            for (const glm::vec3 &corner : worldPoints) {
                floor = std::min(floor, corner.z);
            }
            return floor;
        }

        /** The shape's points in world space. */
        std::vector<glm::vec3> WorldPoints() const {
            std::vector<glm::vec3> world;
            world.reserve(points.size());
            for (const glm::vec3 &local : points) {
                world.push_back(position + rotation * local);
            }
            return world;
        }

        /** Whether this is a shape the engine could build: a real box, a closed polygon, a positive radius. */
        bool IsValid() const {
            const auto finite = [](const glm::vec3 &value) {
                return glm::all(glm::lessThan(glm::abs(value), glm::vec3(kMaxExtent)));
            };
            if (!finite(position)) {
                return false;
            }
            switch (type) {
            case AreaShapeType::Box: return finite(boxMin) && finite(boxMax) && glm::all(glm::lessThan(boxMin, boxMax));
            case AreaShapeType::Shape:
                return points.size() >= 3 && points.size() <= kMaxPoints && height >= 0.0F && height < kMaxExtent && std::all_of(points.begin(), points.end(), finite);
            case AreaShapeType::Sphere: return radius > 0.0F && radius < kMaxExtent;
            }
            return false;
        }

        /** `CArea::CalcPointWithinNonCached` for this shape's type. */
        bool Contains(const glm::vec3 &point, const std::vector<glm::vec3> &worldPoints) const {
            switch (type) {
            case AreaShapeType::Box: {
                const glm::vec3 local = glm::inverse(rotation) * (point - position);
                return glm::all(glm::greaterThanEqual(local, boxMin)) && glm::all(glm::lessThanEqual(local, boxMax));
            }
            case AreaShapeType::Shape: {
                if (worldPoints.size() < 3) {
                    return false;
                }
                if (height > 0.0F) {
                    const float floor = Floor(worldPoints);
                    if (point.z < floor || point.z > floor + height) {
                        return false;
                    }
                }
                return IsPointInCryShapePolygon(worldPoints.data(), worldPoints.size(), point.x, point.y);
            }
            case AreaShapeType::Sphere: {
                const glm::vec3 offset = point - position;
                return glm::dot(offset, offset) <= radius * radius;
            }
            }
            return false;
        }

        /** The world box around the shape, for indexing; an open-topped shape reaches kMaxExtent. */
        void WorldBounds(const std::vector<glm::vec3> &worldPoints, glm::vec3 &min, glm::vec3 &max) const {
            switch (type) {
            case AreaShapeType::Box: {
                min = glm::vec3(kMaxExtent);
                max = glm::vec3(-kMaxExtent);
                for (int corner = 0; corner < 8; ++corner) {
                    const glm::vec3 local {(corner & 1) != 0 ? boxMax.x : boxMin.x, (corner & 2) != 0 ? boxMax.y : boxMin.y, (corner & 4) != 0 ? boxMax.z : boxMin.z};
                    const glm::vec3 world = position + rotation * local;
                    min                   = glm::min(min, world);
                    max                   = glm::max(max, world);
                }
                return;
            }
            case AreaShapeType::Shape: {
                min = glm::vec3(kMaxExtent);
                max = glm::vec3(-kMaxExtent);
                for (const glm::vec3 &corner : worldPoints) {
                    min = glm::min(min, corner);
                    max = glm::max(max, corner);
                }
                // min.z is already the floor.
                max.z = height > 0.0F ? min.z + height : kMaxExtent;
                if (height <= 0.0F) {
                    min.z = -kMaxExtent;
                }
                return;
            }
            case AreaShapeType::Sphere:
                min = position - glm::vec3(radius);
                max = position + glm::vec3(radius);
                return;
            }
        }
    };
} // namespace KCDC::Shared::Game
