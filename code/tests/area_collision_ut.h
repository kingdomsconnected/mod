#pragma once

#include "shared/features/area/area_definition.h"

#include <limits>

namespace AreaCollisionTests {
    // Backface-culling ray test: a collision must expose an outward face to
    // both an approaching exterior player and one walking out of the hollow.
    inline bool Hits(const KCDC::Shared::Game::AreaCollisionMesh &mesh, const glm::vec3 &from, const glm::vec3 &to) {
        const auto direction = to - from;
        for (std::size_t i = 0; i < mesh.indices.size(); i += 3) {
            const auto a = mesh.vertices[mesh.indices[i]], b = mesh.vertices[mesh.indices[i + 1]], c = mesh.vertices[mesh.indices[i + 2]];
            const auto e1 = b - a, e2 = c - a, p = glm::cross(direction, e2);
            const float determinant = glm::dot(e1, p);
            if (determinant <= 1e-7F) {
                continue;
            }
            const auto offset = from - a;
            const float u     = glm::dot(offset, p) / determinant;
            const auto q      = glm::cross(offset, e1);
            const float v = glm::dot(direction, q) / determinant, t = glm::dot(e2, q) / determinant;
            if (u >= -1e-5F && v >= -1e-5F && u + v <= 1.00001F && t >= 0 && t <= 1) {
                return true;
            }
        }
        return false;
    }

    inline const KCDC::Shared::Game::AreaCollisionOptions kWalls {};
    inline const KCDC::Shared::Game::AreaCollisionOptions kClosed {true, true};
} // namespace AreaCollisionTests

