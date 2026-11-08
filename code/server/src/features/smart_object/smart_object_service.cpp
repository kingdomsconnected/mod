#include "smart_object_service.h"

#include "features/level_edit/level_edit_module.h"
#include "features/level_edit/level_edit_verbs.h"

#include "shared/pose_math.h"
#include "shared/scripting/level_guid.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <mafianet/virtual_world.h>
#include <networking/replication/replication_manager.h>

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cstdlib>

namespace KCDC::Features::SmartObject {
    namespace {
        constexpr const char *kLogger = "SmartObject";

        /** A part's pose in the world, from the object's pose and the part's in the prefab's frame. */
        glm::vec3 PartPosition(const Shared::Game::SmartObjectPart &part, const glm::vec3 &position, const glm::quat &rotation) {
            return position + rotation * glm::vec3(part.posX, part.posY, part.posZ);
        }

        /** The index of the part `station` links to under `name`, or -1. */
        int LinkedPart(const Shared::Game::SmartObjectPart &station, std::string_view name) {
            for (std::size_t link = 0; link < station.linkCount; ++link) {
                if (station.links[link].name == name) {
                    return station.links[link].target;
                }
            }
            return -1;
        }

        /** The GUID part of a catalog id, `level:guid`. */
        std::uint64_t GuidOfId(std::string_view id) {
            const std::size_t colon = id.rfind(':');
            return colon == std::string_view::npos ? 0 : KCDC::Scripting::ParseLevelGuid(id.substr(colon + 1));
        }
    } // namespace

    SmartObjectService &SmartObjectService::Get() {
        static SmartObjectService service;
        return service;
    }

    void SmartObjectService::Install(const std::string &level) {
        _level = level;

        // Every station the level places, from every catalog, keyed by the
        // GUID a level edit takes it out by.
        for (const Shared::Game::CraftStation &station : Shared::Game::g_CraftStations) {
            if (station.level == level) {
                _levelStations[station.guid] = Station {station.guid, station.kind == Shared::Game::CraftKind::Alchemy ? Shared::Game::SmartObjectKind::Alchemy : Shared::Game::SmartObjectKind::Smithing, glm::vec3(station.x, station.y, station.z), 0};
            }
        }
        for (const Shared::Game::DiceTable &table : Shared::Game::g_DiceTables) {
            if (table.level == level) {
                _levelStations[table.guid] = Station {table.guid, Shared::Game::SmartObjectKind::Dice, glm::vec3(table.x, table.y, table.z), 0};
            }
        }
        for (const Shared::Game::PreparationStation &station : Shared::Game::g_PreparationStations) {
            if (station.level == level && station.kind != Shared::Game::PreparationKind::Cooking) {
                const std::uint64_t guid = GuidOfId(station.id);
                _levelStations[guid]     = Station {guid, station.kind == Shared::Game::PreparationKind::Drying ? Shared::Game::SmartObjectKind::Drying : Shared::Game::SmartObjectKind::Smoking, glm::vec3(station.x, station.y, station.z), 0};
            }
        }
        Framework::Logging::GetLogger(kLogger)->info("{} smart object blueprint(s); {} station(s) the level places in '{}'", Shared::Game::g_SmartObjectBlueprints.size(), _levelStations.size(), level);
    }

    bool SmartObjectService::IsLive(const Placed &placed) {
        auto *replication = Framework::CoreModules::GetReplication();
        return replication != nullptr && replication->GetEntity<Shared::Entities::SmartObjectEntity>(placed.networkId) != nullptr;
    }

    bool SmartObjectService::Sees(MafiaNet::VirtualWorldId placedWorld, MafiaNet::VirtualWorldId virtualWorld) {
        // As replication sees it, so a player who sees a station can use it.
        return MafiaNet::VirtualWorldsCanSee(placedWorld, virtualWorld);
    }

