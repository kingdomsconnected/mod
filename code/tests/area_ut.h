#pragma once

#include "shared/features/area/area_definition.h"
#include "shared/features/area/area_geometry.h"
#include "shared/game/area_catalog.h"
#include "shared/game/door_catalog.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace AreaTests {
    inline const KCDC::Shared::Game::LevelArea *FindLevelArea(const char *level, std::uint64_t guid) {
        for (const KCDC::Shared::Game::LevelArea &area : KCDC::Shared::Game::g_LevelAreas) {
            if (area.guid == guid && std::strcmp(area.level, level) == 0) {
                return &area;
            }
        }
        return nullptr;
    }

    // The centre of an area's polygon, at `z`.
    inline glm::vec3 Centroid(const KCDC::Shared::Game::LevelArea &area, float z) {
        glm::vec3 sum {0.0F};
        for (std::uint32_t index = 0; index < area.pointCount; ++index) {
            const KCDC::Shared::Game::LevelAreaPoint &point = KCDC::Shared::Game::g_LevelAreaPoints[area.firstPoint + index];
            sum += glm::vec3(point.x, point.y, 0.0F);
        }
        sum /= static_cast<float>(area.pointCount);
        sum.z = z;
        return sum;
    }
} // namespace AreaTests

MODULE(area_geometry, {
    IT("tests a TriggerArea prism from triggerareas.fubar the way C_TriggerArea::IsPointInside does", {
        // The first record of kutnohorsko's fubar: EntityGuid "3e05a7c3-dcda-0e46", height 4, four corners.
        const KCDC::Shared::Game::LevelArea *area = AreaTests::FindLevelArea("kutnohorsko", 0x0E46DCDA3E05A7C3ULL);
        NEQUALS(area, nullptr);
        EQUALS(area->areaClass, KCDC::Shared::Game::LevelAreaClass::TriggerArea);
        EQUALS(area->pointCount, 4U);
        EQUALS(std::fabs(area->height - 4.0F) < 0.001F, true);

        const glm::vec3 inside = AreaTests::Centroid(*area, area->boundsMin.z + 1.0F);
        EQUALS(KCDC::Shared::Game::LevelAreaContains(*area, inside), true);
        EQUALS(KCDC::Shared::Game::LevelAreaContains(*area, inside + glm::vec3(0.0F, 0.0F, area->boundsMax.z - area->boundsMin.z + 1.0F)), false);
        EQUALS(KCDC::Shared::Game::LevelAreaContains(*area, inside - glm::vec3(0.0F, 0.0F, 2.0F)), false);
        EQUALS(KCDC::Shared::Game::LevelAreaContains(*area, inside + glm::vec3(500.0F, 0.0F, 0.0F)), false);
    });

    IT("raises a TriggerArea's box by its height above the first corner", {
        const KCDC::Shared::Game::LevelArea *area = AreaTests::FindLevelArea("kutnohorsko", 0x0E46DCDA3E05A7C3ULL);
        NEQUALS(area, nullptr);
        const KCDC::Shared::Game::LevelAreaPoint &first = KCDC::Shared::Game::g_LevelAreaPoints[area->firstPoint];
        EQUALS(area->boundsMax.z >= first.z + area->height - 0.01F, true);
    });

    IT("gives every level area a floor and a ceiling", {
        bool closed = true;
        for (const KCDC::Shared::Game::LevelArea &area : KCDC::Shared::Game::g_LevelAreas) {
            closed = closed && std::isfinite(area.boundsMin.z) && std::isfinite(area.boundsMax.z) && area.boundsMax.z > area.boundsMin.z;
        }
        EQUALS(closed, true);
    });

    IT("resolves every union to TriggerAreas of its own level", {
        std::unordered_map<std::uint64_t, const KCDC::Shared::Game::LevelArea *> kutno;
        for (const KCDC::Shared::Game::LevelArea &area : KCDC::Shared::Game::g_LevelAreas) {
            if (std::strcmp(area.level, "kutnohorsko") == 0) {
                kutno.emplace(area.guid, &area);
            }
        }
        std::size_t unions = 0;
        bool resolved      = true;
        for (const auto &entry : kutno) {
            const KCDC::Shared::Game::LevelArea &area = *entry.second;
            if (area.areaClass != KCDC::Shared::Game::LevelAreaClass::AreaUnion) {
                continue;
            }
            ++unions;
            resolved = resolved && area.memberCount > 0;
            for (std::uint32_t index = 0; index < area.memberCount; ++index) {
                const auto member = kutno.find(KCDC::Shared::Game::g_LevelAreaMembers[area.firstMember + index]);
                resolved          = resolved && member != kutno.end() && member->second->areaClass == KCDC::Shared::Game::LevelAreaClass::TriggerArea;
            }
        }
        GREATER(unions, static_cast<std::size_t>(0));
        EQUALS(resolved, true);
    });

    IT("links every crime_door to a door the door catalog knows", {
        std::size_t links = 0;
        bool known        = true;
        for (const KCDC::Shared::Game::LevelArea &area : KCDC::Shared::Game::g_LevelAreas) {
            for (std::uint32_t index = 0; index < area.doorCount; ++index) {
                ++links;
                known = known && KCDC::Shared::Game::FindDoorPlacement(area.level, KCDC::Shared::Game::g_LevelAreaDoors[area.firstDoor + index].doorGuid) != nullptr;
            }
        }
        GREATER(links, static_cast<std::size_t>(0));
        EQUALS(known, true);
    });

    IT("puts a shape's floor at its lowest corner and leaves it open with no height", {
        KCDC::Shared::Game::AreaShape shape;
        shape.type     = KCDC::Shared::Game::AreaShapeType::Shape;
        shape.position = glm::vec3(100.0F, 100.0F, 10.0F);
        shape.points   = {{0.0F, 0.0F, 0.0F}, {10.0F, 0.0F, -2.0F}, {10.0F, 10.0F, 0.0F}, {0.0F, 10.0F, 0.0F}};
        shape.height   = 5.0F;
        EQUALS(shape.IsValid(), true);
        const std::vector<glm::vec3> world = shape.WorldPoints();

        EQUALS(shape.Contains(glm::vec3(105.0F, 105.0F, 8.5F), world), true);  // floor is 8
        EQUALS(shape.Contains(glm::vec3(105.0F, 105.0F, 7.5F), world), false); // below it
        EQUALS(shape.Contains(glm::vec3(105.0F, 105.0F, 13.5F), world), false); // above 8 + 5
        EQUALS(shape.Contains(glm::vec3(115.0F, 105.0F, 9.0F), world), false);

        shape.height = 0.0F;
        EQUALS(shape.Contains(glm::vec3(105.0F, 105.0F, 500.0F), shape.WorldPoints()), true);
    });

    IT("tests a turned box in its own space", {
        KCDC::Shared::Game::AreaShape box;
        box.type     = KCDC::Shared::Game::AreaShapeType::Box;
        box.position = glm::vec3(50.0F, 50.0F, 0.0F);
        box.rotation = glm::angleAxis(glm::radians(90.0F), glm::vec3(0.0F, 0.0F, 1.0F));
        box.boxMin   = glm::vec3(-10.0F, -1.0F, 0.0F);
        box.boxMax   = glm::vec3(10.0F, 1.0F, 3.0F);
        EQUALS(box.IsValid(), true);

        // Long along world Y once turned a quarter.
        EQUALS(box.Contains(glm::vec3(50.0F, 58.0F, 1.0F), {}), true);
        EQUALS(box.Contains(glm::vec3(58.0F, 50.0F, 1.0F), {}), false);
        EQUALS(box.Contains(glm::vec3(50.0F, 58.0F, 4.0F), {}), false);

        glm::vec3 min {0.0F};
        glm::vec3 max {0.0F};
        box.WorldBounds({}, min, max);
        EQUALS(std::fabs(max.y - 60.0F) < 0.01F && std::fabs(max.x - 51.0F) < 0.01F, true);
    });

    IT("tests a sphere by its radius", {
        KCDC::Shared::Game::AreaShape sphere;
        sphere.type     = KCDC::Shared::Game::AreaShapeType::Sphere;
        sphere.position = glm::vec3(0.0F, 0.0F, 10.0F);
        sphere.radius   = 3.0F;
        EQUALS(sphere.Contains(glm::vec3(0.0F, 2.9F, 10.0F), {}), true);
        EQUALS(sphere.Contains(glm::vec3(0.0F, 3.1F, 10.0F), {}), false);
    });

    IT("refuses shapes the engine could not build", {
        KCDC::Shared::Game::AreaShape box;
        box.boxMin = glm::vec3(1.0F);
        box.boxMax = glm::vec3(1.0F);
        EQUALS(box.IsValid(), false);

        KCDC::Shared::Game::AreaShape shape;
        shape.type   = KCDC::Shared::Game::AreaShapeType::Shape;
        shape.points = {{0.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}};
        EQUALS(shape.IsValid(), false);

        KCDC::Shared::Game::AreaShape sphere;
        sphere.type   = KCDC::Shared::Game::AreaShapeType::Sphere;
        sphere.radius = 0.0F;
        EQUALS(sphere.IsValid(), false);
    });

    IT("round-trips a definition through the format Area.create and the World Builder share", {
        KCDC::Shared::Game::AreaDefinition area;
        area.id           = "market";
        area.name         = "Market";
        area.shape.type   = KCDC::Shared::Game::AreaShapeType::Shape;
        area.shape.points = {{0.0F, 0.0F, 0.0F}, {5.0F, 0.0F, 1.0F}, {5.0F, 5.0F, 0.0F}};
        area.shape.height = 4.0F;
        area.labels       = {"shop", "public"};
        area.metadataJson = R"({"owner":"guild"})";
        area.virtualWorld = 3U;

        std::vector<KCDC::Shared::Game::AreaDefinition> read;
        std::size_t skipped = 0;
        std::string error;
        EQUALS(KCDC::Shared::Game::ParseAreaDefinitions(KCDC::Shared::Game::SerializeAreaDefinitions({area}), read, skipped, error), true);
        EQUALS(read.size(), static_cast<std::size_t>(1));
        EQUALS(skipped, static_cast<std::size_t>(0));
        STREQUALS(read[0].id.c_str(), "market");
        EQUALS(read[0].shape.type, KCDC::Shared::Game::AreaShapeType::Shape);
        EQUALS(read[0].shape.points.size(), static_cast<std::size_t>(3));
        EQUALS(std::fabs(read[0].shape.points[1].z - 1.0F) < 0.001F, true);
        EQUALS(read[0].labels.size(), static_cast<std::size_t>(2));
        EQUALS(read[0].virtualWorld.value_or(0U), 3U);
        EQUALS(nlohmann::json::parse(read[0].metadataJson)["owner"] == "guild", true);
    });

    IT("skips a definition Area.create would refuse", {
        std::vector<KCDC::Shared::Game::AreaDefinition> read;
        std::size_t skipped = 0;
        std::string error;
        EQUALS(KCDC::Shared::Game::ParseAreaDefinitions(R"([{"id":"a","type":"sphere","radius":2},{"id":"b","type":"cylinder"},{"type":"box"}])", read, skipped, error), true);
        EQUALS(read.size(), static_cast<std::size_t>(1));
        EQUALS(skipped, static_cast<std::size_t>(2));
        EQUALS(KCDC::Shared::Game::ParseAreaDefinitions("not json", read, skipped, error), false);
    });

    IT("reads the shape names scripts and the World Builder write", {
        KCDC::Shared::Game::AreaShapeType type = KCDC::Shared::Game::AreaShapeType::Box;
        EQUALS(KCDC::Shared::Game::ParseAreaShapeType("sphere", type), true);
        EQUALS(type, KCDC::Shared::Game::AreaShapeType::Sphere);
        EQUALS(KCDC::Shared::Game::ParseAreaShapeType("cylinder", type), false);
        EQUALS(std::strcmp(KCDC::Shared::Game::AreaShapeTypeName(KCDC::Shared::Game::AreaShapeType::Shape), "shape"), 0);
    });
});
