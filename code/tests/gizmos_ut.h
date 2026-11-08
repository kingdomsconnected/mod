#pragma once

#include "features/gizmos/gizmos_service.h"
#include "shared/features/area/area_wireframe.h"

MODULE(gizmos, {
    IT("keeps names private to a resource and releases its drawing budget", {
        KCDC::Features::Gizmos::Drawings drawings;
        KCDC::Features::Gizmos::Drawing line;
        line.segments.push_back({glm::vec3(0), glm::vec3(1)});
        EQUALS(drawings.Set("first", "zone", line), true);
        EQUALS(drawings.Set("second", "zone", line), true);
        EQUALS(drawings.Remove("other", "zone"), false);
        drawings.Clear("first");
        EQUALS(drawings.Entries().size(), std::size_t(1));
        EQUALS(drawings.Remove("second", "zone"), true);
        EQUALS(drawings.Remove("second", "zone"), false);
        for (std::size_t index = 0; index < KCDC::Features::Gizmos::Drawings::kMaxPerResource; ++index) {
            EQUALS(drawings.Set("first", std::to_string(index), line), true);
        }
        EQUALS(drawings.Set("first", "overflow", line), false);
        EQUALS(drawings.Set("first", "0", line), true);
        EQUALS(drawings.Set("second", "0", line), true);
        drawings.Reset();
        EQUALS(drawings.Entries().empty(), true);
        line.segments.resize(KCDC::Features::Gizmos::Drawings::kMaxGeometryUnits);
        EQUALS(drawings.Set("first", "all", line), true);
        line.segments.push_back({glm::vec3(0), glm::vec3(1)});
        EQUALS(drawings.Set("first", "all", line), false);
        EQUALS(drawings.Entries().begin()->second.segments.size(), KCDC::Features::Gizmos::Drawings::kMaxGeometryUnits);
        drawings.Clear("first");
        line.segments.resize(1);
        EQUALS(drawings.Set("second", "new", line), true);
    });

    IT("expires drawings, renews replacements and counts filled geometry", {
        KCDC::Features::Gizmos::Drawings drawings;
        KCDC::Features::Gizmos::Drawing drawing;
        EQUALS(drawing.depthTest, true);
        drawing.triangles.push_back({glm::vec3(0), glm::vec3(1, 0, 0), glm::vec3(0, 1, 0)});
        drawing.durationMs = 100;
        const auto start   = std::chrono::steady_clock::time_point {};
        EQUALS(drawings.Set("test", "shape", drawing, start), true);
        drawings.Expire(start + std::chrono::milliseconds(99));
        EQUALS(drawings.Entries().size(), std::size_t(1));
        EQUALS(drawings.Set("test", "shape", drawing, start + std::chrono::milliseconds(90)), true);
        drawings.Expire(start + std::chrono::milliseconds(100));
        EQUALS(drawings.Entries().size(), std::size_t(1));
        drawings.Expire(start + std::chrono::milliseconds(190));
        EQUALS(drawings.Entries().empty(), true);
        drawing.durationMs = 0;
        drawing.triangles.resize(5461);
        drawing.segments.resize(1);
        EQUALS(drawings.Set("test", "full", drawing, start), true);
        drawing.segments.resize(2);
        EQUALS(drawings.Set("test", "full", drawing, start), false);
        EQUALS(drawings.Entries().begin()->second.Cost(), std::size_t(16384));
        drawings.Expire(start + std::chrono::hours(100));
        EQUALS(drawings.Entries().size(), std::size_t(1));
    });

    IT("builds filled circles and capsules on arbitrary axes without degenerate pole faces", {
        KCDC::Features::Gizmos::Drawing circle;
        circle.fillColor = 0x804488FF;
        KCDC::Features::Gizmos::Circle(circle, {2, 3, 4}, {1, 0, 0}, 2, 24);
        EQUALS(circle.segments.size(), std::size_t(24));
        EQUALS(circle.triangles.size(), std::size_t(24));
        for (const auto &edge : circle.segments) {
            EQUALS(std::abs(edge.from.x - 2) < 0.0001F, true);
            EQUALS(std::abs(glm::length(edge.from - glm::vec3(2, 3, 4)) - 2) < 0.0001F, true);
        }
        KCDC::Features::Gizmos::Drawing capsule;
        capsule.fillColor = 0x804488FF;
        KCDC::Features::Gizmos::Capsule(capsule, {0, 0, 0}, {3, 0, 0}, 1, 24);
        float minimum = 0, maximum = 0;
        for (const auto &face : capsule.triangles) {
            EQUALS(glm::length(glm::cross(face.b - face.a, face.c - face.a)) > 1e-6F, true);
            for (const auto &p : {face.a, face.b, face.c}) {
                minimum = std::min(minimum, p.x);
                maximum = std::max(maximum, p.x);
                EQUALS(std::abs(glm::length(p - glm::vec3(std::clamp(p.x, 0.0F, 3.0F), 0, 0)) - 1) < 0.0001F, true);
            }
        }
        EQUALS(std::abs(minimum + 1) < 0.0001F && std::abs(maximum - 4) < 0.0001F, true);
        bool rejected = false;
        try {
            KCDC::Features::Gizmos::Arrow(capsule, {0, 0, 0}, {0, 0, 1}, 2);
        }
        catch (const std::invalid_argument &) {
            rejected = true;
        }
        EQUALS(rejected, true);
    });

    IT("draws transformed boxes, polygon floors and three sphere circles", {
        KCDC::Shared::Game::AreaShape shape;
        shape.position = {10, 20, 30};
        shape.rotation = glm::angleAxis(1.57079632679F, glm::vec3(0, 0, 1));
        std::vector<KCDC::Features::Gizmos::Segment> edges;
        const auto collect = [&](const glm::vec3 &from, const glm::vec3 &to, float weight) {
            edges.push_back({from, to, weight});
        };
        KCDC::Shared::Game::AreaWireframe(shape, collect);
        EQUALS(edges.size(), std::size_t(12));
        EQUALS(glm::length(edges.front().from - glm::vec3(11, 19, 29)) < 0.0001F, true);
        shape.type   = KCDC::Shared::Game::AreaShapeType::Shape;
        shape.points = {{0, 0, 1}, {2, 0, 2}, {0, 2, 3}};
        edges.clear();
        KCDC::Shared::Game::AreaWireframe(shape, collect);
        EQUALS(edges.size(), std::size_t(3));
        EQUALS(edges.front().from.z, 31.0F);
        EQUALS(edges.front().to.z, 32.0F);
        shape.height = 5;
        edges.clear();
        KCDC::Shared::Game::AreaWireframe(shape, collect);
        EQUALS(edges.size(), std::size_t(9));
        EQUALS(edges[0].from.z, 31.0F);
        EQUALS(edges[0].to.z, 31.0F);
        EQUALS(edges[1].from.z, 36.0F);
        shape.type   = KCDC::Shared::Game::AreaShapeType::Sphere;
        shape.radius = 3;
        edges.clear();
        KCDC::Shared::Game::AreaWireframe(shape, collect);
        EQUALS(edges.size(), std::size_t(96));
        for (const auto &edge : edges) {
            EQUALS(std::abs(glm::length(edge.from - shape.position) - 3) < 0.0001F, true);
        }
    });

    IT("moves a drawing without rebuilding it and resets the move on replacement", {
        KCDC::Features::Gizmos::Drawings drawings;
        KCDC::Features::Gizmos::Drawing line;
        line.segments.push_back({glm::vec3(-1, 0, 0), glm::vec3(1, 0, 0)});
        EQUALS(drawings.SetTransform("test", "bar", glm::vec3(0), glm::quat(1, 0, 0, 0)), false);
        EQUALS(drawings.Set("test", "bar", line), true);
        const auto &stored = drawings.Entries().begin()->second;
        EQUALS(stored.Placed(), false);
        EQUALS(std::abs(stored.boundsRadius - 1) < 0.0001F, true);
        EQUALS(drawings.SetTransform("test", "bar", glm::vec3(5, 0, 0), glm::angleAxis(1.57079632679F, glm::vec3(0, 0, 1))), true);
        EQUALS(stored.Placed(), true);
        EQUALS(glm::length(stored.Place(stored.segments[0].to) - glm::vec3(5, 1, 0)) < 0.0001F, true);
        EQUALS(drawings.SetTransform("other", "bar", glm::vec3(0), glm::quat(1, 0, 0, 0)), false);
        EQUALS(drawings.Set("test", "bar", line), true);
        EQUALS(drawings.Entries().begin()->second.Placed(), false);
    });

    IT("frees a resource's slots on removal, clear and expiry alike", {
        KCDC::Features::Gizmos::Drawings drawings;
        KCDC::Features::Gizmos::Drawing line;
        line.segments.push_back({glm::vec3(0), glm::vec3(1)});
        const auto start = std::chrono::steady_clock::time_point {};
        for (std::size_t index = 0; index < KCDC::Features::Gizmos::Drawings::kMaxPerResource; ++index) {
            EQUALS(drawings.Set("first", std::to_string(index), line, start), true);
        }
        EQUALS(drawings.Set("first", "overflow", line, start), false);
        EQUALS(drawings.Remove("first", "0"), true);
        EQUALS(drawings.Set("first", "overflow", line, start), true);
        drawings.Clear("first");
        line.durationMs = 10;
        for (std::size_t index = 0; index < KCDC::Features::Gizmos::Drawings::kMaxPerResource; ++index) {
            EQUALS(drawings.Set("first", std::to_string(index), line, start), true);
        }
        EQUALS(drawings.Set("first", "overflow", line, start), false);
        EQUALS(drawings.Set("first", "late", line, start + std::chrono::milliseconds(10)), true);
        EQUALS(drawings.Entries().size(), std::size_t(1));
    });
});
