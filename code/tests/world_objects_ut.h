#pragma once

#include "features/prop/prop_editor_document.h"
#include "features/prop/prop_editor_world_objects.h"
#include "shared/features/door/door_definition.h"
#include "shared/features/door/door_entity.h"
#include "shared/features/ground_item/ground_item_definition.h"
#include "shared/features/smart_object/smart_object_entity.h"
#include "shared/features/world_builder/world_document.h"
#include "shared/game/door_catalog.h"
#include "shared/game/inventory_property_classes.h"
#include "shared/game/item_classes.h"
#include "shared/game/prop_models.h"
#include "shared/game/smart_object_blueprints.h"

#include <mafianet/bit_stream.h>
#include <networking/replication/network_entity.h>

#include <nlohmann/json.hpp>

#include <map>
#include <set>
#include <string>
#include <string_view>

MODULE(world_objects, {
    // The first shipped class the game stacks, for a placement of more than one.
    const auto stackable = []() -> const KCDC::Shared::Game::ItemClassEntry & {
        for (const KCDC::Shared::Game::ItemClassEntry &item : KCDC::Shared::Game::g_ItemClasses) {
            const KCDC::Shared::Game::InventoryPropertyClass *properties = KCDC::Shared::Game::FindInventoryPropertyClass(item.guid);
            if (properties != nullptr && properties->nativeStackable && !properties->questItem && item.category != "ItemAlias") {
                return item;
            }
        }
        return KCDC::Shared::Game::g_ItemClasses.front();
    };

    IT("mines a door model for every placed leaf, hinged the way its placements swing", {
        EQUALS(KCDC::Shared::Game::g_DoorModels.size() > 50, true);
        std::size_t modelled = 0;
        for (const KCDC::Shared::Game::DoorPlacement &placement : KCDC::Shared::Game::g_DoorPlacements) {
            if (placement.model == KCDC::Shared::Game::kNoDoorModel) {
                continue;
            }
            ++modelled;
            EQUALS(KCDC::Shared::Game::DoorModelAt(placement.model) != nullptr, true);
        }
        // A handful of cinematic doors hang no leaf; every other one names its model.
        EQUALS(modelled + 8 > KCDC::Shared::Game::g_DoorPlacements.size(), true);
        for (const KCDC::Shared::Game::DoorModel &model : KCDC::Shared::Game::g_DoorModels) {
            const std::string_view path(model.path);
            EQUALS(path.starts_with("objects/characters/assets/doors/"), true);
            EQUALS((std::string_view(model.animSet) == "VillageLeft" || std::string_view(model.animSet) == "VillageRight"), true);
            EQUALS((std::string_view(model.helpers) == "DoorLeft" || std::string_view(model.helpers) == "DoorRight"), true);
            EQUALS(std::string_view(model.smartObjectType).empty(), false);
        }
        EQUALS(KCDC::Shared::Game::FindDoorModelIndex("Objects\\Characters\\Assets\\Doors\\LVL1_DOOR_A_LEFT.cdf") != KCDC::Shared::Game::kNoDoorModel, true);
        EQUALS(KCDC::Shared::Game::ResolveDoorModel("lvl1_door_a_left"), KCDC::Shared::Game::FindDoorModelIndex("objects/characters/assets/doors/lvl1_door_a_left.cdf"));
        EQUALS(KCDC::Shared::Game::ResolveDoorModel("not_a_door"), KCDC::Shared::Game::kNoDoorModel);
    });

    IT("reads a hung door's definition with the level's defaults, and refuses what no client could hang", {
        KCDC::Shared::Game::DoorDefinition door;
        std::string error;
        EQUALS(KCDC::Shared::Game::DoorDefinitionFromJson({{"model", "lvl1_door_a_left"}, {"position", {1, 2, 3}}}, door, error), true);
        EQUALS(door.locked, false);
        EQUALS(door.lock, KCDC::Shared::Game::DoorLockType::ManualKey);
        EQUALS(door.lockpickable, true);
        EQUALS(door.lockDifficulty < 0.0F, true);

        const nlohmann::json full {{"model", "objects/characters/assets/doors/lvl1_door_a_right.cdf"}, {"position", {{"x", 1}, {"y", 2}, {"z", 3}}}, {"rotation", {0, 0, 0.7071, 0.7071}}, {"name", "jail"}, {"locked", true}, {"lock", "key"}, {"lockpickable", false},
            {"lockDifficulty", 0.8}, {"keyItem", "d14a857c-e2fb-4a8a-a54b-79dde42d06cd"}};
        EQUALS(KCDC::Shared::Game::DoorDefinitionFromJson(full, door, error), true);
        EQUALS(door.name, std::string("jail"));
        EQUALS(door.locked, true);
        EQUALS(door.lock, KCDC::Shared::Game::DoorLockType::Key);
        EQUALS(door.lockpickable, false);
        EQUALS(door.keyItem, std::string("d14a857c-e2fb-4a8a-a54b-79dde42d06cd"));

        KCDC::Shared::Game::DoorDefinition again;
        EQUALS(KCDC::Shared::Game::DoorDefinitionFromJson(KCDC::Shared::Game::DoorDefinitionToJson(door), again, error), true);
        EQUALS(again.lock, door.lock);
        EQUALS(again.lockDifficulty, door.lockDifficulty);
        EQUALS(again.name, door.name);

        for (const nlohmann::json &bad :
            {nlohmann::json {{"position", {1, 2, 3}}}, nlohmann::json {{"model", "barrel"}, {"position", {1, 2, 3}}}, nlohmann::json {{"model", "lvl1_door_a_left"}}, nlohmann::json {{"model", "lvl1_door_a_left"}, {"position", {1, 2, 3}}, {"lock", "padlock"}},
                nlohmann::json {{"model", "lvl1_door_a_left"}, {"position", {1, 2, 3}}, {"lockDifficulty", 2}}, nlohmann::json {{"model", "lvl1_door_a_left"}, {"position", {1, 2, 3}}, {"name", std::string(65, 'n')}}}) {
            EQUALS(KCDC::Shared::Game::DoorDefinitionFromJson(bad, door, error), false);
        }
    });

    IT("puts a hung door's leaf and lock on the wire, and nothing for a level door", {
        KCDC::Shared::Entities::DoorEntity sent;
        sent.doorGuid               = KCDC::Shared::Entities::DoorEntity::SpawnedGuid(7);
        sent.spawned.model          = 3;
        sent.spawned.rotation       = glm::quat(0.7071F, 0.0F, 0.0F, 0.7071F);
        sent.spawned.lockType       = KCDC::Shared::Game::DoorLockType::Key;
        sent.spawned.lockpickable   = false;
        sent.spawned.lockDifficulty = 0.4F;
        sent.spawned.keyItem        = "d14a857c-e2fb-4a8a-a54b-79dde42d06cd";
        sent.locked                 = true;

        MafiaNet::BitStream stream;
        Framework::Networking::Replication::FieldSerializer writer(&stream, true);
        sent.OnSerializeConstruction(writer);
        KCDC::Shared::Entities::DoorEntity received;
        Framework::Networking::Replication::FieldSerializer reader(&stream, false);
        received.OnSerializeConstruction(reader);
        EQUALS(reader.Good(), true);
        EQUALS(received.IsSpawned(), true);
        EQUALS(KCDC::Shared::Entities::DoorEntity::IsSpawnedGuid(received.doorGuid), true);
        EQUALS(received.spawned.model, std::uint16_t {3});
        EQUALS(received.spawned.lockType, KCDC::Shared::Game::DoorLockType::Key);
        EQUALS(received.spawned.lockpickable, false);
        EQUALS(received.spawned.keyItem, sent.spawned.keyItem);
        EQUALS(received.locked, true);

        KCDC::Shared::Entities::DoorEntity level;
        level.doorGuid = 0x00244decccf5f78aULL;
        MafiaNet::BitStream levelStream;
        Framework::Networking::Replication::FieldSerializer levelWriter(&levelStream, true);
        level.OnSerializeConstruction(levelWriter);
        KCDC::Shared::Entities::DoorEntity levelReceived;
        Framework::Networking::Replication::FieldSerializer levelReader(&levelStream, false);
        levelReceived.OnSerializeConstruction(levelReader);
        EQUALS(levelReader.Good(), true);
        EQUALS(levelReceived.IsSpawned(), false);
        EQUALS(KCDC::Shared::Entities::DoorEntity::IsSpawnedGuid(levelReceived.doorGuid), false);
    });

    IT("mines every interactive prefab into a blueprint whose parts link only each other", {
        EQUALS(KCDC::Shared::Game::g_SmartObjectBlueprints.size() >= 20, true);
        std::set<KCDC::Shared::Game::SmartObjectKind> kinds;
        for (const KCDC::Shared::Game::SmartObjectBlueprint &blueprint : KCDC::Shared::Game::g_SmartObjectBlueprints) {
            kinds.insert(blueprint.kind);
            EQUALS((blueprint.partCount > 0 && blueprint.partCount < 256), true);
            for (std::size_t index = 0; index < blueprint.partCount; ++index) {
                const KCDC::Shared::Game::SmartObjectPart &part = blueprint.parts[index];
                EQUALS((part.entityClass != "PrefabPort" && part.entityClass != "SchedulerHub" && part.entityClass != "GhostDummy"), true);
                for (std::size_t link = 0; link < part.linkCount; ++link) {
                    EQUALS(part.links[link].target < blueprint.partCount, true);
                }
            }
            const bool station = blueprint.kind != KCDC::Shared::Game::SmartObjectKind::Seat && blueprint.kind != KCDC::Shared::Game::SmartObjectKind::Bed;
            EQUALS(blueprint.stationPart >= 0, station);
            EQUALS(KCDC::Shared::Game::FindSmartObjectBlueprint(blueprint.name), &blueprint);
        }
        for (const KCDC::Shared::Game::SmartObjectKind kind : {KCDC::Shared::Game::SmartObjectKind::Seat, KCDC::Shared::Game::SmartObjectKind::Bed, KCDC::Shared::Game::SmartObjectKind::Dice, KCDC::Shared::Game::SmartObjectKind::Alchemy,
                 KCDC::Shared::Game::SmartObjectKind::Smithing, KCDC::Shared::Game::SmartObjectKind::Drying}) {
            EQUALS(kinds.contains(kind), true);
        }

        // The dice table is the part the client's dice module and the dice catalog find a table by.
        const KCDC::Shared::Game::SmartObjectBlueprint *dice = KCDC::Shared::Game::FindSmartObjectBlueprint("table/TABLE_DICE");
        EQUALS(dice != nullptr, true);
        const KCDC::Shared::Game::SmartObjectPart &table = dice->parts[dice->stationPart];
        std::set<std::string_view> links;
        for (std::size_t link = 0; link < table.linkCount; ++link) {
            links.insert(table.links[link].name);
        }
        for (const char *name : {"seat1", "seat2", "seat1_SO", "seat2_SO", "dice_cup"}) {
            EQUALS(links.contains(name), true);
        }

        // An alchemy table's recipe book is a fixture the client lets spawn.
        const KCDC::Shared::Game::SmartObjectBlueprint *alchemy = KCDC::Shared::Game::FindSmartObjectBlueprint("profession/apothecary/alchemyTable_wip");
        bool book                                               = false;
        for (std::size_t index = 0; index < alchemy->partCount; ++index) {
            book = book || (alchemy->parts[index].entityClass == "ItemSlot" && alchemy->parts[index].fixture);
        }
        EQUALS(book, true);

        // Its kettle holder is a placed `GeomEntity`, whose model the client loads, and its bellows load the model the level's own tables do.
        bool holder  = false;
        bool bellows = false;
        for (std::size_t index = 0; index < alchemy->partCount; ++index) {
            const KCDC::Shared::Game::SmartObjectPart &part = alchemy->parts[index];
            holder                                          = holder || part.geometry.ends_with("alchemy_kettle_halter.cdf");
            for (std::size_t property = 0; property < part.propertyCount; ++property) {
                bellows = bellows || (part.properties[property].key == "object_Model" && part.properties[property].value == "objects/characters/assets/bellows/bellows.chr");
            }
        }
        EQUALS(holder, true);
        EQUALS(bellows, true);

        // A smithy's station stands in the base it nests, and reaches the bag and the coal of the forge built around it through the base's ports.
        const KCDC::Shared::Game::SmartObjectBlueprint *smithy = KCDC::Shared::Game::FindSmartObjectBlueprint("profession/blacksmith/smithy_workshop");
        EQUALS(smithy != nullptr, true);
        const KCDC::Shared::Game::SmartObjectPart &smithery = smithy->parts[smithy->stationPart];
        std::map<std::string_view, std::string_view> forge;
        for (std::size_t link = 0; link < smithery.linkCount; ++link) {
            forge[smithery.links[link].name] = smithy->parts[smithery.links[link].target].geometry;
        }
        EQUALS(forge["forgeBag"], std::string_view("objects/characters/assets/forge_bag/forge_bag.cdf"));
        EQUALS(forge["forgeCoal"], std::string_view("objects/manmade/structures/industrial/smitheries/coal_forge.cgf"));

        // A bench seat's own stance properties ride along, nested ones included.
        const KCDC::Shared::Game::SmartObjectBlueprint *bench = KCDC::Shared::Game::FindSmartObjectBlueprint("Bench/bench_2places_low");
        bool nested                                           = false;
        for (std::size_t index = 0; index < bench->partCount; ++index) {
            for (std::size_t property = 0; property < bench->parts[index].propertyCount; ++property) {
                nested = nested || bench->parts[index].properties[property].key == "Script.esBedTypes";
            }
        }
        EQUALS(nested, true);
    });

    IT("mints every placed part a GUID no level entity and no other part has", {
        const std::uint64_t first = KCDC::Shared::Game::SmartObjectPartGuid(1, 0);
        EQUALS(KCDC::Shared::Game::IsSmartObjectGuid(first), true);
        EQUALS(first != KCDC::Shared::Game::SmartObjectPartGuid(1, 1), true);
        EQUALS(first != KCDC::Shared::Game::SmartObjectPartGuid(2, 0), true);
        EQUALS(KCDC::Shared::Game::IsSmartObjectGuid(KCDC::Shared::Entities::DoorEntity::SpawnedGuid(1)), false);
        EQUALS(KCDC::Shared::Game::IsSmartObjectGuid(0x0d5fede4c00a0646ULL), false);

        KCDC::Shared::Entities::SmartObjectEntity sent;
        sent.blueprint = 5;
        sent.serial    = 42;
        sent.position  = glm::vec3(10.5F, 20.25F, 30.125F);
        sent.rotation  = glm::quat(0.9238795F, 0.0F, 0.0F, 0.3826834F);
        MafiaNet::BitStream stream;
        Framework::Networking::Replication::FieldSerializer writer(&stream, true);
        sent.OnSerializeConstruction(writer);
        KCDC::Shared::Entities::SmartObjectEntity received;
        Framework::Networking::Replication::FieldSerializer reader(&stream, false);
        received.OnSerializeConstruction(reader);
        EQUALS(reader.Good(), true);
        EQUALS(received.blueprint, std::uint16_t {5});
        EQUALS(received.PartGuid(3), sent.PartGuid(3));
        EQUALS(received.position.z, 30.125F);
        EQUALS(received.rotation.w, sent.rotation.w);
    });

    IT("reads smart object and item placements, and refuses what no client could build", {
        KCDC::Shared::Game::SmartObjectDefinition object;
        std::string error;
        EQUALS(KCDC::Shared::Game::SmartObjectDefinitionFromJson({{"blueprint", "bed/BED_LOW"}, {"position", {1, 2, 3}}, {"name", "inn bed"}}, object, error), true);
        EQUALS(object.blueprint, std::string("Bed/bed_low"));
        EQUALS(KCDC::Shared::Game::SmartObjectDefinitionFromJson({{"blueprint", "Bed/bed_nope"}, {"position", {1, 2, 3}}}, object, error), false);
        EQUALS(KCDC::Shared::Game::SmartObjectDefinitionFromJson({{"blueprint", "Bed/bed_low"}}, object, error), false);

        KCDC::Shared::Game::ItemPlacementDefinition item;
        EQUALS(KCDC::Shared::Game::ItemPlacementDefinitionFromJson({{"item", "arrow_crude"}, {"position", {1, 2, 3}}, {"amount", 12}, {"name", "quiver"}}, item, error), true);
        EQUALS(item.amount, std::uint32_t {12});
        EQUALS(item.health < 0.0F, true);
        KCDC::Shared::Game::ItemPlacementDefinition again;
        EQUALS(KCDC::Shared::Game::ItemPlacementDefinitionFromJson(KCDC::Shared::Game::ItemPlacementDefinitionToJson(item), again, error), true);
        EQUALS(again.name, std::string("quiver"));
        for (const nlohmann::json &bad : {nlohmann::json {{"position", {1, 2, 3}}}, nlohmann::json {{"item", "apple"}}, nlohmann::json {{"item", "apple"}, {"position", {1, 2, 3}}, {"amount", 0}}, nlohmann::json {{"item", "apple"}, {"position", {1, 2, 3}}, {"health", 1.5}}}) {
            EQUALS(KCDC::Shared::Game::ItemPlacementDefinitionFromJson(bad, item, error), false);
        }
    });

    IT("validates and exports doors, smart objects and items, and copies them as API calls", {
        nlohmann::json map {{"format", KCDC::Shared::WorldBuilder::kResourceFormat}, {"version", 2}, {"level", "kutnohorsko"}, {"objects", nlohmann::json::array()}, {"effects", nlohmann::json::array()}, {"world", nlohmann::json::array()}, {"areas", nlohmann::json::array()}};
        std::string error;
        EQUALS(KCDC::Shared::WorldBuilder::ValidateContent(map, error), true);

        map["doors"]        = nlohmann::json::array({{{"model", "lvl1_door_a_left"}, {"position", {1, 2, 3}}, {"rotation", {0, 0, 0, 1}}, {"locked", true}, {"group", "g"}, {"layer", "l"}}});
        map["smartObjects"] = nlohmann::json::array({{{"blueprint", "Table/table_dice"}, {"position", {4, 5, 6}}, {"rotation", {0, 0, 0, 1}}}});
        map["items"]        = nlohmann::json::array({{{"item", "arrow_crude"}, {"position", {7, 8, 9}}, {"rotation", {0, 0, 0, 1}}, {"amount", 3}, {"name", "loot"}}});
        EQUALS(KCDC::Shared::WorldBuilder::ValidateContent(map, error), true);

        const nlohmann::json resource = KCDC::Shared::WorldBuilder::ExportResource(map);
        EQUALS(resource.at("doors").size(), std::size_t {1});
        EQUALS(resource.at("doors")[0].contains("group"), false);
        EQUALS(resource.at("smartObjects").size(), std::size_t {1});
        EQUALS(resource.at("items").size(), std::size_t {1});

        const std::string calls = KCDC::Shared::WorldBuilder::ApiCalls(resource);
        EQUALS(calls.find("Door.spawn(") != std::string::npos, true);
        EQUALS(calls.find("SmartObject.spawn(") != std::string::npos, true);
        EQUALS(calls.find("GroundItem.spawn(\"arrow_crude\"") != std::string::npos, true);
        EQUALS(calls.find("\"name\":\"loot\"") != std::string::npos, true);

        map["doors"][0]["model"] = "barrel";
        EQUALS(KCDC::Shared::WorldBuilder::ValidateContent(map, error), false);
        EQUALS(error.find("doors[0]") != std::string::npos, true);
    });

    IT("names doors, smart objects and items by library key, and checks their settings as the server reads them", {
        std::string model;
        EQUALS(KCDC::Features::Prop::SplitWorldObjectKey("door:lvl1_door_a_left", model) == KCDC::Features::Prop::PlacedKind::Door, true);
        EQUALS(model, std::string("lvl1_door_a_left"));
        EQUALS(KCDC::Features::Prop::SplitWorldObjectKey("objects/manmade/barrel.cgf", model) == KCDC::Features::Prop::PlacedKind::Prop, true);
        EQUALS(model, std::string("objects/manmade/barrel.cgf"));
        EQUALS(KCDC::Features::Prop::WorldObjectKey(KCDC::Features::Prop::PlacedKind::SmartObject, "Table/table_dice"), std::string("smart:Table/table_dice"));
        EQUALS(KCDC::Features::Prop::SplitWorldObjectKey(KCDC::Features::Prop::WorldObjectKey(KCDC::Features::Prop::PlacedKind::Item, "abc"), model) == KCDC::Features::Prop::PlacedKind::Item, true);
        EQUALS((KCDC::Features::Prop::PlacedKindFromName("smartObject") == KCDC::Features::Prop::PlacedKind::SmartObject), true);
        EQUALS(KCDC::Features::Prop::PlacedKindFromName("bench").has_value(), false);

        // A door by its stem comes back by its catalog path, with every field its kind takes.
        std::string error;
        model                   = "lvl1_door_a_left";
        nlohmann::json settings = nullptr;
        EQUALS(KCDC::Features::Prop::NormalizeWorldObject(KCDC::Features::Prop::PlacedKind::Door, model, settings, error), true);
        EQUALS(model, std::string("objects/characters/assets/doors/lvl1_door_a_left.cdf"));
        EQUALS(settings.value("locked", true), false);
        EQUALS(settings.value("lock", std::string()), std::string("boltAndKey"));
        EQUALS(settings.contains("position"), false);
        EQUALS(settings.contains("model"), false);
        settings             = nlohmann::json::object();
        std::string farModel = "lvl1_door_a_left";
        nlohmann::json distant {{"model", farModel}, {"position", {20000, 0, 0}}};
        KCDC::Shared::Game::DoorDefinition outside;
        EQUALS(KCDC::Shared::Game::DoorDefinitionFromJson(distant, outside, error), false);
        EQUALS(error.find("16384") != std::string::npos, true);
        settings = {{"lock", "portcullis"}};
        EQUALS(KCDC::Features::Prop::NormalizeWorldObject(KCDC::Features::Prop::PlacedKind::Door, model, settings, error), false);
        EQUALS(error.find("lock") != std::string::npos, true);
        model    = "not_a_door";
        settings = nlohmann::json::object();
        EQUALS(KCDC::Features::Prop::NormalizeWorldObject(KCDC::Features::Prop::PlacedKind::Door, model, settings, error), false);

        model    = "table/TABLE_DICE";
        settings = {{"name", "tavern"}};
        EQUALS(KCDC::Features::Prop::NormalizeWorldObject(KCDC::Features::Prop::PlacedKind::SmartObject, model, settings, error), true);
        EQUALS(model, std::string("Table/table_dice"));
        EQUALS(settings, (nlohmann::json {{"name", "tavern"}}));
        settings = {{"name", std::string(65, 'x')}};
        EQUALS(KCDC::Features::Prop::NormalizeWorldObject(KCDC::Features::Prop::PlacedKind::SmartObject, model, settings, error), false);

        // A shipped item by name is kept by its GUID; an unknown name is the server's to resolve.
        const KCDC::Shared::Game::ItemClassEntry &shipped = stackable();
        model                                             = std::string(shipped.name);
        settings                                          = {{"amount", 3}};
        EQUALS(KCDC::Features::Prop::NormalizeWorldObject(KCDC::Features::Prop::PlacedKind::Item, model, settings, error), true);
        EQUALS(model, std::string(shipped.guid));
        EQUALS(settings.value("amount", 0), 3);
        EQUALS(KCDC::Features::Prop::WorldObjectName(KCDC::Features::Prop::PlacedKind::Item, model), std::string(shipped.name));
        // An apple does not stack, so a stack of three is no stack the server lays down.
        model    = std::string(KCDC::Shared::Game::FindItemClassByName("apple")->guid);
        settings = {{"amount", 3}};
        EQUALS(KCDC::Features::Prop::NormalizeWorldObject(KCDC::Features::Prop::PlacedKind::Item, model, settings, error), false);
        EQUALS(error.find("does not stack") != std::string::npos, true);
        model    = "my_custom_relic";
        settings = nlohmann::json::object();
        EQUALS(KCDC::Features::Prop::NormalizeWorldObject(KCDC::Features::Prop::PlacedKind::Item, model, settings, error), true);
        EQUALS(model, std::string("my_custom_relic"));
        settings = {{"amount", 0}};
        EQUALS(KCDC::Features::Prop::NormalizeWorldObject(KCDC::Features::Prop::PlacedKind::Item, model, settings, error), false);
        EQUALS(KCDC::Features::Prop::NormalizeWorldObject(KCDC::Features::Prop::PlacedKind::Prop, model, settings, error), false);
    });

    IT("lists every door leaf, blueprint and item after the meshes, which is where a library row past the last mesh points", {
        const std::vector<KCDC::Features::Prop::WorldObjectRow> &rows = KCDC::Features::Prop::WorldObjectRows();
        EQUALS(rows.size() > KCDC::Shared::Game::g_DoorModels.size() + KCDC::Shared::Game::g_SmartObjectBlueprints.size(), true);
        EQUALS(rows.front().kind == KCDC::Features::Prop::PlacedKind::Door, true);
        EQUALS(rows.front().model, std::string(KCDC::Shared::Game::g_DoorModels.front().path));
        EQUALS(rows.front().directories.front(), std::string("doors"));
        const KCDC::Features::Prop::WorldObjectRow &bench = rows[KCDC::Shared::Game::g_DoorModels.size()];
        EQUALS(bench.kind == KCDC::Features::Prop::PlacedKind::SmartObject, true);
        EQUALS(bench.directories.front(), std::string("interactive"));
        EQUALS(rows.back().kind == KCDC::Features::Prop::PlacedKind::Item, true);
        for (const KCDC::Features::Prop::WorldObjectRow &row : rows) {
            std::string model       = row.model;
            nlohmann::json settings = nlohmann::json::object();
            std::string error;
            EQUALS(KCDC::Features::Prop::NormalizeWorldObject(row.kind, model, settings, error), true);
            EQUALS(model, row.model);
        }
        const int first = static_cast<int>(KCDC::Shared::Game::g_PropModels.size());
        EQUALS(KCDC::Features::Prop::WorldObjectAtLibraryRow(first - 1) == nullptr, true);
        EQUALS(KCDC::Features::Prop::WorldObjectAtLibraryRow(first) == &rows.front(), true);
        EQUALS(KCDC::Features::Prop::WorldObjectAtLibraryRow(first + static_cast<int>(rows.size())) == nullptr, true);
    });

    IT("saves placed doors, smart objects and items in their own arrays and layers, and loads them back", {
        KCDC::Features::Prop::MapDocument document;
        document.level = "kutnohorsko";
        std::string error;
        const std::string layer = document.layers.Create("Interiors", error);

        KCDC::Features::Prop::DocumentObject prop;
        prop.model    = "objects/manmade/barrel.cgf";
        prop.position = {1, 1, 1};
        document.objects.push_back(prop);

        KCDC::Features::Prop::DocumentObject door;
        door.kind     = KCDC::Features::Prop::PlacedKind::Door;
        door.model    = "lvl1_door_a_right";
        door.settings = {{"locked", true}, {"lock", "key"}, {"keyItem", "d14a857c-e2fb-4a8a-a54b-79dde42d06cd"}};
        EQUALS(KCDC::Features::Prop::NormalizeWorldObject(door.kind, door.model, door.settings, error), true);
        door.position = {2, 3, 4};
        door.rotation = {0.0F, 0.0F, 0.7071068F, 0.7071068F};
        door.group    = "jail";
        door.layer    = layer;

        KCDC::Features::Prop::DocumentObject bench;
        bench.kind     = KCDC::Features::Prop::PlacedKind::SmartObject;
        bench.model    = "Table/table_dice";
        bench.settings = {{"name", "tavern dice"}};
        EQUALS(KCDC::Features::Prop::NormalizeWorldObject(bench.kind, bench.model, bench.settings, error), true);
        bench.position = {5, 6, 7};

        KCDC::Features::Prop::DocumentObject item;
        item.kind     = KCDC::Features::Prop::PlacedKind::Item;
        item.model    = std::string(stackable().guid);
        item.settings = {{"amount", 5}, {"name", "cache"}};
        EQUALS(KCDC::Features::Prop::NormalizeWorldObject(item.kind, item.model, item.settings, error), true);
        item.position = {8, 9, 10};
        item.layer    = layer;

        // Interleaved, as a scene holds them; saved and loaded kind by kind.
        document.objects.push_back(item);
        document.objects.push_back(door);
        document.objects.push_back(bench);

        const nlohmann::json scene = KCDC::Features::Prop::MapDocumentToJson(document);
        EQUALS(scene.at("objects").size(), std::size_t {1});
        EQUALS(scene.at("doors").size(), std::size_t {1});
        EQUALS(scene.at("doors")[0].value("model", std::string()), std::string("objects/characters/assets/doors/lvl1_door_a_right.cdf"));
        EQUALS(scene.at("doors")[0].value("group", std::string()), std::string("jail"));
        EQUALS(scene.at("smartObjects")[0].value("blueprint", std::string()), std::string("Table/table_dice"));
        EQUALS(scene.at("items")[0].value("amount", 0), 5);
        EQUALS(KCDC::Shared::WorldBuilder::ValidateContent(scene, error), true);

        const nlohmann::json project = KCDC::Features::Prop::ProjectToJson(document);
        EQUALS(project.at("editor").at("layers").at("doors"), (nlohmann::json::array({layer})));
        EQUALS(project.at("editor").at("layers").at("smartObjects"), (nlohmann::json::array({"default"})));
        EQUALS(project.at("editor").at("layers").at("items"), (nlohmann::json::array({layer})));

        KCDC::Features::Prop::MapDocument restored;
        EQUALS(KCDC::Features::Prop::ParseProject(project.dump(), restored, error), true);
        EQUALS(restored.objects.size(), std::size_t {4});
        EQUALS(restored.objects[0].kind == KCDC::Features::Prop::PlacedKind::Prop, true);
        EQUALS(restored.objects[1].kind == KCDC::Features::Prop::PlacedKind::Door, true);
        EQUALS(restored.objects[1].layer, layer);
        EQUALS(restored.objects[1].group, std::string("jail"));
        EQUALS(restored.objects[1].settings, door.settings);
        EQUALS(restored.objects[1].rotation.z, door.rotation.z);
        EQUALS(restored.objects[2].kind == KCDC::Features::Prop::PlacedKind::SmartObject, true);
        EQUALS(restored.objects[2].layer, std::string("default"));
        EQUALS(restored.objects[3].kind == KCDC::Features::Prop::PlacedKind::Item, true);
        EQUALS(restored.objects[3].layer, layer);
        EQUALS(restored.objects[3].settings, item.settings);
        EQUALS(KCDC::Features::Prop::ProjectToJson(restored) == KCDC::Features::Prop::ProjectToJson(KCDC::Features::Prop::MapDocument(restored)), true);
        EQUALS(KCDC::Features::Prop::MapDocumentToJson(restored) == scene, true);

        // A layer list that does not match the array it describes fails the project, as for meshes.
        nlohmann::json broken               = project;
        broken["editor"]["layers"]["doors"] = nlohmann::json::array();
        EQUALS(KCDC::Features::Prop::ParseProject(broken.dump(), restored, error), false);

        // A project from before them has no arrays and no layer lists for them.
        KCDC::Features::Prop::MapDocument plain;
        plain.level = "kutnohorsko";
        plain.objects.push_back(prop);
        const nlohmann::json older = KCDC::Features::Prop::ProjectToJson(plain);
        EQUALS(older.at("scene").contains("doors"), false);
        EQUALS(older.at("editor").at("layers").contains("doors"), false);
        EQUALS(KCDC::Features::Prop::ParseProject(older.dump(), restored, error), true);

        // The export the server loads keeps them, without the editor's groups.
        const nlohmann::json resource = KCDC::Shared::WorldBuilder::ExportResource(scene);
        EQUALS(resource.at("doors")[0].contains("group"), false);
        nlohmann::json loaded;
        EQUALS(KCDC::Shared::WorldBuilder::ReadResource(resource.dump(), loaded, error), true);
    });
});