    void SmartObjectService::Update() {
        // A resource that stops destroys the objects it placed without
        // telling anyone; their station rows go with them.
        std::erase_if(_placed, [](const auto &entry) {
            return !IsLive(*entry.second);
        });

        // Clients build a smart object once, from its construction, and every
        // station row is posed from the placement. A script that moved one or
        // took it to another world through the inherited Entity setters would
        // leave the two disagreeing, so it is put back.
        auto *replication = Framework::CoreModules::GetReplication();
        for (const auto &[networkId, placed] : _placed) {
            auto *object = replication != nullptr ? replication->GetEntity<Shared::Entities::SmartObjectEntity>(networkId) : nullptr;
            if (object == nullptr) {
                continue;
            }
            const glm::quat rotation = glm::normalize(placed->definition.rotation);
            if (object->position != placed->definition.position || object->rotation != rotation || object->GetVirtualWorld() != placed->virtualWorld) {
                Framework::Logging::GetLogger(kLogger)->warn("Smart object {} was moved or changed world; it stands where it was placed", networkId);
                object->position = placed->definition.position;
                object->rotation = rotation;
                object->SetVirtualWorld(placed->virtualWorld);
            }
        }
    }

    Shared::Entities::SmartObjectEntity *SmartObjectService::Spawn(const Shared::Game::SmartObjectDefinition &definition, MafiaNet::VirtualWorldId virtualWorld, std::string &error) {
        const Shared::Game::SmartObjectBlueprint *blueprint = Shared::Game::FindSmartObjectBlueprint(definition.blueprint);
        if (blueprint == nullptr) {
            error = "'" + definition.blueprint + "' is not a smart object blueprint";
            return nullptr;
        }
        if (!Shared::Game::IsFinite(definition.position) || !Shared::Game::IsUsableRotation(definition.rotation)) {
            error = "the position or rotation is not finite";
            return nullptr;
        }
        if (definition.name.size() > Shared::Game::SmartObjectDefinition::kMaxNameLength) {
            error = "the name is longer than 64 bytes";
            return nullptr;
        }
        auto *replication = Framework::CoreModules::GetReplication();
        auto *object      = replication != nullptr ? replication->CreateEntity<Shared::Entities::SmartObjectEntity>() : nullptr;
        if (object == nullptr) {
            error = "the smart object entity could not be created";
            return nullptr;
        }

        const glm::quat rotation = glm::normalize(definition.rotation);
        object->blueprint        = Shared::Game::SmartObjectBlueprintIndex(blueprint);
        object->serial           = _nextSerial++;
        object->position         = definition.position;
        object->rotation         = rotation;
        object->streaming.range  = Shared::Entities::SmartObjectEntity::kStreamRange;
        object->SetVirtualWorld(virtualWorld);

        auto placed                  = std::make_unique<Placed>();
        placed->definition           = definition;
        placed->definition.blueprint = std::string(blueprint->name);
        placed->definition.rotation  = rotation;
        placed->networkId            = object->GetNetworkID();
        placed->virtualWorld         = virtualWorld;
        placed->serial               = object->serial;
        placed->level                = _level;

        // A station gets the row the level's own of its kind have, so every
        // rule that judges those judges this one.
        if (blueprint->stationPart >= 0) {
            const Shared::Game::SmartObjectPart &station = blueprint->parts[blueprint->stationPart];
            const std::uint64_t guid                     = object->PartGuid(static_cast<std::size_t>(blueprint->stationPart));
            const glm::vec3 at                           = PartPosition(station, definition.position, rotation);
            const glm::vec3 forward                      = rotation * glm::quat(station.rotW, station.rotX, station.rotY, station.rotZ) * glm::vec3(0.0F, 1.0F, 0.0F);
            const glm::vec2 flat                         = glm::length(glm::vec2(forward)) > 1e-4F ? glm::normalize(glm::vec2(forward)) : glm::vec2(0.0F, 1.0F);
            placed->id                                   = _level + ":" + KCDC::Scripting::FormatLevelGuid(guid);
            switch (blueprint->kind) {
            case Shared::Game::SmartObjectKind::Alchemy:
            case Shared::Game::SmartObjectKind::Smithing:
                placed->craft = Shared::Game::CraftStation {placed->id, placed->level, guid, blueprint->kind == Shared::Game::SmartObjectKind::Alchemy ? Shared::Game::CraftKind::Alchemy : Shared::Game::CraftKind::Smithing, "", at.x, at.y, at.z, flat.x, flat.y};
                break;
            case Shared::Game::SmartObjectKind::Dice: {
                // Where the dice catalog puts a table: midway between the two
                // seats' alignment points, which the game sits the players at.
                const int first      = LinkedPart(station, "seat1");
                const int second     = LinkedPart(station, "seat2");
                const int firstSeat  = LinkedPart(station, "seat1_SO");
                const int secondSeat = LinkedPart(station, "seat2_SO");
                if (first >= 0 && second >= 0 && firstSeat >= 0 && secondSeat >= 0) {
                    const glm::vec3 middle = (PartPosition(blueprint->parts[first], definition.position, rotation) + PartPosition(blueprint->parts[second], definition.position, rotation)) * 0.5F;
                    placed->dice           = Shared::Game::DiceTable {placed->level, guid, object->PartGuid(static_cast<std::size_t>(firstSeat)), object->PartGuid(static_cast<std::size_t>(secondSeat)), middle.x, middle.y, middle.z};
                }
                break;
            }
            case Shared::Game::SmartObjectKind::Drying:
            case Shared::Game::SmartObjectKind::Smoking:
                placed->preparation = Shared::Game::PreparationStation {placed->id, placed->level, blueprint->kind == Shared::Game::SmartObjectKind::Drying ? Shared::Game::PreparationKind::Drying : Shared::Game::PreparationKind::Smoking, at.x, at.y, at.z};
                break;
            case Shared::Game::SmartObjectKind::Seat:
            case Shared::Game::SmartObjectKind::Bed:
            case Shared::Game::SmartObjectKind::Sharpening: break;
            }
        }

        _placed.emplace(placed->networkId, std::move(placed));
        Framework::Logging::GetLogger(kLogger)->info("Placed smart object {} ('{}', serial {}) in world {} at {:.2f}, {:.2f}, {:.2f}", object->GetNetworkID(), blueprint->name, object->serial, virtualWorld, definition.position.x, definition.position.y, definition.position.z);
        return object;
    }

