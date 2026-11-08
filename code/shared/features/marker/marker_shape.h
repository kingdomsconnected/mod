#pragma once

#include <cstdint>
#include <string_view>

namespace KCDC::Shared::Entities {
    /**
     * What a marker is made of.
     *
     * `Decal` is a decal render node and lies on whatever surface is under it.
     * Every other shape is a mesh on a carrier entity, which can float and be
     * seen from below a ridge.
     *
     * The numbering is the wire format. Append, never reorder.
     */
    enum class MarkerShape : std::uint8_t {
        Decal    = 0,
        Cylinder = 1,
        Sphere   = 2,
        Chevron  = 3,
        Cube     = 4,
        Plane    = 5,
    };

    inline constexpr std::uint8_t kMarkerShapeCount = 6;

    inline bool IsMarkerShape(std::uint8_t raw) {
        return raw < kMarkerShapeCount;
    }

    inline bool IsMeshShape(MarkerShape shape) {
        return shape != MarkerShape::Decal;
    }

    /** The name a script and a command spell this shape as. */
    inline constexpr std::string_view MarkerShapeName(MarkerShape shape) {
        switch (shape) {
        case MarkerShape::Cylinder: return "cylinder";
        case MarkerShape::Sphere: return "sphere";
        case MarkerShape::Chevron: return "chevron";
        case MarkerShape::Cube: return "cube";
        case MarkerShape::Plane: return "plane";
        case MarkerShape::Decal: break;
        }
        return "decal";
    }

    /** The shape a name means, or `Decal` when it names nothing -- which is also the default. */
    inline MarkerShape MarkerShapeFromName(std::string_view name) {
        for (std::uint8_t raw = 0; raw < kMarkerShapeCount; ++raw) {
            const MarkerShape shape = static_cast<MarkerShape>(raw);
            if (MarkerShapeName(shape) == name) {
                return shape;
            }
        }
        return MarkerShape::Decal;
    }
} // namespace KCDC::Shared::Entities
