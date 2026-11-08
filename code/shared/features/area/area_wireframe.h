#pragma once

#include "area_geometry.h"

#include <cmath>

namespace KCDC::Shared::Game {
    /** The World Builder and client gizmos draw the same area edges. */
    template <typename Segment>
    void AreaWireframe(const AreaShape &shape, Segment &&segment) {
        switch (shape.type) {
        case AreaShapeType::Box: {
            glm::vec3 corners[8];
            for (int corner = 0; corner < 8; ++corner) {
                const glm::vec3 local {(corner & 1) != 0 ? shape.boxMax.x : shape.boxMin.x, (corner & 2) != 0 ? shape.boxMax.y : shape.boxMin.y, (corner & 4) != 0 ? shape.boxMax.z : shape.boxMin.z};
                corners[corner] = shape.position + shape.rotation * local;
            }
            constexpr int edges[12][2] {{0, 1}, {0, 2}, {0, 4}, {1, 3}, {1, 5}, {2, 3}, {2, 6}, {3, 7}, {4, 5}, {4, 6}, {5, 7}, {6, 7}};
            for (const auto &edge : edges) {
                segment(corners[edge[0]], corners[edge[1]], 1.0F);
            }
            break;
        }
        case AreaShapeType::Shape: {
            const std::vector<glm::vec3> points = shape.WorldPoints();
            const float floor                   = AreaShape::Floor(points);
            for (std::size_t index = 0; index < points.size(); ++index) {
                const glm::vec3 &a = points[index];
                const glm::vec3 &b = points[(index + 1) % points.size()];
                if (shape.height <= 0.0F) {
                    segment(a, b, 1.0F);
                }
                else {
                    segment(glm::vec3(a.x, a.y, floor), glm::vec3(b.x, b.y, floor), 1.0F);
                    segment(glm::vec3(a.x, a.y, floor + shape.height), glm::vec3(b.x, b.y, floor + shape.height), 1.0F);
                    segment(glm::vec3(a.x, a.y, floor), glm::vec3(a.x, a.y, floor + shape.height), 0.6F);
                }
            }
            break;
        }
        case AreaShapeType::Sphere: {
            constexpr int segments = 32;
            for (int axis = 0; axis < 3; ++axis) {
                for (int step = 0; step < segments; ++step) {
                    const auto at = [&](int index) {
                        const float angle = static_cast<float>(index) / segments * 6.2831853F;
                        const glm::vec2 circle(std::cos(angle) * shape.radius, std::sin(angle) * shape.radius);
                        return shape.position + (axis == 0 ? glm::vec3(circle.x, circle.y, 0.0F) : axis == 1 ? glm::vec3(circle.x, 0.0F, circle.y) : glm::vec3(0.0F, circle.x, circle.y));
                    };
                    segment(at(step), at(step + 1), 1.0F);
                }
            }
            break;
        }
        }
    }
} // namespace KCDC::Shared::Game