    void SmartObjectService::Despawn(Shared::Entities::SmartObjectEntity &object) {
        _placed.erase(object.GetNetworkID());
        if (auto *replication = Framework::CoreModules::GetReplication()) {
            replication->DestroyEntity(&object);
        }
    }

    const Shared::Game::SmartObjectDefinition *SmartObjectService::DefinitionOf(const Shared::Entities::SmartObjectEntity &object) const {
        const auto found = _placed.find(const_cast<Shared::Entities::SmartObjectEntity &>(object).GetNetworkID());
        return found != _placed.end() ? &found->second->definition : nullptr;
    }

    std::vector<Shared::Entities::SmartObjectEntity *> SmartObjectService::Collect(std::optional<MafiaNet::VirtualWorldId> virtualWorld) const {
        std::vector<Shared::Entities::SmartObjectEntity *> objects;
        auto *replication = Framework::CoreModules::GetReplication();
        for (const auto &[networkId, placed] : _placed) {
            auto *object = replication != nullptr ? replication->GetEntity<Shared::Entities::SmartObjectEntity>(networkId) : nullptr;
            if (object != nullptr && (!virtualWorld.has_value() || placed->virtualWorld == *virtualWorld)) {
                objects.push_back(object);
            }
        }
        return objects;
    }

    bool SmartObjectService::IsRemoved(std::uint64_t guid, MafiaNet::VirtualWorldId virtualWorld) const {
        return _levelStations.contains(guid) && KCDC::Features::LevelEdit::LevelEditModule::Get().RemovalOf(guid, virtualWorld) != nullptr;
    }

    const SmartObjectService::Station *SmartObjectService::LevelStation(std::uint64_t guid) const {
        const auto found = _levelStations.find(guid);
        return found != _levelStations.end() ? &found->second : nullptr;
    }

