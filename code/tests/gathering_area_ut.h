#pragma once

#include "features/gathering/gathering_module.h"
#include "shared/game/gathering_catalog.h"

#include <glm/gtc/quaternion.hpp>

namespace GatheringAreaTests {
    inline KCDC::Features::Area::AreaRecord Sphere(const glm::vec3 &position, float radius) {
        KCDC::Features::Area::AreaRecord area;
        area.definition.shape.type     = KCDC::Shared::Game::AreaShapeType::Sphere;
        area.definition.shape.position = position;
        area.definition.shape.radius   = radius;
        area.definition.shape.WorldBounds(area.worldPoints, area.boundsMin, area.boundsMax);
        return area;
    }
} // namespace GatheringAreaTests

MODULE(gathering_areas, {
    IT("keeps close same-species sources distinct across an area boundary", {
        KCDC::Features::Gathering::GatheringModule gathering({});
        KCDC::Features::Area::AreaService areas;
        gathering.SetSources({{2, {10.0F, 10.0F, 10.0F}}, {2, {10.005F, 10.0F, 10.0F}}, {2, {10.05F, 10.0F, 10.0F}}});
        auto narrow = GatheringAreaTests::Sphere({10.0F, 10.0F, 10.0F}, 0.001F);
        auto all    = GatheringAreaTests::Sphere({10.0F, 10.0F, 10.0F}, 1.0F);
        EQUALS(gathering.RemoveHerbs(areas, narrow, 0, 100).size(), std::size_t(1));
        EQUALS(gathering.GetHerbs(areas, all, 0, 100).size(), std::size_t(2));
        EQUALS(gathering.RemoveHerbs(areas, all, 0, 100).size(), std::size_t(2));
        EQUALS(gathering.RespawnHerbs(areas, narrow, 0, 100).size(), std::size_t(1));
        EQUALS(gathering.GetHerbs(areas, all, 0, 100).size(), std::size_t(1));
    });

    IT("replays a full sync after the client loses its native projection", {
        KCDC::Features::Gathering::GatheringModule gathering({});
        KCDC::Features::Area::AreaService areas;
        gathering.SetSources({{2, {100.0F, 200.0F, 30.0F}}});
        const auto area = GatheringAreaTests::Sphere({100.0F, 200.0F, 30.0F}, 2.0F);
        gathering.RemoveHerbs(areas, area, 0, 100);
        EQUALS(gathering.Sync(42, 0, {100, 200, 30}, 1000, 100)->size(), std::size_t(1));
        EQUALS(gathering.Sync(42, 0, {100, 200, 30}, 10000, 100).has_value(), false);
        EQUALS(gathering.Sync(42, 0, {100, 200, 30}, 20000, 100, true)->size(), std::size_t(1));
        EQUALS(gathering.Sync(42, 1, {100, 200, 30}, 30000, 100)->empty(), true);
        KCDC::Features::Gathering::GatheringRequest request;
        EQUALS(KCDC::Features::Gathering::GatheringModule::ReadRequest(R"({"action":"sync","full":true})", request), true);
        EQUALS(request.full, true);
        EQUALS(KCDC::Features::Gathering::GatheringModule::ReadRequest(R"({"action":"sync","full":"true"})", request), false);
    });

    IT("converts picked sources to permanent removal and rechecks synchronous scripts", {
        KCDC::Features::Inventory::InventoryIds ids;
        KCDC::Features::Inventory::Inventory inventory(ids);
        KCDC::Features::Area::AreaService areas;
        auto area                                         = GatheringAreaTests::Sphere({100.0F, 200.0F, 30.0F}, 2.0F);
        KCDC::Features::Gathering::GatheringModule *owner = nullptr;
        bool removeDuringHarvest                          = false;
        double xp                                         = 0;
        KCDC::Features::Gathering::GatheringHooks hooks;
        hooks.inventory = [&](std::uint64_t) {
            return &inventory;
        };
        hooks.survivalLevel = [](std::uint64_t) {
            return std::optional<float>(4.0F);
        };
        hooks.experience = [&](std::uint64_t, double amount) {
            xp += amount;
        };
        hooks.harvesting = [&](std::uint64_t, const nlohmann::json &) {
            if (removeDuringHarvest) {
                owner->RemoveHerbs(areas, area, 0, 100);
            }
            return true;
        };
        KCDC::Features::Gathering::GatheringModule gathering(std::move(hooks));
        owner = &gathering;
        gathering.SetSources({{2, {100.0F, 200.0F, 30.0F}}});
        KCDC::Features::Gathering::GatheringRequest request {"begin", "pick", 2, {100.0F, 200.0F, 30.0F}, {}};
        EQUALS(gathering.Begin(42, request, request.position, 0, 1000, 100).empty(), true);
        EQUALS(gathering.Complete(42, request, request.position, 0, true, 3000, 100).code.empty(), true);
        EQUALS(gathering.GetHerbs(areas, area, 0, 100).empty(), true);
        const auto changed = gathering.RemoveHerbs(areas, area, 0, 100);
        EQUALS(changed.size(), std::size_t(1));
        EQUALS(changed.front().removed, true);
        EQUALS(changed.front().until, std::uint64_t(0));
        gathering.RespawnHerbs(areas, area, 0, 100);
        removeDuringHarvest = true;
        xp                  = 0;
        EQUALS(gathering.Begin(42, request, request.position, 0, 5000, 100).empty(), true);
        const auto refused = gathering.Complete(42, request, request.position, 0, true, 7000, 100);
        EQUALS(refused.code, std::string("depleted"));
        EQUALS(refused.outputs.empty(), true);
        EQUALS(xp, 0.0);
    });

    IT("restores the optimistic primary when a script removes only a secondary herb", {
        KCDC::Features::Inventory::InventoryIds ids;
        KCDC::Features::Inventory::Inventory inventory(ids);
        KCDC::Features::Area::AreaService areas;
        auto secondary                                    = GatheringAreaTests::Sphere({100.5F, 200.0F, 30.0F}, 0.01F);
        KCDC::Features::Gathering::GatheringModule *owner = nullptr;
        KCDC::Features::Gathering::GatheringHooks hooks;
        hooks.inventory = [&](std::uint64_t) {
            return &inventory;
        };
        hooks.survivalLevel = [](std::uint64_t) {
            return std::optional<float>(4.0F);
        };
        hooks.experience = [](std::uint64_t, double) {};
        hooks.harvesting = [&](std::uint64_t, const nlohmann::json &) {
            owner->RemoveHerbs(areas, secondary, 0, 100);
            return true;
        };
        KCDC::Features::Gathering::GatheringModule gathering(std::move(hooks));
        owner = &gathering;
        gathering.SetSources({{2, {100.0F, 200.0F, 30.0F}}, {2, {100.5F, 200.0F, 30.0F}}});
        KCDC::Features::Gathering::GatheringRequest request {"begin", "pick", 2, {100.0F, 200.0F, 30.0F}, {{100.5F, 200.0F, 30.0F}}};
        EQUALS(gathering.Begin(42, request, request.position, 0, 1000, 100).empty(), true);
        const auto refused = gathering.Complete(42, request, request.position, 0, true, 3000, 100);
        EQUALS(refused.code, std::string("gatheringHarvestRejected"));
        EQUALS(refused.outputs.empty(), true);
        EQUALS(refused.depleted.size(), std::size_t(1));
        EQUALS(gathering.IsDepleted(0, 2, request.position, 100), false);
    });

    IT("queries untouched herbs across cell edges and applies exact 3D geometry", {
        KCDC::Features::Gathering::GatheringModule gathering({});
        KCDC::Features::Area::AreaService areas;
        gathering.SetSources({{2, {63.5F, 64.0F, 10.0F}}, {3, {64.5F, 64.0F, 10.0F}}, {2, {64.0F, 64.0F, 13.0F}}, {2, {65.9F, 65.9F, 10.0F}}});
        auto area = GatheringAreaTests::Sphere({64.0F, 64.0F, 10.0F}, 2.0F);
        EQUALS(gathering.GetHerbs(areas, area, 0, 100).size(), std::size_t(2));
        area.definition.enabled = false;
        EQUALS(gathering.RemoveHerbs(areas, area, 0, 100).size(), std::size_t(2));
        EQUALS(gathering.GetHerbs(areas, area, 0, 100).empty(), true);
        EQUALS(gathering.GetHerbs(areas, area, 1, 100).size(), std::size_t(2));
        EQUALS(gathering.IsDepleted(0, 2, {64.0F, 64.0F, 13.0F}, 100), false);
        EQUALS(gathering.RemoveHerbs(areas, area, 0, 100).empty(), true);
        area.definition.virtualWorld = 1;
        EQUALS(gathering.GetHerbs(areas, area, 0, 100).empty(), true);
        EQUALS(gathering.RespawnHerbs(areas, area, 0, 100).empty(), true);
    });

    IT("keeps removal permanent across timer changes and synchronizes late arrivals", {
        KCDC::Features::Gathering::GatheringModule gathering({});
        KCDC::Features::Area::AreaService areas;
        gathering.SetSources({{2, {100.0F, 200.0F, 30.0F}}, {3, {100.0F, 200.0F, 30.0F}}});
        auto area = GatheringAreaTests::Sphere({100.0F, 200.0F, 30.0F}, 2.0F);
        EQUALS(gathering.RemoveHerbs(areas, area, 0, 100).size(), std::size_t(2));
        gathering.SetRespawnTime(0.001, 0.001);
        EQUALS(gathering.Reschedule(200).empty(), true);
        gathering.ResetRespawnTime();
        EQUALS(gathering.Reschedule(300).empty(), true);
        gathering.Prune(1000000000);
        EQUALS(gathering.GetHerbs(areas, area, 0, 1000000000).empty(), true);
        const auto sync = gathering.Sync(42, 0, {100.0F, 200.0F, 30.0F}, 1000, 1000000000);
        EQUALS(sync.has_value(), true);
        EQUALS(sync->size(), std::size_t(2));
        EQUALS(sync->front().until, std::uint64_t(0));
        EQUALS(gathering.RespawnHerbs(areas, area, 0, 1000000000).size(), std::size_t(2));
        EQUALS(gathering.GetHerbs(areas, area, 0, 1000000000).size(), std::size_t(2));
        EQUALS(gathering.RespawnHerbs(areas, area, 0, 1000000000).empty(), true);
        gathering.RemoveHerbs(areas, area, 0, 1000000000);
        EQUALS(gathering.Respawn(0, std::nullopt, 0, 1000000000).size(), std::size_t(2));
    });

    IT("uses rotated boxes and polygon interiors rather than their bounds", {
        KCDC::Features::Gathering::GatheringModule gathering({});
        KCDC::Features::Area::AreaService areas;
        gathering.SetSources({{2, {1.0F, 1.0F, 0.0F}}, {2, {1.0F, -1.0F, 0.0F}}, {2, {1.0F, 1.0F, 5.0F}}});
        KCDC::Features::Area::AreaRecord area;
        area.definition.shape.boxMin   = {-2.0F, -0.2F, -1.0F};
        area.definition.shape.boxMax   = {2.0F, 0.2F, 1.0F};
        area.definition.shape.rotation = glm::angleAxis(glm::radians(45.0F), glm::vec3(0, 0, 1));
        area.definition.shape.WorldBounds(area.worldPoints, area.boundsMin, area.boundsMax);
        EQUALS(gathering.GetHerbs(areas, area, 0, 100).size(), std::size_t(1));
        area.definition.shape.type     = KCDC::Shared::Game::AreaShapeType::Shape;
        area.definition.shape.rotation = {1.0F, 0.0F, 0.0F, 0.0F};
        area.definition.shape.points   = {{0, 0, -1}, {3, 0, -1}, {0, 3, -1}};
        area.definition.shape.height   = 2;
        area.worldPoints               = area.definition.shape.WorldPoints();
        area.definition.shape.WorldBounds(area.worldPoints, area.boundsMin, area.boundsMax);
        EQUALS(gathering.RemoveHerbs(areas, area, 0, 100).size(), std::size_t(1));
        EQUALS(gathering.IsDepleted(0, 2, {1, -1, 0}, 100), false);
        area.destroying = true;
        EQUALS(gathering.RespawnHerbs(areas, area, 0, 100).empty(), true);
        gathering.Regrow();
        area.destroying = false;
        EQUALS(gathering.GetHerbs(areas, area, 0, 100).size(), std::size_t(1));
    });

    IT("removal blocks a pending harvest without awarding inventory or experience", {
        KCDC::Features::Area::AreaService areas;
        auto area = GatheringAreaTests::Sphere({100.0F, 200.0F, 30.0F}, 2.0F);
        KCDC::Features::Gathering::GatheringModule gathering({});
        gathering.SetSources({{2, {100.0F, 200.0F, 30.0F}}});
        KCDC::Features::Gathering::GatheringRequest request {"begin", "pick", 2, {100.0F, 200.0F, 30.0F}, {}};
        EQUALS(gathering.Begin(42, request, request.position, 0, 1000, 100).empty(), true);
        gathering.RemoveHerbs(areas, area, 0, 100);
        request.action     = "complete";
        const auto harvest = gathering.Complete(42, request, request.position, 0, true, 3000, 100);
        EQUALS(harvest.code, std::string("depleted"));
        EQUALS(harvest.outputs.empty(), true);
        EQUALS(harvest.depleted.size(), std::size_t(1));
        EQUALS(gathering.Begin(43, request, request.position, 0, 3000, 100), std::string("depleted"));
    });
});
