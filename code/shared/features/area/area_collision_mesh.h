#pragma once

#include "area_geometry.h"
#include "shared/pose_math.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <numeric>
#include <string>

namespace KCDC::Shared::Game {
    /** How an area's boundary is built. The side walls always are. A floor
     * or ceiling plate spans the whole interior, so each one is opt-in. */
    struct AreaCollisionOptions {
        static constexpr float kDefaultThickness = 0.2F;
        static constexpr float kMinThickness     = 0.05F;
        static constexpr float kMaxThickness     = 2.0F;

        bool floor      = false;
        bool ceiling    = false;
        float thickness = kDefaultThickness;

        bool IsValid() const {
            return std::isfinite(thickness) && thickness >= kMinThickness && thickness <= kMaxThickness;
        }

        static std::string ThicknessRangeError() {
            char message[96];
            std::snprintf(message, sizeof(message), "Collision wall thickness must be from %g to %g m.", kMinThickness, kMaxThickness);
            return message;
        }

        bool operator==(const AreaCollisionOptions &) const = default;
    };

    /** Closed, outward-wound wall pieces. Coordinates are relative to the area's
     * position; its rotation is already applied. Interiors remain empty. */
    struct AreaCollisionMesh {
        float thickness = AreaCollisionOptions::kDefaultThickness;
        std::vector<glm::vec3> vertices;
        std::vector<int> indices;

        void Wall(const glm::vec3 &a, const glm::vec3 &b, const glm::vec3 &c) {
            const glm::vec3 normal = glm::normalize(glm::cross(b - a, c - a)) * (thickness * 0.5F);
            const int first        = static_cast<int>(vertices.size());
            for (const auto &point : {a - normal, b - normal, c - normal, a + normal, b + normal, c + normal}) {
                vertices.push_back(point);
            }
            for (const int index : {0, 2, 1, 3, 4, 5, 0, 1, 4, 0, 4, 3, 1, 2, 5, 1, 5, 4, 2, 0, 3, 2, 3, 5}) {
                indices.push_back(first + index);
            }
        }

        void Quad(const glm::vec3 &a, const glm::vec3 &b, const glm::vec3 &c, const glm::vec3 &d) {
            Wall(a, b, c);
            Wall(a, c, d);
        }
    };

    namespace AreaCollisionGeometry {
        inline double Cross(const glm::vec3 &a, const glm::vec3 &b, const glm::vec3 &c) {
            return (double(b.x) - a.x) * (double(c.y) - a.y) - (double(b.y) - a.y) * (double(c.x) - a.x);
        }

        inline bool OnSegment(const glm::vec3 &a, const glm::vec3 &b, const glm::vec3 &point) {
            return std::abs(Cross(a, b, point)) < 1e-8 && point.x >= std::min(a.x, b.x) && point.x <= std::max(a.x, b.x) && point.y >= std::min(a.y, b.y) && point.y <= std::max(a.y, b.y);
        }

        inline bool Intersects(const glm::vec3 &a, const glm::vec3 &b, const glm::vec3 &c, const glm::vec3 &d) {
            const double abC = Cross(a, b, c), abD = Cross(a, b, d), cdA = Cross(c, d, a), cdB = Cross(c, d, b);
            return ((abC > 0) != (abD > 0) && (cdA > 0) != (cdB > 0)) || OnSegment(a, b, c) || OnSegment(a, b, d) || OnSegment(c, d, a) || OnSegment(c, d, b);
        }

