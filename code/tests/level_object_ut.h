#pragma once

#include "shared/features/level_edit/level_edit_definition.h"
#include "shared/features/level_edit/level_edit_entity.h"
#include "shared/features/world/world_query_rpc.h"

#include <mafianet/bit_stream.h>
#include <networking/replication/network_entity.h>

#include <nlohmann/json.hpp>

#include <cmath>
#include <limits>
#include <string>

MODULE(level_objects, {
    IT("writes a swap into a map entry and reads it back", {
        KCDC::Shared::Game::LevelEditDefinition tree;
        tree.name  = "objects/natural/vegetation/trees/normal_trees/quercus_robur/quercus_robur_big_a.cgf";
        tree.at    = {10.0F, 20.0F, 30.0F};
        tree.model = "objects/natural/vegetation/trees/stumps/stump_fagus_a.cgf";
        EQUALS(tree.IsRemoved(), true);

        const nlohmann::json entry = KCDC::Shared::Game::LevelEditDefinitionToJson(tree);
        EQUALS(entry.at("model").get<std::string>(), tree.model);
        EQUALS(entry.at("hidden").get<bool>(), false);

        KCDC::Shared::Game::LevelEditDefinition read;
        std::string error;
        EQUALS(KCDC::Shared::Game::LevelEditDefinitionFromJson(entry, read, error), true);
        EQUALS(read.model, tree.model);
        EQUALS(read.hidden, false);
        EQUALS(read.IsRemoved(), true);
        EQUALS(read.moved, false);
    });

    IT("leaves the swap out of an entry that has none", {
        KCDC::Shared::Game::LevelEditDefinition wall;
        wall.name                  = "objects/manmade/walls/wall_a.cgf";
        wall.at                    = {1.0F, 2.0F, 3.0F};
        wall.hidden                = true;
        const nlohmann::json entry = KCDC::Shared::Game::LevelEditDefinitionToJson(wall);
        EQUALS(entry.contains("model"), false);

        KCDC::Shared::Game::LevelEditDefinition read;
        std::string error;
        EQUALS(KCDC::Shared::Game::LevelEditDefinitionFromJson(entry, read, error), true);
        EQUALS(read.model.empty(), true);
        EQUALS(read.IsRemoved(), true);

        wall.hidden = false;
        EQUALS(wall.IsRemoved(), false);
    });

    IT("reads a trace's level object spread into a definition", {
        // What `LevelEdit.apply({ ...hit.levelObject, model })` hands the reader:
        // the object's own rotation and scale ride along, but without a
        // position they move nothing.
        const nlohmann::json entry = {{"kind", "brush"}, {"name", "objects/natural/rocks/rock_a.cgf"}, {"at", {{"x", 1.0}, {"y", 2.0}, {"z", 3.0}}}, {"rotation", {{"x", 0.0}, {"y", 0.0}, {"z", 0.7071}, {"w", 0.7071}}}, {"scale", {{"x", 2.0}, {"y", 2.0}, {"z", 2.0}}},
            {"model", "objects/natural/rocks/rock_b.cgf"}};
        KCDC::Shared::Game::LevelEditDefinition read;
        std::string error;
        EQUALS(KCDC::Shared::Game::LevelEditDefinitionFromJson(entry, read, error), true);
        EQUALS(read.moved, false);
        EQUALS(read.model, std::string("objects/natural/rocks/rock_b.cgf"));
        EQUALS(read.at.x, 1.0F);

        const nlohmann::json entity = {{"kind", "entity"}, {"name", "AnimDoor1"}, {"guid", "00000000000000ab"}, {"class", "AnimDoor"}, {"at", {1, 2, 3}}, {"hidden", true}};
        EQUALS(KCDC::Shared::Game::LevelEditDefinitionFromJson(entity, read, error), true);
        EQUALS(read.kind, KCDC::Shared::Game::LevelEditKind::Entity);
        EQUALS(read.guid, std::uint64_t {0xab});
        EQUALS(read.className, std::string("AnimDoor"));
    });

    IT("replicates a swap with the edit, and a swap taken back", {
        KCDC::Shared::Entities::LevelEditEntity sent;
        sent.name  = "objects/natural/vegetation/trees/normal_trees/quercus_robur/quercus_robur_big_a.cgf";
        sent.at    = {10.0F, 20.0F, 30.0F};
        sent.model = "objects/natural/vegetation/trees/stumps/stump_fagus_a.cgf";

        MafiaNet::BitStream stream;
        Framework::Networking::Replication::FieldSerializer writer(&stream, true);
        sent.OnSerializeConstruction(writer);

        KCDC::Shared::Entities::LevelEditEntity received;
        Framework::Networking::Replication::FieldSerializer reader(&stream, false);
        received.OnSerializeConstruction(reader);
        EQUALS(reader.Good(), true);
        EQUALS(received.model, sent.model);
        EQUALS(received.name, sent.name);
        EQUALS(received.Describe().IsRemoved(), true);

        sent.model.clear();
        MafiaNet::BitStream update;
        Framework::Networking::Replication::FieldSerializer updateWriter(&update, true);
        sent.SerializeFields(updateWriter);
        Framework::Networking::Replication::FieldSerializer updateReader(&update, false);
        received.SerializeFields(updateReader);
        EQUALS(updateReader.Good(), true);
        EQUALS(received.model.empty(), true);
        EQUALS(received.Describe().IsRemoved(), false);
    });

    IT("carries a trace hit's level object only when the level placed what it hit", {
        KCDC::Shared::RPC::WorldRayHit tree;
        tree.kind           = KCDC::Shared::RPC::WorldHitKind::Vegetation;
        tree.model          = "objects/natural/vegetation/trees/normal_trees/quercus_robur/quercus_robur_big_a.cgf";
        tree.pointX         = 10.4F;
        tree.pointY         = 20.1F;
        tree.pointZ         = 31.5F;
        tree.distance       = 3.0F;
        tree.levelObject    = true;
        tree.objectAt       = {10.0F, 20.0F, 30.0F};
        tree.objectRotation = {0.0F, 0.0F, 0.38268343F, 0.92387953F};
        tree.objectScale    = {1.1F, 1.1F, 1.1F};
        EQUALS(tree.IsSane(), true);

        KCDC::Shared::RPC::WorldRayHit ground;
        ground.kind     = KCDC::Shared::RPC::WorldHitKind::Terrain;
        ground.terrain  = true;
        ground.distance = 5.0F;

        MafiaNet::BitStream stream;
        EQUALS(tree.Serialize(&stream, true), true);
        EQUALS(ground.Serialize(&stream, true), true);

        KCDC::Shared::RPC::WorldRayHit readTree;
        KCDC::Shared::RPC::WorldRayHit readGround;
        EQUALS(readTree.Serialize(&stream, false), true);
        EQUALS(readGround.Serialize(&stream, false), true);
        EQUALS(readTree.IsSane(), true);
        EQUALS(readTree.levelObject, true);
        EQUALS(readTree.objectAt[2], 30.0F);
        EQUALS(readTree.objectRotation[3], tree.objectRotation[3]);
        EQUALS(readTree.objectScale[0], 1.1F);
        EQUALS(readTree.model, tree.model);
        EQUALS(readGround.levelObject, false);
        EQUALS(readGround.terrain, true);
    });

    IT("refuses a level object no client could key", {
        KCDC::Shared::RPC::WorldRayHit hit;
        hit.kind        = KCDC::Shared::RPC::WorldHitKind::Brush;
        hit.model       = "objects/manmade/walls/wall_a.cgf";
        hit.levelObject = true;
        hit.objectAt    = {std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F};
        EQUALS(hit.IsSane(), false);

        hit.objectAt       = {0.0F, 0.0F, 0.0F};
        hit.objectRotation = {0.0F, 0.0F, 0.0F, 0.0F};
        EQUALS(hit.IsSane(), false);

        hit.objectRotation = {0.0F, 0.0F, 0.0F, 1.0F};
        hit.objectScale    = {1.0F, std::numeric_limits<float>::infinity(), 1.0F};
        EQUALS(hit.IsSane(), false);

        // Not a level object: the pose is never read, nor sent.
        hit.levelObject = false;
        EQUALS(hit.IsSane(), true);
    });
});