MODULE(area_collision, {
    IT("keeps old areas nonblocking and preserves explicit collision through JSON", {
        KCDC::Shared::Game::AreaDefinition area;
        area.id   = "boundary";
        auto json = KCDC::Shared::Game::AreaDefinitionToJson(area);
        json.erase("collision");
        std::string error;
        EQUALS(KCDC::Shared::Game::AreaDefinitionFromJson(json, area, error), true);
        EQUALS(area.collision, false);
        json["collision"] = true;
        EQUALS(KCDC::Shared::Game::AreaDefinitionFromJson(json, area, error), true);
        EQUALS(KCDC::Shared::Game::AreaDefinitionToJson(area)["collision"].get<bool>(), true);
        json["collision"] = "true";
        EQUALS(KCDC::Shared::Game::AreaDefinitionFromJson(json, area, error), false);
    });

    IT("reads areas saved without collision options as walls only and round-trips the options", {
        KCDC::Shared::Game::AreaDefinition area;
        area.id        = "arena";
        area.collision = true;
        auto json      = KCDC::Shared::Game::AreaDefinitionToJson(area);
        json.erase("collisionFloor");
        json.erase("collisionCeiling");
        json.erase("collisionThickness");
        std::string error;
        EQUALS(KCDC::Shared::Game::AreaDefinitionFromJson(json, area, error), true);
        EQUALS(area.collisionOptions == AreaCollisionTests::kWalls, true);

        area.collisionOptions = {true, false, 0.5F};
        json                  = KCDC::Shared::Game::AreaDefinitionToJson(area);
        EQUALS(json["collisionFloor"].get<bool>(), true);
        EQUALS(json["collisionCeiling"].get<bool>(), false);
        EQUALS(json["collisionThickness"].get<float>(), 0.5F);
        KCDC::Shared::Game::AreaDefinition read;
        EQUALS(KCDC::Shared::Game::AreaDefinitionFromJson(json, read, error), true);
        EQUALS(read.collisionOptions == area.collisionOptions, true);
    });

    IT("rejects mistyped collision options and a thickness outside its range", {
        KCDC::Shared::Game::AreaDefinition area;
        area.id = "arena";
        std::string error;
        for (const auto &[key, value] : {std::pair<const char *, nlohmann::json> {"collisionFloor", "yes"}, {"collisionCeiling", 1}, {"collisionThickness", "0.5"}, {"collisionThickness", 0.01}, {"collisionThickness", 2.5}}) {
            auto json = KCDC::Shared::Game::AreaDefinitionToJson(area);
            json[key] = value;
            KCDC::Shared::Game::AreaDefinition read;
            error.clear();
            EQUALS(KCDC::Shared::Game::AreaDefinitionFromJson(json, read, error), false);
            EQUALS((error.find(key) != std::string::npos || error.find("thickness") != std::string::npos), true);
        }
        auto json = KCDC::Shared::Game::AreaDefinitionToJson(area);
        KCDC::Shared::Game::AreaDefinition read;
        for (const double edge : {0.05, 2.0}) {
            json["collisionThickness"] = edge;
            EQUALS(KCDC::Shared::Game::AreaDefinitionFromJson(json, read, error), true);
        }
    });

    IT("refuses a collision shape too small for its wall thickness", {
        KCDC::Shared::Game::AreaShape shape;
        KCDC::Shared::Game::AreaCollisionMesh mesh;
        std::string error;
        EQUALS(KCDC::Shared::Game::BuildAreaCollisionMesh(shape, {false, false, 1.5F}, mesh, error), true);
        EQUALS(KCDC::Shared::Game::BuildAreaCollisionMesh(shape, {false, false, 2.0F}, mesh, error), false);
        EQUALS(error.find("2 m") != std::string::npos, true);
        EQUALS(mesh.vertices.empty(), true);
    });

    IT("blocks both directions on all rotated box faces without filling its interior", {
        KCDC::Shared::Game::AreaShape shape;
        shape.rotation = glm::angleAxis(0.6F, glm::normalize(glm::vec3(1, 2, 3)));
        for (const bool closed : {false, true}) {
            KCDC::Shared::Game::AreaCollisionMesh mesh;
            std::string error;
            EQUALS(KCDC::Shared::Game::BuildAreaCollisionMesh(shape, closed ? AreaCollisionTests::kClosed : AreaCollisionTests::kWalls, mesh, error), true);
            for (int axis = 0; axis < 3; ++axis) {
                for (float sign : {-1.0F, 1.0F}) {
                    glm::vec3 outside(0);
                    outside[axis] = sign * 2;
                    outside       = shape.rotation * outside;
                    // The box's own bottom and top close only when asked to.
                    const bool blocks = axis < 2 || closed;
                    EQUALS(AreaCollisionTests::Hits(mesh, glm::vec3(0), outside), blocks);
                    EQUALS(AreaCollisionTests::Hits(mesh, outside, glm::vec3(0)), blocks);
                }
            }
            EQUALS(AreaCollisionTests::Hits(mesh, glm::vec3(-0.1F), glm::vec3(0.1F)), false);
        }
    });

    IT("closes only the plate that is asked for", {
        KCDC::Shared::Game::AreaShape shape;
        const glm::vec3 below(0, 0, -2), above(0, 0, 2);
        for (const auto &[floor, ceiling] : {std::pair {true, false}, std::pair {false, true}}) {
            KCDC::Shared::Game::AreaCollisionMesh mesh;
            std::string error;
            EQUALS(KCDC::Shared::Game::BuildAreaCollisionMesh(shape, {floor, ceiling}, mesh, error), true);
            EQUALS(AreaCollisionTests::Hits(mesh, glm::vec3(0), below), floor);
            EQUALS(AreaCollisionTests::Hits(mesh, glm::vec3(0), above), ceiling);
        }
    });

    IT("centres walls of the chosen thickness on the outline", {
        KCDC::Shared::Game::AreaShape shape;
        for (const float thickness : {0.05F, 0.2F, 1.0F}) {
            KCDC::Shared::Game::AreaCollisionMesh mesh;
            std::string error;
            EQUALS(KCDC::Shared::Game::BuildAreaCollisionMesh(shape, {false, false, thickness}, mesh, error), true);
            float reach = 0.0F;
            for (const auto &vertex : mesh.vertices) {
                reach = std::max(reach, vertex.x);
            }
            EQUALS(std::abs(reach - (shape.boxMax.x + thickness * 0.5F)) < 1e-4F, true);
            // Just inside the inner face is open; just inside the outer face is wall.
            EQUALS(AreaCollisionTests::Hits(mesh, glm::vec3(0), glm::vec3(shape.boxMax.x - thickness * 0.5F - 0.01F, 0, 0)), false);
            EQUALS(AreaCollisionTests::Hits(mesh, glm::vec3(0), glm::vec3(shape.boxMax.x + thickness * 0.5F + 0.01F, 0, 0)), true);
        }
    });

    IT("triangulates concave caps in either winding without sealing the notch", {
        KCDC::Shared::Game::AreaShape shape;
        shape.type   = KCDC::Shared::Game::AreaShapeType::Shape;
        shape.points = {{0, 0, 0}, {4, 0, 0}, {4, 1, 0}, {1, 1, 0}, {1, 4, 0}, {0, 4, 0}};
        shape.height = 3;
        for (int winding = 0; winding < 2; ++winding) {
            for (const bool closed : {false, true}) {
                KCDC::Shared::Game::AreaCollisionMesh mesh;
                std::string error;
                EQUALS(KCDC::Shared::Game::BuildAreaCollisionMesh(shape, closed ? AreaCollisionTests::kClosed : AreaCollisionTests::kWalls, mesh, error), true);
                EQUALS(AreaCollisionTests::Hits(mesh, {0.5F, 2, 1.5F}, {2, 2, 1.5F}), true);
                EQUALS(AreaCollisionTests::Hits(mesh, {2, 2, 1.5F}, {0.5F, 2, 1.5F}), true);
                // Straight up through the inside meets the plates only when they were asked for.
                EQUALS(AreaCollisionTests::Hits(mesh, {0.5F, 2, -1}, {0.5F, 2, 4}), closed);
                EQUALS(AreaCollisionTests::Hits(mesh, {2, 2, -1}, {2, 2, 4}), false);
            }
            std::reverse(shape.points.begin(), shape.points.end());
        }
        shape.points = {{0, 0, 0}, {4, 4, 0}, {0, 4, 0}, {4, 0, 0}};
        KCDC::Shared::Game::AreaCollisionMesh mesh;
        std::string error;
        EQUALS(shape.IsValid(), true);
        // A crossing outline is refused even without plates: the triangulation is also the outline check.
        EQUALS(KCDC::Shared::Game::BuildAreaCollisionMesh(shape, AreaCollisionTests::kWalls, mesh, error), false);
        EQUALS(mesh.vertices.empty(), true);
    });

    IT("rejects polygon faces that collapse in native float arithmetic", {
        KCDC::Shared::Game::AreaShape shape;
        shape.type   = KCDC::Shared::Game::AreaShapeType::Shape;
        shape.points = {{0, 0, 0}, {8192, 8191, 0}, {8193, 8192, 0}};
        shape.height = 3;
        KCDC::Shared::Game::AreaCollisionMesh mesh;
        std::string error;
        EQUALS(shape.IsValid(), true);
        EQUALS(KCDC::Shared::Game::BuildAreaCollisionMesh(shape, AreaCollisionTests::kClosed, mesh, error), false);
        EQUALS(mesh.vertices.empty(), true);
        EQUALS(mesh.indices.empty(), true);
        EQUALS(error.empty(), false);
    });

    IT("closes sphere poles and seams from both sides within the native index budget", {
        KCDC::Shared::Game::AreaShape shape;
        shape.type = KCDC::Shared::Game::AreaShapeType::Sphere;
        for (float radius : {0.3F, 5.0F, 11.0F, 16000.0F}) {
            shape.radius = radius;
            KCDC::Shared::Game::AreaCollisionMesh mesh;
            std::string error;
            EQUALS(KCDC::Shared::Game::BuildAreaCollisionMesh(shape, AreaCollisionTests::kWalls, mesh, error), true);
            EQUALS(mesh.vertices.size() < 65536, true);
            EQUALS(mesh.indices.size() / 3 < 32767, true);
            for (const auto &direction : {glm::vec3(0, 0, 1), glm::vec3(0, 0, -1), glm::vec3(1, 0, 0), glm::normalize(glm::vec3(1, 2, 3))}) {
                const auto outside = direction * (radius + 1);
                EQUALS(AreaCollisionTests::Hits(mesh, glm::vec3(0), outside), true);
                EQUALS(AreaCollisionTests::Hits(mesh, outside, glm::vec3(0)), true);
            }
            for (std::size_t i = 0; i < mesh.indices.size(); i += 3) {
                const auto normal = glm::cross(mesh.vertices[mesh.indices[i + 1]] - mesh.vertices[mesh.indices[i]], mesh.vertices[mesh.indices[i + 2]] - mesh.vertices[mesh.indices[i]]);
                EQUALS(std::isfinite(glm::dot(normal, normal)) && glm::dot(normal, normal) > 0, true);
            }
        }
    });
});