        /** Ear clipping supports concave simple polygons in either winding. */
        inline bool Triangulate(std::vector<glm::vec3> &points, std::vector<std::array<std::size_t, 3>> &triangles) {
            for (std::size_t i = 0; i < points.size(); ++i) {
                const std::size_t next = (i + 1) % points.size();
                if (glm::length(glm::vec2(points[i] - points[next])) < 0.001F) {
                    return false;
                }
                for (std::size_t j = i + 1; j < points.size(); ++j) {
                    const std::size_t after = (j + 1) % points.size();
                    if (j != next && after != i && Intersects(points[i], points[next], points[j], points[after])) {
                        return false;
                    }
                }
            }
            // A split edge is legal editor geometry; remove its redundant vertex.
            for (std::size_t i = 0; i < points.size() && points.size() > 3;) {
                if (std::abs(Cross(points[(i + points.size() - 1) % points.size()], points[i], points[(i + 1) % points.size()])) < 1e-8) {
                    points.erase(points.begin() + i);
                    i = 0;
                }
                else {
                    ++i;
                }
            }
            double area = 0;
            for (std::size_t i = 0; i < points.size(); ++i) {
                area += double(points[i].x) * points[(i + 1) % points.size()].y - double(points[i].y) * points[(i + 1) % points.size()].x;
            }
            if (std::abs(area) < 1e-6) {
                return false;
            }
            const double winding = area > 0 ? 1 : -1;
            std::vector<std::size_t> remaining(points.size());
            std::iota(remaining.begin(), remaining.end(), 0);
            while (remaining.size() > 3) {
                bool clipped = false;
                for (std::size_t i = 0; i < remaining.size(); ++i) {
                    const auto a = remaining[(i + remaining.size() - 1) % remaining.size()], b = remaining[i], c = remaining[(i + 1) % remaining.size()];
                    if (Cross(points[a], points[b], points[c]) * winding <= 1e-8) {
                        continue;
                    }
                    bool occupied = false;
                    for (const auto p : remaining) {
                        if (p != a && p != b && p != c && Cross(points[a], points[b], points[p]) * winding >= -1e-8 && Cross(points[b], points[c], points[p]) * winding >= -1e-8 && Cross(points[c], points[a], points[p]) * winding >= -1e-8) {
                            occupied = true;
                            break;
                        }
                    }
                    if (occupied) {
                        continue;
                    }
                    triangles.push_back({a, b, c});
                    remaining.erase(remaining.begin() + i);
                    clipped = true;
                    break;
                }
                if (!clipped) {
                    return false;
                }
            }
            triangles.push_back({remaining[0], remaining[1], remaining[2]});
            return true;
        }
    } // namespace AreaCollisionGeometry

