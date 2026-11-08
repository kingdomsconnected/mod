#pragma once

#include "shared/features/area/area_wireframe.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace KCDC::Features::Gizmos {
    struct Segment {
        glm::vec3 from;
        glm::vec3 to;
        float weight = 1.0F;
        std::optional<std::uint32_t> color;
    };

    struct Triangle {
        glm::vec3 a, b, c;
    };

    struct Drawing {
        std::vector<Segment> segments;
        std::vector<Triangle> triangles;
        bool depthTest          = true;
        std::uint32_t color     = 0xFF6EE678; // 0xAARRGGBB, like player nametags.
        std::uint32_t fillColor = 0;
        float width             = 1.5F;
        std::int64_t durationMs = 0;
        std::optional<std::chrono::steady_clock::time_point> expiresAt;

        // Drawn at position + rotation * p for every authored point p, so a
        // moving drawing is re-placed instead of rebuilt.
        glm::vec3 position {0.0F};
        glm::quat rotation {1.0F, 0.0F, 0.0F, 0.0F};

        // A sphere around the authored geometry; the renderer skips drawings
        // whose sphere is outside the view.
        glm::vec3 boundsCentre {0.0F};
        float boundsRadius = 0.0F;

        std::size_t Cost() const {
            return segments.size() + triangles.size() * 3;
        }
        bool Filled() const {
            return (fillColor >> 24) != 0;
        }
        bool Placed() const {
            return position != glm::vec3(0.0F) || rotation != glm::quat(1.0F, 0.0F, 0.0F, 0.0F);
        }
        glm::vec3 Place(const glm::vec3 &point) const {
            return position + rotation * point;
        }

        void ComputeBounds() {
            glm::vec3 low(std::numeric_limits<float>::max());
            glm::vec3 high(std::numeric_limits<float>::lowest());
            const auto grow = [&](const glm::vec3 &point) {
                low  = glm::min(low, point);
                high = glm::max(high, point);
            };
            for (const Segment &segment : segments) {
                grow(segment.from);
                grow(segment.to);
            }
            for (const Triangle &triangle : triangles) {
                grow(triangle.a);
                grow(triangle.b);
                grow(triangle.c);
            }
            if (segments.empty() && triangles.empty()) {
                boundsCentre = glm::vec3(0.0F);
                boundsRadius = 0.0F;
                return;
            }
            boundsCentre = (low + high) * 0.5F;
            boundsRadius = glm::length(high - low) * 0.5F;
        }
    };

    inline glm::vec3 Direction(const glm::vec3 &value) {
        const float length = glm::length(value);
        if (length < 1e-5F) {
            throw std::invalid_argument("direction or endpoint separation must be non-zero");
        }
        return value / length;
    }

    inline std::pair<glm::vec3, glm::vec3> Basis(const glm::vec3 &normal) {
        const glm::vec3 axis  = Direction(normal);
        const glm::vec3 right = glm::normalize(glm::cross(axis, std::abs(axis.z) < 0.9F ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0)));
        return {right, glm::cross(axis, right)};
    }

    inline std::vector<glm::vec3> Ring(const glm::vec3 &center, const glm::vec3 &normal, float radius, int count) {
        const auto [right, up] = Basis(normal);
        std::vector<glm::vec3> points;
        points.reserve(count);
        for (int i = 0; i < count; ++i) {
            const float angle = i * 6.28318530718F / count;
            points.push_back(center + radius * (right * std::cos(angle) + up * std::sin(angle)));
        }
        return points;
    }

    inline void Circle(Drawing &drawing, const glm::vec3 &center, const glm::vec3 &normal, float radius, int count) {
        const auto points = Ring(center, normal, radius, count);
        for (int i = 0; i < count; ++i) {
            const auto &a = points[i];
            const auto &b = points[(i + 1) % count];
            drawing.segments.push_back({a, b});
            if (drawing.Filled()) {
                drawing.triangles.push_back({center, a, b});
            }
        }
    }

    /** Revolves a sequence of (axial distance, radius) pairs around an axis. */
    inline void Lathe(Drawing &drawing, const glm::vec3 &origin, const glm::vec3 &axis, const std::vector<glm::vec2> &profile, int count, bool wire = true) {
        std::vector<glm::vec3> previous;
        for (const auto &row : profile) {
            const auto points = Ring(origin + axis * row.x, axis, row.y, count);
            for (int i = 0; i < count; ++i) {
                const int next = (i + 1) % count;
                if (wire && row.y > 0) {
                    drawing.segments.push_back({points[i], points[next]});
                }
                if (!previous.empty()) {
                    if (wire && i % std::max(1, count / 4) == 0) {
                        drawing.segments.push_back({previous[i], points[i]});
                    }
                    if (drawing.Filled()) {
                        if (glm::length(previous[i] - previous[next]) > 1e-6F) {
                            drawing.triangles.push_back({previous[i], previous[next], points[i]});
                        }
                        if (row.y > 0) {
                            drawing.triangles.push_back({previous[next], points[next], points[i]});
                        }
                    }
                }
            }
            previous = points;
        }
    }

    inline void Capsule(Drawing &drawing, const glm::vec3 &from, const glm::vec3 &to, float radius, int count) {
        const float length   = glm::length(to - from);
        const glm::vec3 axis = length < 1e-5F ? glm::vec3(0, 0, 1) : (to - from) / length;
        std::vector<glm::vec2> profile;
        constexpr int hemisphereSteps = 6;
        for (int i = 0; i <= hemisphereSteps; ++i) {
            const float angle = -1.57079632679F + i * 1.57079632679F / hemisphereSteps;
            profile.push_back({radius * std::sin(angle), i == 0 ? 0 : radius * std::cos(angle)});
        }
        if (length > 1e-5F) {
            profile.push_back({length, radius});
        }
        for (int i = 1; i <= hemisphereSteps; ++i) {
            const float angle = i * 1.57079632679F / hemisphereSteps;
            profile.push_back({length + radius * std::sin(angle), i == hemisphereSteps ? 0 : radius * std::cos(angle)});
        }
        Lathe(drawing, from, axis, profile, count);
    }

    inline void ConeOrCylinder(Drawing &drawing, const glm::vec3 &from, const glm::vec3 &to, float radius, int count, bool cone) {
        const glm::vec3 axis = Direction(to - from);
        const float length   = glm::length(to - from);
        Lathe(drawing, from, axis, cone ? std::vector<glm::vec2> {{0, 0}, {0, radius}, {length, 0}} : std::vector<glm::vec2> {{0, 0}, {0, radius}, {length, radius}, {length, 0}}, count);
    }

    inline void Arrow(Drawing &drawing, const glm::vec3 &from, const glm::vec3 &to, float headLength) {
        const glm::vec3 axis = Direction(to - from);
        if (headLength <= 0 || headLength > glm::length(to - from)) {
            throw std::invalid_argument("arrow headLength must fit within the arrow");
        }
        drawing.segments.push_back({from, to});
        ConeOrCylinder(drawing, to - axis * headLength, to, headLength * 0.35F, 16, true);
    }

    inline void Area(Drawing &drawing, const Shared::Game::AreaShape &shape) {
        Shared::Game::AreaWireframe(shape, [&](const glm::vec3 &a, const glm::vec3 &b, float weight) {
            drawing.segments.push_back({a, b, weight});
        });
        if (!drawing.Filled()) {
            return;
        }
        if (shape.type == Shared::Game::AreaShapeType::Sphere) {
            std::vector<glm::vec2> profile;
            for (int i = 0; i <= 12; ++i) {
                const float angle = -1.57079632679F + i * 3.14159265359F / 12;
                profile.push_back({shape.radius * std::sin(angle), (i == 0 || i == 12) ? 0 : shape.radius * std::cos(angle)});
            }
            Lathe(drawing, shape.position, {0, 0, 1}, profile, 24, false);
        }
        else if (shape.type == Shared::Game::AreaShapeType::Box) {
            glm::vec3 corners[8];
            for (int i = 0; i < 8; ++i) {
                corners[i] = shape.position + shape.rotation * glm::vec3((i & 1) ? shape.boxMax.x : shape.boxMin.x, (i & 2) ? shape.boxMax.y : shape.boxMin.y, (i & 4) ? shape.boxMax.z : shape.boxMin.z);
            }
            constexpr int faces[6][4] {{0, 1, 3, 2}, {4, 6, 7, 5}, {0, 4, 5, 1}, {2, 3, 7, 6}, {0, 2, 6, 4}, {1, 5, 7, 3}};
            for (const auto &face : faces) {
                drawing.triangles.push_back({corners[face[0]], corners[face[1]], corners[face[2]]});
                drawing.triangles.push_back({corners[face[0]], corners[face[2]], corners[face[3]]});
            }
        }
        else {
            throw std::invalid_argument("polygon area fill requires explicit triangles; use type 'mesh'");
        }
    }

} // namespace KCDC::Features::Gizmos