    std::vector<SmartObjectService::Station> SmartObjectService::Stations(MafiaNet::VirtualWorldId virtualWorld) const {
        std::vector<Station> stations;
        for (const auto &[guid, station] : _levelStations) {
            if (!IsRemoved(guid, virtualWorld)) {
                stations.push_back(station);
            }
        }
        for (const auto &[networkId, placed] : _placed) {
            if (!Sees(placed->virtualWorld, virtualWorld) || !IsLive(*placed)) {
                continue;
            }
            const Shared::Game::SmartObjectBlueprint *blueprint = Shared::Game::FindSmartObjectBlueprint(placed->definition.blueprint);
            if (placed->craft) {
                stations.push_back(Station {placed->craft->guid, blueprint->kind, glm::vec3(placed->craft->x, placed->craft->y, placed->craft->z), networkId});
            }
            else if (placed->dice) {
                stations.push_back(Station {placed->dice->guid, blueprint->kind, glm::vec3(placed->dice->x, placed->dice->y, placed->dice->z), networkId});
            }
            else if (placed->preparation) {
                stations.push_back(Station {Shared::Game::SmartObjectPartGuid(placed->serial, static_cast<std::size_t>(blueprint->stationPart)), blueprint->kind, glm::vec3(placed->preparation->x, placed->preparation->y, placed->preparation->z), networkId});
            }
        }
        return stations;
    }

    const Shared::Game::CraftStation *SmartObjectService::FindCraftStation(std::string_view id) const {
        if (const auto *station = Shared::Game::FindCraftStation(id)) {
            return station;
        }
        for (const auto &[networkId, placed] : _placed) {
            if (placed->craft && placed->craft->id == id && IsLive(*placed)) {
                return &*placed->craft;
            }
        }
        return nullptr;
    }

    const Shared::Game::CraftStation *SmartObjectService::FindCraftStation(std::uint64_t guid, Shared::Game::CraftKind kind, MafiaNet::VirtualWorldId virtualWorld) const {
        if (IsRemoved(guid, virtualWorld)) {
            return nullptr;
        }
        for (const Shared::Game::CraftStation &station : Shared::Game::g_CraftStations) {
            if (station.guid == guid && station.level == _level && station.kind == kind) {
                return &station;
            }
        }
        for (const auto &[networkId, placed] : _placed) {
            if (placed->craft && placed->craft->guid == guid && placed->craft->kind == kind && Sees(placed->virtualWorld, virtualWorld) && IsLive(*placed)) {
                return &*placed->craft;
            }
        }
        return nullptr;
    }

    std::vector<const Shared::Game::CraftStation *> SmartObjectService::CraftStations(MafiaNet::VirtualWorldId virtualWorld) const {
        std::vector<const Shared::Game::CraftStation *> stations;
        for (const Shared::Game::CraftStation &station : Shared::Game::g_CraftStations) {
            if (station.level == _level && !IsRemoved(station.guid, virtualWorld)) {
                stations.push_back(&station);
            }
        }
        for (const auto &[networkId, placed] : _placed) {
            if (placed->craft && Sees(placed->virtualWorld, virtualWorld) && IsLive(*placed)) {
                stations.push_back(&*placed->craft);
            }
        }
        return stations;
    }

    const Shared::Game::DiceTable *SmartObjectService::FindDiceTable(std::uint64_t guid, MafiaNet::VirtualWorldId virtualWorld) const {
        for (const Shared::Game::DiceTable *table : DiceTables(virtualWorld)) {
            if (table->guid == guid) {
                return table;
            }
        }
        return nullptr;
    }

    std::vector<const Shared::Game::DiceTable *> SmartObjectService::DiceTables(MafiaNet::VirtualWorldId virtualWorld) const {
        std::vector<const Shared::Game::DiceTable *> tables;
        for (const Shared::Game::DiceTable &table : Shared::Game::g_DiceTables) {
            if (table.level == _level && !IsRemoved(table.guid, virtualWorld)) {
                tables.push_back(&table);
            }
        }
        for (const auto &[networkId, placed] : _placed) {
            if (placed->dice && Sees(placed->virtualWorld, virtualWorld) && IsLive(*placed)) {
                tables.push_back(&*placed->dice);
            }
        }
        return tables;
    }