    /** Builds the boundary `options` describes. A sphere is one closed shell, so its floor and ceiling flags do nothing. */
    inline bool BuildAreaCollisionMesh(const AreaShape &shape, const AreaCollisionOptions &options, AreaCollisionMesh &mesh, std::string &error) {
        mesh            = {};
        const auto fail = [&](const std::string &message) {
            error = message;
            mesh  = {};
            return false;
        };
        if (!options.IsValid()) {
            return fail(AreaCollisionOptions::ThicknessRangeError());
        }
        char thickness[32];
        std::snprintf(thickness, sizeof(thickness), "%g m", options.thickness);
        mesh.thickness = options.thickness;
        if (!shape.IsValid() || !IsUsableRotation(shape.rotation) || std::abs(glm::dot(shape.rotation, shape.rotation) - 1.0F) > 0.001F) {
            return fail("Collision needs a valid shape and a normalized rotation.");
        }
        switch (shape.type) {
        case AreaShapeType::Box: {
            if (glm::any(glm::lessThanEqual(shape.boxMax - shape.boxMin, glm::vec3(options.thickness)))) {
                return fail(std::string("Collision box dimensions must exceed the ") + thickness + " wall thickness.");
            }
            std::array<glm::vec3, 8> corners;
            for (int i = 0; i < 8; ++i) {
                corners[i] = shape.rotation * glm::vec3((i & 1) ? shape.boxMax.x : shape.boxMin.x, (i & 2) ? shape.boxMax.y : shape.boxMin.y, (i & 4) ? shape.boxMax.z : shape.boxMin.z);
            }
            // The box's own bottom and top come first: the floor at `boxMin.z`, the ceiling at `boxMax.z`.
            const std::array<std::array<int, 4>, 6> faces {{{0, 1, 3, 2}, {4, 6, 7, 5}, {0, 4, 5, 1}, {2, 3, 7, 6}, {0, 2, 6, 4}, {1, 5, 7, 3}}};
            for (std::size_t index = 0; index < faces.size(); ++index) {
                if ((index == 0 && !options.floor) || (index == 1 && !options.ceiling)) {
                    continue;
                }
                const auto &face = faces[index];
                mesh.Quad(corners[face[0]], corners[face[1]], corners[face[2]], corners[face[3]]);
            }
            break;
        }
        case AreaShapeType::Shape: {
            if (shape.height <= options.thickness) {
                return fail(std::string("Collision polygons need a finite height greater than the ") + thickness + " wall thickness.");
            }
            auto points       = shape.WorldPoints();
            const float floor = AreaShape::Floor(points);
            for (auto &point : points) {
                point = glm::vec3(point.x - shape.position.x, point.y - shape.position.y, floor - shape.position.z);
            }
            std::vector<std::array<std::size_t, 3>> triangles;
            if (!AreaCollisionGeometry::Triangulate(points, triangles)) {
                return fail("Collision polygons must have a simple, non-degenerate outline without crossing or duplicate edges.");
            }
            // The outline is triangulated even without a floor or ceiling: it
            // is also the check that the outline is simple.
            const glm::vec3 rise(0, 0, shape.height);
            for (const auto &triangle : triangles) {
                if (options.floor) {
                    mesh.Wall(points[triangle[0]], points[triangle[1]], points[triangle[2]]);
                }
                if (options.ceiling) {
                    mesh.Wall(points[triangle[0]] + rise, points[triangle[1]] + rise, points[triangle[2]] + rise);
                }
            }
            for (std::size_t i = 0; i < points.size(); ++i) {
                const auto &a = points[i], &b = points[(i + 1) % points.size()];
                mesh.Quad(a, b, b + rise, a + rise);
            }
            break;
        }
        case AreaShapeType::Sphere: {
            if (shape.radius <= options.thickness) {
                return fail(std::string("Collision sphere radius must exceed the ") + thickness + " wall thickness.");
            }
            // Keep ordinary spheres within 5 cm of the drawn surface. The
            // native island map packs triangle links into 15 bits (0x7FFF is
            // its sentinel), even though vertex indices have 16 bits. At 32
            // rings the closed wall pieces produce 31,744 triangles.
            const int rings  = std::clamp(static_cast<int>(std::ceil(3.141592654 / std::acos(std::max(-1.0, 1.0 - 0.05 / shape.radius)))), 12, 32);
            const int slices = rings * 2;
            const auto point = [&](int ring, int slice) {
                const double latitude = 3.141592654 * ring / rings, longitude = 6.283185307 * slice / slices;
                return shape.radius * glm::vec3(std::sin(latitude) * std::cos(longitude), std::sin(latitude) * std::sin(longitude), std::cos(latitude));
            };
            for (int ring = 0; ring < rings; ++ring) {
                for (int slice = 0; slice < slices; ++slice) {
                    const auto a = point(ring, slice), b = point(ring, slice + 1), c = point(ring + 1, slice + 1), d = point(ring + 1, slice);
                    if (ring > 0) {
                        mesh.Wall(a, b, c);
                    }
                    if (ring + 1 < rings) {
                        mesh.Wall(a, c, d);
                    }
                }
            }
            break;
        }
        }
        // The native mesh consumes float vertices and computes float normals.
        // A polygon can have nonzero double area yet collapse at this precision.
        for (std::size_t i = 0; i < mesh.indices.size(); i += 3) {
            const auto normal         = glm::cross(mesh.vertices[mesh.indices[i + 1]] - mesh.vertices[mesh.indices[i]], mesh.vertices[mesh.indices[i + 2]] - mesh.vertices[mesh.indices[i]]);
            const float lengthSquared = glm::dot(normal, normal);
            if (!std::isfinite(lengthSquared) || lengthSquared <= 0) {
                return fail("Collision geometry is too thin or numerically degenerate.");
            }
        }
        return true;
    }
} // namespace KCDC::Shared::Game
