#pragma once

#include "shared/features/area/area_definition.h"

#include <span>

namespace KCDC::Features::Prop {
    struct AreaHandle {
        enum class Kind {
            None,
            Point,
            Edge,
            Radius,
            BoxFace,
            Height
        };
        Kind kind = Kind::None;
        int index = 0;

        bool operator==(const AreaHandle &) const = default;
    };

    /** The bottom of the drawn volume, including box tilt and polygon floor semantics. */
    inline float AreaGroundFloor(const Shared::Game::AreaShape &shape) {
        if (shape.type == Shared::Game::AreaShapeType::Shape) {
            return Shared::Game::AreaShape::Floor(shape.WorldPoints());
        }
        glm::vec3 min, max;
        shape.WorldBounds(shape.WorldPoints(), min, max);
        return min.z;
    }

    inline bool HasAreaHandle(const Shared::Game::AreaShape &shape, AreaHandle handle) {
        switch (handle.kind) {
        case AreaHandle::Kind::Point:
        case AreaHandle::Kind::Edge: return shape.type == Shared::Game::AreaShapeType::Shape && handle.index >= 0 && static_cast<std::size_t>(handle.index) < shape.points.size();
        case AreaHandle::Kind::Height: return shape.type == Shared::Game::AreaShapeType::Shape && !shape.points.empty();
        case AreaHandle::Kind::Radius: return shape.type == Shared::Game::AreaShapeType::Sphere && handle.index >= 0 && handle.index < 6;
        case AreaHandle::Kind::BoxFace: return shape.type == Shared::Game::AreaShapeType::Box && handle.index >= 0 && handle.index < 6;
        case AreaHandle::Kind::None: return false;
        }
        return false;
    }

    /** Insert along an edge, including the closing edge, without changing its outline. */
    inline bool SplitAreaEdge(Shared::Game::AreaShape &shape, int edge, float fraction = 0.5F) {
        if (shape.type != Shared::Game::AreaShapeType::Shape || edge < 0 || static_cast<std::size_t>(edge) >= shape.points.size() || shape.points.size() >= Shared::Game::AreaShape::kMaxPoints || !std::isfinite(fraction) || fraction <= 0.001F || fraction >= 0.999F) {
            return false;
        }
        const auto next  = (edge + 1) % shape.points.size();
        const auto point = glm::mix(shape.points[edge], shape.points[next], fraction);
        shape.points.insert(shape.points.begin() + edge + 1, point);
        return true;
    }

    enum class AreaPointDeletion {
        Unchanged,
        PointsRemoved,
        RemoveArea
    };

    /** Remove a corner or both edge endpoints; never leave an incomplete polygon. */
    inline AreaPointDeletion DeleteAreaHandlePoints(Shared::Game::AreaShape &shape, AreaHandle handle) {
        if ((handle.kind != AreaHandle::Kind::Point && handle.kind != AreaHandle::Kind::Edge) || !HasAreaHandle(shape, handle)) {
            return AreaPointDeletion::Unchanged;
        }
        const std::size_t count = handle.kind == AreaHandle::Kind::Edge ? 2 : 1;
        if (shape.points.size() < 3 + count) {
            return AreaPointDeletion::RemoveArea;
        }
        if (handle.kind == AreaHandle::Kind::Edge) {
            const int next = (handle.index + 1) % static_cast<int>(shape.points.size());
            shape.points.erase(shape.points.begin() + std::max(handle.index, next));
            shape.points.erase(shape.points.begin() + std::min(handle.index, next));
        }
        else {
            shape.points.erase(shape.points.begin() + handle.index);
        }
        return AreaPointDeletion::PointsRemoved;
    }

    inline float UniformAreaScale(const glm::vec3 &ratio) {
        float uniform = ratio.x;
        for (int axis = 1; axis < 3; ++axis) {
            if (std::fabs(ratio[axis] - 1.0F) > std::fabs(uniform - 1.0F)) {
                uniform = ratio[axis];
            }
        }
        return uniform;
    }

    inline void ScaleAreaShape(Shared::Game::AreaShape &shape, const glm::vec3 &ratio) {
        switch (shape.type) {
        case Shared::Game::AreaShapeType::Box:
            shape.boxMin *= ratio;
            shape.boxMax *= ratio;
            break;
        case Shared::Game::AreaShapeType::Shape:
            for (auto &point : shape.points) {
                point *= ratio;
            }
            shape.height *= ratio.z;
            break;
        case Shared::Game::AreaShapeType::Sphere: shape.radius *= UniformAreaScale(ratio); break;
        }
    }

