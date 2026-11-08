#include "area_service.h"
#include "area_scripting.h"
#include "shared/features/area/area_collision_entity.h"

#include "shared/features/cart/cart_entity.h"
#include "shared/features/horse/horse_entity.h"
#include "shared/features/npc/npc_entity.h"
#include "shared/features/player/player_entity.h"
#include "shared/game/door_catalog.h"
#include "shared/scripting/level_guid.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/replication/replication_manager.h>
#include <utils/string_utils.h>

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace KCDC::Features::Area {
    namespace {
        constexpr const char *kLogger = "Area";

        // A body that has not moved this far since its last test is not tested again.
        constexpr float kRetestDistanceSq = 0.05F * 0.05F;

        // An area covering more cells than this goes on the wide list instead of the grid.
        constexpr std::int64_t kMaxGridCells = 4096;

        // The catalog stores a level's labels already trimmed, joined by `;`.
        std::vector<std::string> SplitLabels(std::string_view labels) {
            std::vector<std::string> split;
            while (!labels.empty()) {
                const std::size_t end = labels.find(';');
                split.emplace_back(labels.substr(0, end));
                labels.remove_prefix(end == std::string_view::npos ? labels.size() : end + 1);
            }
            return split;
        }
    } // namespace

    std::int64_t AreaService::CellKey(std::int32_t x, std::int32_t y) {
        return (static_cast<std::int64_t>(x) << 32) ^ static_cast<std::uint32_t>(y);
    }

    void AreaService::Install(const std::string &level) {
        _level = level;

        for (const KCDC::Shared::Game::LevelArea &row : KCDC::Shared::Game::g_LevelAreas) {
            if (level != row.level) {
                continue;
            }
            auto area               = std::make_unique<AreaRecord>();
            area->level             = &row;
            area->definition.id     = KCDC::Scripting::FormatLevelGuid(row.guid);
            area->definition.name   = row.name;
            area->definition.labels = SplitLabels(row.labels);
            area->boundsMin         = KCDC::Shared::Game::ToVec3(row.boundsMin);
            area->boundsMax         = KCDC::Shared::Game::ToVec3(row.boundsMax);
            AreaRecord &inserted = Insert(std::move(area));
            _byGuid.emplace(row.guid, inserted.handle);
            for (std::uint32_t index = 0; index < row.doorCount; ++index) {
                _byDoor.emplace(KCDC::Shared::Game::g_LevelAreaDoors[row.firstDoor + index].doorGuid, inserted.handle);
            }
        }

        // Members are resolved once every area of the level has a handle.
        for (auto &entry : _areas) {
            AreaRecord &area = *entry.second;
            for (std::uint32_t index = 0; index < area.level->memberCount; ++index) {
                const auto member = _byGuid.find(KCDC::Shared::Game::g_LevelAreaMembers[area.level->firstMember + index]);
                if (member != _byGuid.end()) {
                    area.members.push_back(member->second);
                }
            }
        }

        Framework::Logging::GetLogger(kLogger)->info("Indexed {} level area(s) for '{}', {} door link(s)", _areas.size(), level, _byDoor.size());
    }

    AreaRecord &AreaService::Insert(std::unique_ptr<AreaRecord> area) {
        area->handle     = _nextHandle++;
        AreaRecord &kept = *area;
        _byId.emplace(kept.definition.id, kept.handle);
        _areas.emplace(kept.handle, std::move(area));
        IndexArea(kept);
        return kept;
    }

    template <typename Visit>
    bool AreaService::ForEachCell(const AreaRecord &area, Visit &&visit) {
        const std::int32_t minX = static_cast<std::int32_t>(std::floor(area.boundsMin.x / kCellSize));
        const std::int32_t minY = static_cast<std::int32_t>(std::floor(area.boundsMin.y / kCellSize));
        const std::int32_t maxX = static_cast<std::int32_t>(std::floor(area.boundsMax.x / kCellSize));
        const std::int32_t maxY = static_cast<std::int32_t>(std::floor(area.boundsMax.y / kCellSize));
        if (static_cast<std::int64_t>(maxX - minX + 1) * static_cast<std::int64_t>(maxY - minY + 1) > kMaxGridCells) {
            return false;
        }
        for (std::int32_t x = minX; x <= maxX; ++x) {
            for (std::int32_t y = minY; y <= maxY; ++y) {
                visit(CellKey(x, y));
            }
        }
        return true;
    }

    void AreaService::IndexArea(const AreaRecord &area) {
        const bool gridded = ForEachCell(area, [&](std::int64_t cell) {
            _grid[cell].push_back(area.handle);
        });
        if (!gridded) {
            _wide.push_back(area.handle);
        }
    }

    void AreaService::UnindexArea(const AreaRecord &area) {
        const auto drop = [&area](std::vector<std::uint32_t> &handles) {
            handles.erase(std::remove(handles.begin(), handles.end(), area.handle), handles.end());
        };
        const bool gridded = ForEachCell(area, [&](std::int64_t key) {
            if (const auto cell = _grid.find(key); cell != _grid.end()) {
                drop(cell->second);
            }
        });
        if (!gridded) {
            drop(_wide);
        }
    }

    void AreaService::RefreshGeometry(AreaRecord &area) {
        area.worldPoints = area.definition.shape.WorldPoints();
        area.definition.shape.WorldBounds(area.worldPoints, area.boundsMin, area.boundsMax);
    }

    template <typename Visit>
    void AreaService::ForEachCandidate(const glm::vec3 &point, Visit &&visit) const {
        const auto cell = _grid.find(CellKey(static_cast<std::int32_t>(std::floor(point.x / kCellSize)), static_cast<std::int32_t>(std::floor(point.y / kCellSize))));
        if (cell != _grid.end()) {
            for (const std::uint32_t handle : cell->second) {
                visit(handle);
            }
        }
        for (const std::uint32_t handle : _wide) {
            visit(handle);
        }
    }

    AreaRecord *AreaService::Find(std::uint32_t handle) {
        const auto found = _areas.find(handle);
        return found != _areas.end() ? found->second.get() : nullptr;
    }

    const AreaRecord *AreaService::Find(std::uint32_t handle) const {
        const auto found = _areas.find(handle);
        return found != _areas.end() ? found->second.get() : nullptr;
    }

    AreaRecord *AreaService::FindById(const std::string &id) {
        const auto found = _byId.find(id);
        if (found != _byId.end()) {
            return Find(found->second);
        }

        // A level GUID reads in any case and with or without `0x`, as `Door.find` takes one.
        const std::uint64_t guid = KCDC::Scripting::ParseLevelGuid(id);
        const auto level         = guid != 0 ? _byGuid.find(guid) : _byGuid.end();
        return level != _byGuid.end() ? Find(level->second) : nullptr;
    }

    bool AreaService::SharesWorld(const AreaRecord &area, MafiaNet::VirtualWorldId bodyWorld) {
        const std::optional<std::uint32_t> &world = area.definition.virtualWorld;
        return !world.has_value() || *world == MafiaNet::VIRTUAL_WORLD_GLOBAL || bodyWorld == MafiaNet::VIRTUAL_WORLD_GLOBAL || *world == bodyWorld;
    }

    AreaRecord *AreaService::Create(KCDC::Shared::Game::AreaDefinition definition, std::string &error) {
        const std::string &id = definition.id;
        if (id.empty()) {
            error = "an area needs a non-empty id";
            return nullptr;
        }
        if (KCDC::Scripting::ParseLevelGuid(id) != 0) {
            error = "'" + id + "' reads as a level GUID; pick an id that is not sixteen hex digits";
            return nullptr;
        }
        if (_byId.count(id) != 0) {
            error = "an area with id '" + id + "' already exists";
            return nullptr;
        }
        if (!definition.shape.IsValid()) {
            error = "the shape is not one the engine could build: a box needs min below max, a shape three to " + std::to_string(KCDC::Shared::Game::AreaShape::kMaxPoints) + " points and a height of 0 or more, a sphere a positive radius";
            return nullptr;
        }

        if (definition.name.empty()) {
            definition.name = definition.id;
        }
        auto area        = std::make_unique<AreaRecord>();
        area->definition = std::move(definition);
        RefreshGeometry(*area);
        if (!SyncCollision(*area, error)) {
            return nullptr;
        }
        _retestAll = true;
        return &Insert(std::move(area));
    }

    bool AreaService::Destroy(std::uint32_t handle) {
        AreaRecord *area = Find(handle);
        if (area == nullptr || area->IsLevel() || area->destroying) {
            return false;
        }

        // Everything goes out while the area still resolves, so a handler can read what it was. First
        // whatever this tick queued for it, in order -- a body that already left still gets its exit, one
        // that just arrived its entry -- then an exit for everyone still inside.
        area->destroying = true;
        for (std::size_t index = 0; index < _pending.size(); ++index) {
            if (_pending[index].handle == handle) {
                const Crossing queued  = _pending[index];
                _pending[index].handle = 0;
                Emit(queued);
            }
        }
        std::vector<Crossing> exits;
        for (const auto &occupant : area->occupants) {
            const auto tracked       = _tracked.find(occupant.first);
            const bool matchingWorld = tracked == _tracked.end() || SharesWorld(*area, tracked->second.virtualWorld);
            exits.push_back(Crossing {false, handle, occupant.first, occupant.second, matchingWorld});
        }
        for (const Crossing &exit : exits) {
            Emit(exit);
        }

        area = Find(handle);
        if (area == nullptr) {
            return true;
        }
        for (auto &entry : _tracked) {
            std::vector<std::uint32_t> &inside = entry.second.inside;
            inside.erase(std::remove(inside.begin(), inside.end(), handle), inside.end());
        }
        UnindexArea(*area);
        _byId.erase(area->definition.id);
        RemoveCollision(*area);
        _areas.erase(handle);
        return true;
    }

    bool AreaService::SetShape(AreaRecord &area, const KCDC::Shared::Game::AreaShape &shape) {
        if (area.IsLevel() || area.destroying || !shape.IsValid()) {
            return false;
        }
        const auto previous   = area.definition.shape;
        area.definition.shape = shape;
        std::string error;
        if (!SyncCollision(area, error)) {
            area.definition.shape = previous;
            return false;
        }
        UnindexArea(area);
        RefreshGeometry(area);
        IndexArea(area);
        _retestAll = true;
        return true;
    }

    void AreaService::SetEnabled(AreaRecord &area, bool enabled) {
        if (!area.destroying && area.definition.enabled != enabled) {
            const bool previous     = area.definition.enabled;
            area.definition.enabled = enabled;
            std::string error;
            if (!SyncCollision(area, error)) {
                area.definition.enabled = previous;
                throw std::runtime_error(error);
            }
            _retestAll              = true;
        }
    }

    void AreaService::SetVirtualWorld(AreaRecord &area, std::optional<MafiaNet::VirtualWorldId> virtualWorld) {
        if (!area.IsLevel() && !area.destroying && area.definition.virtualWorld != virtualWorld) {
            const auto previous          = area.definition.virtualWorld;
            area.definition.virtualWorld = virtualWorld;
            std::string error;
            if (!SyncCollision(area, error)) {
                area.definition.virtualWorld = previous;
                throw std::runtime_error(error);
            }
            _retestAll                   = true;
        }
    }

    void AreaService::RemoveCollision(AreaRecord &area) {
        if (area.collisionNetworkId != 0) {
            if (auto *replication = Framework::CoreModules::GetReplication()) {
                if (auto *entity = replication->GetEntity<Shared::Entities::AreaCollisionEntity>(area.collisionNetworkId)) {
                    replication->DestroyEntity(entity);
                }
            }
            area.collisionNetworkId = 0;
        }
    }

    bool AreaService::SyncCollision(AreaRecord &area, std::string &error) {
        if (!area.IsLevel() && area.definition.collision && !area.destroying) {
            Shared::Game::AreaCollisionMesh mesh;
            if (!Shared::Game::BuildAreaCollisionMesh(area.definition.shape, area.definition.collisionOptions, mesh, error)) {
                return false;
            }
        }
        if (area.IsLevel() || !area.definition.collision || !area.definition.enabled || area.destroying) {
            RemoveCollision(area);
            return true;
        }
        auto *replication = Framework::CoreModules::GetReplication();
        auto *entity      = replication ? replication->GetEntity<Shared::Entities::AreaCollisionEntity>(area.collisionNetworkId) : nullptr;
        if (!entity && replication) {
            entity = replication->CreateEntity<Shared::Entities::AreaCollisionEntity>();
        }
        if (!entity) {
            error = "Area collision could not be replicated.";
            return false;
        }
        area.collisionNetworkId = entity->GetNetworkID();
        Shared::Game::AreaDefinition boundary;
        boundary.id                     = "collision";
        boundary.shape                  = area.definition.shape;
        boundary.collision              = true;
        boundary.collisionOptions       = area.definition.collisionOptions;
        entity->definitionJson          = Shared::Game::AreaDefinitionToJson(boundary).dump();
        entity->position                = area.definition.shape.position;
        entity->streaming.alwaysVisible = true;
        entity->SetVirtualWorld(area.definition.virtualWorld.value_or(MafiaNet::VIRTUAL_WORLD_GLOBAL));
        return true;
    }

    bool AreaService::SetCollision(AreaRecord &area, bool collision, std::string &error) {
        if (area.IsLevel() || area.destroying) {
            error = "Only authored areas can have collision.";
            return false;
        }
        const bool previous       = area.definition.collision;
        area.definition.collision = collision;
        if (!SyncCollision(area, error)) {
            area.definition.collision = previous;
            return false;
        }
        return true;
    }

    bool AreaService::SetCollisionOptions(AreaRecord &area, const KCDC::Shared::Game::AreaCollisionOptions &options, std::string &error) {
        if (area.IsLevel() || area.destroying) {
            error = "Only authored areas can have collision.";
            return false;
        }
        if (!options.IsValid()) {
            error = Shared::Game::AreaCollisionOptions::ThicknessRangeError();
            return false;
        }
        // SyncCollision checks the shape against these options only while collision is on, as Area.create does.
        const auto previous              = area.definition.collisionOptions;
        area.definition.collisionOptions = options;
        if (!SyncCollision(area, error)) {
            area.definition.collisionOptions = previous;
            return false;
        }
        return true;
    }

    bool AreaService::Contains(const AreaRecord &area, const glm::vec3 &point) const {
        if (!area.IsLevel()) {
            return area.definition.shape.Contains(point, area.worldPoints);
        }
        if (area.level->areaClass != KCDC::Shared::Game::LevelAreaClass::AreaUnion) {
            return KCDC::Shared::Game::LevelAreaContains(*area.level, point);
        }
        // C_AreaUnion::IsPointInside (0x1805CCF2C): inside where any member is.
        return std::any_of(area.members.begin(), area.members.end(), [this, &point](std::uint32_t member) {
            const AreaRecord *found = Find(member);
            return found != nullptr && Contains(*found, point);
        });
    }

    std::vector<AreaRecord *> AreaService::At(const glm::vec3 &point, std::optional<MafiaNet::VirtualWorldId> virtualWorld) {
        std::vector<AreaRecord *> found;
        ForEachCandidate(point, [&](std::uint32_t handle) {
            AreaRecord *area = Find(handle);
            if (area == nullptr || !area->definition.enabled || area->destroying) {
                return;
            }
            if (virtualWorld.has_value() && !SharesWorld(*area, *virtualWorld)) {
                return;
            }
            if (Contains(*area, point)) {
                found.push_back(area);
            }
        });
        return found;
    }

    std::vector<AreaRecord *> AreaService::Nearby(const glm::vec3 &point, float radius, std::optional<MafiaNet::VirtualWorldId> virtualWorld) {
        std::vector<std::pair<float, AreaRecord *>> scored;
        for (auto &entry : _areas) {
            AreaRecord *area = entry.second.get();
            if (area->destroying || (virtualWorld.has_value() && !SharesWorld(*area, *virtualWorld))) {
                continue;
            }
            const float distance = KCDC::Shared::Game::DistanceToBox(point, area->boundsMin, area->boundsMax);
            if (distance <= radius) {
                scored.emplace_back(distance, area);
            }
        }
        std::sort(scored.begin(), scored.end(), [](const auto &left, const auto &right) {
            return left.first != right.first ? left.first < right.first : left.second->handle < right.second->handle;
        });
        std::vector<AreaRecord *> found;
        found.reserve(scored.size());
        for (const auto &entry : scored) {
            found.push_back(entry.second);
        }
        return found;
    }

    std::vector<AreaRecord *> AreaService::WithLabel(const std::string &label) {
        std::vector<AreaRecord *> found;
        for (AreaRecord *area : All(std::nullopt)) {
            if (std::any_of(area->definition.labels.begin(), area->definition.labels.end(), [&label](const std::string &own) {
                    return Framework::Utils::StringUtils::EqualsIgnoreCase(own, label);
                })) {
                found.push_back(area);
            }
        }
        return found;
    }

    std::vector<AreaRecord *> AreaService::All(std::optional<bool> level) {
        std::vector<AreaRecord *> found;
        for (auto &entry : _areas) {
            if (!entry.second->destroying && (!level.has_value() || entry.second->IsLevel() == *level)) {
                found.push_back(entry.second.get());
            }
        }
        std::sort(found.begin(), found.end(), [](const AreaRecord *left, const AreaRecord *right) {
            return left->handle < right->handle;
        });
        return found;
    }

    std::vector<AreaDoor> AreaService::DoorsOf(const AreaRecord &area) const {
        std::vector<AreaDoor> doors;
        if (!area.IsLevel()) {
            return doors;
        }
        for (std::uint32_t index = 0; index < area.level->doorCount; ++index) {
            const KCDC::Shared::Game::LevelAreaDoor &link = KCDC::Shared::Game::g_LevelAreaDoors[area.level->firstDoor + index];
            AreaDoor door;
            door.doorGuid = link.doorGuid;
            door.link     = link.link;
            if (const KCDC::Shared::Game::DoorPlacement *placement = KCDC::Shared::Game::FindDoorPlacement(_level.c_str(), link.doorGuid)) {
                door.name     = placement->name;
                door.position = glm::vec3(placement->positionX, placement->positionY, placement->positionZ);
            }
            doors.push_back(std::move(door));
        }
        return doors;
    }

    std::vector<AreaRecord *> AreaService::AreasForDoor(std::uint64_t doorGuid) {
        std::vector<AreaRecord *> found;
        const auto range = _byDoor.equal_range(doorGuid);
        for (auto link = range.first; link != range.second; ++link) {
            if (AreaRecord *area = Find(link->second)) {
                found.push_back(area);
            }
        }
        return found;
    }

    void AreaService::Update() {
        auto *replication = Framework::CoreModules::GetReplication();
        if (replication == nullptr) {
            return;
        }

        for (auto &entry : _tracked) {
            entry.second.seen = false;
        }

        // A body is only measured once its owner has reported a real pose; before that it sits at the origin.
        replication->ForEach<KCDC::Shared::Entities::HumanEntity>([this](KCDC::Shared::Entities::HumanEntity *human) {
            if (human->transformReady) {
                Follow(human->GetNetworkID(), AreaOccupantKind::Player, human->position, human->GetVirtualWorld());
            }
        });
        replication->ForEach<KCDC::Shared::Entities::HorseEntity>([this](KCDC::Shared::Entities::HorseEntity *horse) {
            Follow(horse->GetNetworkID(), AreaOccupantKind::Horse, horse->position, horse->GetVirtualWorld());
        });
        replication->ForEach<KCDC::Shared::Entities::CartEntity>([this](KCDC::Shared::Entities::CartEntity *cart) {
            Follow(cart->GetNetworkID(), AreaOccupantKind::Cart, cart->position, cart->GetVirtualWorld());
        });
        replication->ForEach<KCDC::Shared::Entities::NpcEntity>([this](KCDC::Shared::Entities::NpcEntity *npc) {
            if (npc->transformReady) {
                Follow(npc->GetNetworkID(), AreaOccupantKind::Npc, npc->position, npc->GetVirtualWorld());
            }
        });

        // A body that has gone leaves silently: its handle no longer resolves,
        // and its own despawn or disconnect event already says so.
        for (auto tracked = _tracked.begin(); tracked != _tracked.end();) {
            if (tracked->second.seen) {
                ++tracked;
                continue;
            }
            for (const std::uint32_t handle : tracked->second.inside) {
                if (AreaRecord *area = Find(handle)) {
                    area->occupants.erase(tracked->first);
                }
            }
            tracked = _tracked.erase(tracked);
        }
        _retestAll = false;
        RaisePending();
    }

    void AreaService::Follow(std::uint64_t networkId, AreaOccupantKind kind, const glm::vec3 &position, MafiaNet::VirtualWorldId virtualWorld) {
        auto emplaced     = _tracked.try_emplace(networkId);
        Tracked &tracked  = emplaced.first->second;
        const bool fresh  = emplaced.second;
        const glm::vec3 d = position - tracked.position;
        tracked.seen      = true;
        if (!fresh && !_retestAll && tracked.virtualWorld == virtualWorld && glm::dot(d, d) < kRetestDistanceSq) {
            return;
        }
        tracked.kind         = kind;
        tracked.position     = position;
        tracked.virtualWorld = virtualWorld;
        Evaluate(networkId, tracked);
    }

    void AreaService::Evaluate(std::uint64_t networkId, Tracked &tracked) {
        std::vector<std::uint32_t> inside;
        ForEachCandidate(tracked.position, [&](std::uint32_t handle) {
            const AreaRecord *area = Find(handle);
            if (area != nullptr && area->definition.enabled && !area->destroying && Contains(*area, tracked.position)) {
                inside.push_back(handle);
            }
        });
        std::sort(inside.begin(), inside.end());

        const auto cross = [&](std::uint32_t handle, bool enter) {
            AreaRecord *area = Find(handle);
            if (area == nullptr) {
                return;
            }
            if (enter) {
                area->occupants[networkId] = tracked.kind;
            }
            else {
                area->occupants.erase(networkId);
            }
            const bool matchingWorld = SharesWorld(*area, tracked.virtualWorld);
            _pending.push_back(Crossing {enter, handle, networkId, tracked.kind, matchingWorld});
        };

        // Both lists are sorted, so one merge walk finds the exits and the entries. Exits are raised first,
        // so a body stepping from one area straight into another leaves before it arrives.
        std::vector<std::uint32_t> entered;
        auto before = tracked.inside.begin();
        auto after  = inside.begin();
        while (before != tracked.inside.end() || after != inside.end()) {
            if (after == inside.end() || (before != tracked.inside.end() && *before < *after)) {
                cross(*before++, false);
            }
            else if (before == tracked.inside.end() || *after < *before) {
                entered.push_back(*after++);
            }
            else {
                ++before;
                ++after;
            }
        }
        for (const std::uint32_t handle : entered) {
            cross(handle, true);
        }
        tracked.inside = std::move(inside);
    }

    void AreaService::Emit(const Crossing &crossing) {
        KCDC::Scripting::Events::AreaCrossed(crossing.enter, crossing.handle, crossing.networkId, crossing.kind, crossing.matchingWorld);
    }

    void AreaService::RaisePending() {
        // By index, blanking each as it goes out: a handler may destroy an area, and `Destroy` raises that
        // area's queued crossings itself and blanks them here. Nothing is queued while this runs.
        for (std::size_t index = 0; index < _pending.size(); ++index) {
            const Crossing crossing = _pending[index];
            _pending[index].handle  = 0;
            if (crossing.handle != 0 && Find(crossing.handle) != nullptr) {
                Emit(crossing);
            }
        }
        _pending.clear();
    }
} // namespace KCDC::Features::Area