    std::string SmartObjectService::PreparationStationAt(Shared::Game::PreparationKind kind, const glm::vec3 &position, float reachSquared, MafiaNet::VirtualWorldId virtualWorld) const {
        std::string nearest;
        float nearestSquared = reachSquared;
        const auto consider  = [&](const Shared::Game::PreparationStation &station) {
            const glm::vec3 offset = position - glm::vec3(station.x, station.y, station.z);
            if (station.kind == kind && glm::dot(offset, offset) <= nearestSquared) {
                nearest        = std::string(station.id);
                nearestSquared = glm::dot(offset, offset);
            }
        };
        for (const Shared::Game::PreparationStation &station : Shared::Game::g_PreparationStations) {
            if (station.level == _level && !IsRemoved(GuidOfId(station.id), virtualWorld)) {
                consider(station);
            }
        }
        for (const auto &[networkId, placed] : _placed) {
            if (placed->preparation && Sees(placed->virtualWorld, virtualWorld) && IsLive(*placed)) {
                consider(*placed->preparation);
            }
        }
        return nearest;
    }

    bool SmartObjectService::AtGrindstone(const glm::vec3 &position, float reachSquared, MafiaNet::VirtualWorldId) const {
        // The level's grindstones are not editable stations: none can be taken out, and none is placed.
        return std::any_of(Shared::Game::g_SharpeningStations.begin(), Shared::Game::g_SharpeningStations.end(), [&](const Shared::Game::SharpeningStation &station) {
            const glm::vec3 offset = position - glm::vec3(station.x, station.y, station.z);
            return station.level == _level && glm::dot(offset, offset) <= reachSquared;
        });
    }

    bool SmartObjectService::Remove(std::uint64_t guid, MafiaNet::VirtualWorldId virtualWorld, std::string &error) {
        const Station *station = LevelStation(guid);
        if (station == nullptr) {
            error = "the level places no station with that GUID";
            return false;
        }
        // Already out, here or from the global world: a second edit would only list it twice.
        if (IsRemoved(guid, virtualWorld)) {
            return true;
        }
        Shared::Game::LevelEditDefinition definition;
        definition.kind   = Shared::Game::LevelEditKind::Entity;
        definition.guid   = guid;
        definition.at     = station->position;
        definition.hidden = true;
        if (KCDC::Features::LevelEdit::LevelEdits::Apply(definition, virtualWorld, error) == nullptr) {
            return false;
        }
        Framework::Logging::GetLogger(kLogger)->info("Station {:016x} taken out of world {}", guid, virtualWorld);
        return true;
    }

    bool SmartObjectService::Unremove(std::uint64_t guid, MafiaNet::VirtualWorldId virtualWorld) {
        auto *edit = KCDC::Features::LevelEdit::LevelEditModule::Get().RemovalOf(guid, virtualWorld);
        if (edit == nullptr || edit->GetVirtualWorld() != virtualWorld || LevelStation(guid) == nullptr) {
            return false;
        }
        KCDC::Features::LevelEdit::LevelEdits::Restore(edit);
        return true;
    }

    std::vector<std::pair<SmartObjectService::Station, MafiaNet::VirtualWorldId>> SmartObjectService::Removed(std::optional<MafiaNet::VirtualWorldId> virtualWorld) const {
        std::vector<std::pair<Station, MafiaNet::VirtualWorldId>> removed;
        for (const auto *edit : KCDC::Features::LevelEdit::LevelEditModule::Get().Removals()) {
            const Station *station = LevelStation(edit->guid);
            if (station != nullptr && (!virtualWorld.has_value() || edit->GetVirtualWorld() == *virtualWorld)) {
                removed.emplace_back(*station, edit->GetVirtualWorld());
            }
        }
        return removed;
    }
} // namespace KCDC::Features::SmartObject