    inline glm::vec3 AreaHandlePosition(const Shared::Game::AreaShape &shape, AreaHandle handle) {
        switch (handle.kind) {
        case AreaHandle::Kind::Point: return shape.position + shape.rotation * shape.points[handle.index];
        case AreaHandle::Kind::Edge: return shape.position + shape.rotation * ((shape.points[handle.index] + shape.points[(handle.index + 1) % shape.points.size()]) * 0.5F);
        case AreaHandle::Kind::Radius: {
            glm::vec3 offset(0.0F);
            offset[handle.index / 2] = handle.index % 2 == 0 ? -shape.radius : shape.radius;
            return shape.position + offset;
        }
        case AreaHandle::Kind::BoxFace: {
            glm::vec3 local         = (shape.boxMin + shape.boxMax) * 0.5F;
            local[handle.index / 2] = handle.index % 2 == 0 ? shape.boxMin[handle.index / 2] : shape.boxMax[handle.index / 2];
            return shape.position + shape.rotation * local;
        }
        case AreaHandle::Kind::Height: {
            const auto points = shape.WorldPoints();
            glm::vec3 centre(0.0F);
            for (const auto &point : points) {
                centre += point;
            }
            centre /= static_cast<float>(points.size());
            centre.z = Shared::Game::AreaShape::Floor(points) + shape.height;
            return centre;
        }
        case AreaHandle::Kind::None: return shape.position;
        }
        return shape.position;
    }

    /** Move in world space, retaining the area's pose and all unrelated corners. */
    inline bool MoveAreaHandle(Shared::Game::AreaShape &shape, AreaHandle handle, const glm::vec3 &target) {
        if (!HasAreaHandle(shape, handle) || !std::isfinite(target.x) || !std::isfinite(target.y) || !std::isfinite(target.z)) {
            return false;
        }
        auto edited           = shape;
        const glm::vec3 delta = glm::inverse(shape.rotation) * (target - AreaHandlePosition(shape, handle));
        const float maxExtent = std::nextafter(Shared::Game::AreaShape::kMaxExtent, 0.0F);
        switch (handle.kind) {
        case AreaHandle::Kind::Point: edited.points[handle.index] += delta; break;
        case AreaHandle::Kind::Edge:
            edited.points[handle.index] += delta;
            edited.points[(handle.index + 1) % shape.points.size()] += delta;
            break;
        case AreaHandle::Kind::Radius: edited.radius = std::clamp((target[handle.index / 2] - shape.position[handle.index / 2]) * (handle.index % 2 == 0 ? -1.0F : 1.0F), 0.01F, maxExtent); break;
        case AreaHandle::Kind::BoxFace: {
            const int axis         = handle.index / 2;
            const float coordinate = (glm::inverse(shape.rotation) * (target - shape.position))[axis];
            if (handle.index % 2 == 0) {
                edited.boxMin[axis] = std::clamp(coordinate, -maxExtent, shape.boxMax[axis] - 0.01F);
            }
            else {
                edited.boxMax[axis] = std::clamp(coordinate, shape.boxMin[axis] + 0.01F, maxExtent);
            }
            break;
        }
        case AreaHandle::Kind::Height: edited.height = std::clamp(target.z - Shared::Game::AreaShape::Floor(shape.WorldPoints()), 0.0F, maxExtent); break;
        case AreaHandle::Kind::None: return false;
        }
        if (!edited.IsValid()) {
            return false;
        }
        shape = std::move(edited);
        return true;
    }

    /** Copy the catalog's world polygon and vertical bounds into a custom area. */
    inline bool EditableLevelArea(const Shared::Game::LevelArea &source, std::span<const Shared::Game::LevelAreaPoint> points, const std::string &id, Shared::Game::AreaDefinition &result) {
        if (source.areaClass == Shared::Game::LevelAreaClass::AreaUnion || points.size() < 3 || points.size() > Shared::Game::AreaShape::kMaxPoints) {
            return false;
        }
        Shared::Game::AreaDefinition area;
        area.id               = id;
        area.name             = source.location[0] != '\0' ? source.location : source.name;
        area.shape.type       = Shared::Game::AreaShapeType::Shape;
        area.shape.position   = Shared::Game::LevelAreaCentre(source);
        area.shape.position.z = source.boundsMin.z;
        area.shape.height     = source.boundsMax.z - source.boundsMin.z;
        for (const auto &point : points) {
            area.shape.points.emplace_back(point.x - area.shape.position.x, point.y - area.shape.position.y, 0.0F);
        }
        // Native labels are semicolon separated, unlike the inspector's draft.
        std::string_view labels = source.labels;
        while (!labels.empty()) {
            const auto end   = labels.find(';');
            const auto label = labels.substr(0, end);
            if (!label.empty()) {
                area.labels.emplace_back(label);
            }
            labels.remove_prefix(end == std::string_view::npos ? labels.size() : end + 1);
        }
        if (!area.shape.IsValid()) {
            return false;
        }
        result = std::move(area);
        return true;
    }
} // namespace KCDC::Features::Prop
