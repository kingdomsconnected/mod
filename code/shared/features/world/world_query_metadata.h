#pragma once

#include "shared/features/world/world_query_rpc.h"
#include "shared/scripting/level_guid.h"

#include <scripting/v8pp_types.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <v8pp/convert.hpp>
#include <v8pp/metadata.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>

namespace KCDC::Scripting {
    /**
     * The result shapes both `World` bindings answer with, declared once so
     * the two contracts cannot drift. The engine's entity id is the only
     * difference: it is a handle on the machine that answered, so only the
     * client's contract carries it.
     */
    inline void RegisterWorldQueryMetadata(v8pp::metadata::registry &catalog, bool includeEntityId) {
        auto &hit = catalog.data_type("WorldRayHit", "One thing a trace met, as the machine that traced it saw it.");
        hit.add_property("position", "Vector3", "Where the ray met it, in world-space metres.");
        hit.add_property("normal", "Vector3", "The surface normal at that point, as a unit vector.");
        hit.add_property("distance", "number", "How far along the ray it sits, in metres.");
        hit.add_property("surface", "string",
            "The surface type's own name, spelled the way the game's tables spell it in `Libs/MaterialEffects/SurfaceTypes.xml` -- `mat_wood`, `mat_rock`, `mat_soil`, `mat_water`. It says what the thing is made of, which is how footsteps and blows sound, not what it is: a plank wall is `mat_wood` as much as a trunk is, so tell a tree by `category`. Among the 74 the game defines: `mat_wood`, `mat_wood_soft`, `mat_vegetation`, `mat_bushes`, `mat_rock`, `mat_rock_unwalk`, `mat_rock_horse_ignore`, `mat_stairs_stone`, `mat_gravel`, `mat_soil`, `mat_mud`, `mat_grass`, `mat_road`, `mat_metal`, `mat_glass`, `mat_plaster`, `mat_thatch`, `mat_fabric`, `mat_flesh`. Empty only before the material tables are up.");
        hit.add_property("terrain", "boolean", "Whether the ground itself was hit rather than anything placed on it. Nothing lies behind the terrain, so a trace stops there. The same as `kind` being `terrain`.");
        hit.add_property("kind", "\"terrain\" | \"entity\" | \"vegetation\" | \"brush\" | \"static\"",
            "What sort of thing the ray stopped on. `entity` is anything with an `entityClass` -- doors, props, NPCs, players. `vegetation` is an instance the level paints on: trees, bushes, plants, and boulders painted the same way. `brush` is a static mesh placed one by one: walls, houses, rocks and cliffs. `static` is static geometry that is neither, or whose source the engine does not record. Trees and rocks are not entities, so for them this, `model` and `category` are what say what was hit.");
        hit.add_property("model", "string | null",
            "The mesh of a vegetation instance or a brush, as the path the game loads it from -- `objects/natural/vegetation/trees/normal_trees/quercus_robur/quercus_robur_big_a.cgf`. Null for terrain, entities and `static`. A tree or a rock has no GUID, but its `model` and `position` together are the same on every client on the same level, which is what a server keeps a felled tree or a mined rock by.");
        hit.add_property("category", "\"tree\" | \"bush\" | \"plant\" | \"rock\" | null",
            "Which of the game's own folders of natural objects `model` comes from: `tree` for `objects/natural/vegetation/trees` -- living, dead, fallen and stumps alike, which `model` tells apart -- `bush` for its bushes, `plant` for the rest of its vegetation (grass, ferns, mushrooms, branches), and `rock` for `objects/natural/rocks` and `objects/natural/stones`. Null for anything else, man-made included.");
        hit.add_property("material", "string | null",
            "The material drawn where the ray met it, as the path the material manager keys it by, the sub-material when the mesh has several -- a trunk's bark rather than the tree's whole material. Null for terrain and when nothing names one.");
        hit.add_property("entityGuid", "string | null",
            "The level's own identity for what was hit, as sixteen lowercase hex digits -- the same on every machine, and what `Door.find` and the other GUID lookups take. Null for terrain, for static geometry and for anything the session spawned, none of which the level names.");
        hit.add_property("entityName", "string | null", "The name the level gives it, often empty. Null when nothing that carries a name was hit.");
        hit.add_property("entityClass", "string | null", "The engine class it belongs to -- `AnimDoor`, `NPC_NAI`, `GeomEntity`. Null when nothing that carries a class was hit.");
        if (includeEntityId) {
            hit.add_property("entityId", "number | null", "This machine's own handle for it. Local to this client and to this session -- it is not the network ID and it is not portable, so store `entityGuid` instead. Null for terrain and static geometry.");
        }
        hit.add_property("levelObject", "WorldLevelObject | null",
            "The object the level placed that was hit, named the way `LevelEdit.apply` finds it again on every machine, with where the level put it and how it turned and sized it. A tree, a rock or a wall is one; so is a door or any other entity the level gives a GUID. Null for terrain, for static geometry the engine records no source for, and for anything the session spawned -- props, players, NPCs. Spread it into a definition to take the object out or swap it: `LevelEdit.apply({ ...hit.levelObject, model: stump })`.");

        auto &levelObject = catalog.data_type("WorldLevelObject", "An object the level placed, as a level edit keys it, and the pose the level gave it. Every field a `LevelEditDefinition` needs to find it again.");
        levelObject.add_property("kind", "\"brush\" | \"entity\"", "A `brush` is a static mesh or a vegetation instance -- a wall, a rock, a tree -- found by its mesh and where the level put it. An `entity` is found by its GUID.");
        levelObject.add_property("name", "string", "A brush's mesh path, the same as the hit's `model`; an entity's name.");
        levelObject.add_property("guid", "string | undefined", "An entity's level GUID as sixteen lowercase hex digits; absent for a brush.");
        levelObject.add_property("class", "string | undefined", "An entity's engine class; absent for a brush.");
        levelObject.add_property("at", "Vector3", "Where the level put it: its pivot, not the point the ray met. Unchanged while an edit has moved it, since it is the key.");
        levelObject.add_property("rotation", "Quaternion", "How the level turned it, for a prop or effect standing in its place. `LevelEdit.apply` reads a rotation only with a `position`, so spreading this into a definition moves nothing.");
        levelObject.add_property("scale", "Vector3", "How the level sized it, per axis. Read only with a `position`, as `rotation` is.");

        auto &nearby = catalog.data_type("WorldNearbyEntity", "One entity a sphere reported, and where it stood when it did.");
        nearby.add_property("position", "Vector3", "Where it is, in world-space metres.");
        nearby.add_property("distance", "number", "How far it is from the centre of the sphere, in metres.");
        nearby.add_property("guid", "string | null", "The level's own identity for it, as sixteen lowercase hex digits. Null for anything the session spawned, which the level does not name.");
        nearby.add_property("name", "string", "The name the level gives it, often empty.");
        nearby.add_property("class", "string", "The engine class it belongs to -- `AnimDoor`, `NPC_NAI`, `GeomEntity`.");
        nearby.add_property("physicalized", "boolean", "Whether the engine has built physics for it, which is what makes it something a trace could also find.");
    }

    /**
     * A hit's `levelObject`, or null when the level did not place what was
     * hit. Built here so both bindings hand a script the same definition
     * `LevelEdit.apply` takes.
     */
    inline v8::Local<v8::Value> WorldLevelObjectToValue(v8::Isolate *isolate, bool levelObject, KCDC::Shared::RPC::WorldHitKind kind, const std::string &model, const std::string &entityName, const std::string &entityClass, std::uint64_t entityGuid, const glm::vec3 &at,
        const glm::quat &rotation, const glm::vec3 &scale) {
        if (!levelObject) {
            return v8::Null(isolate);
        }
        v8::Local<v8::Context> context = isolate->GetCurrentContext();
        v8::Local<v8::Object> result   = v8::Object::New(isolate);
        const bool entity              = kind == KCDC::Shared::RPC::WorldHitKind::Entity;
        result->Set(context, v8pp::to_v8(isolate, "kind"), v8pp::to_v8(isolate, entity ? "entity" : "brush")).Check();
        result->Set(context, v8pp::to_v8(isolate, "name"), v8pp::to_v8(isolate, entity ? entityName : model)).Check();
        if (entity) {
            result->Set(context, v8pp::to_v8(isolate, "guid"), v8pp::to_v8(isolate, FormatLevelGuid(entityGuid))).Check();
            result->Set(context, v8pp::to_v8(isolate, "class"), v8pp::to_v8(isolate, entityClass)).Check();
        }
        result->Set(context, v8pp::to_v8(isolate, "at"), v8pp::convert<glm::vec3>::to_v8(isolate, at)).Check();
        result->Set(context, v8pp::to_v8(isolate, "rotation"), v8pp::convert<glm::quat>::to_v8(isolate, rotation)).Check();
        result->Set(context, v8pp::to_v8(isolate, "scale"), v8pp::convert<glm::vec3>::to_v8(isolate, scale)).Check();
        return result;
    }

    /** How a script spells a hit's kind. */
    inline const char *WorldHitKindName(KCDC::Shared::RPC::WorldHitKind kind) {
        switch (kind) {
        case KCDC::Shared::RPC::WorldHitKind::Terrain: return "terrain";
        case KCDC::Shared::RPC::WorldHitKind::Entity: return "entity";
        case KCDC::Shared::RPC::WorldHitKind::Vegetation: return "vegetation";
        case KCDC::Shared::RPC::WorldHitKind::Brush: return "brush";
        case KCDC::Shared::RPC::WorldHitKind::Static: break;
        }
        return "static";
    }

    /**
     * Which of the game's folders of natural objects a mesh comes from, or
     * null. The game files every tree under `objects/natural/vegetation/trees`
     * and every rock under `objects/natural/rocks` or `objects/natural/stones`,
     * so the folder is the classification the game itself made.
     */
    inline const char *WorldHitCategory(const std::string &model) {
        std::string path(model.size(), '\0');
        std::transform(model.begin(), model.end(), path.begin(), [](unsigned char character) {
            return character == '\\' ? '/' : static_cast<char>(std::tolower(character));
        });

        const std::string_view view(path);
        if (view.starts_with("objects/natural/vegetation/trees/")) {
            return "tree";
        }
        if (view.starts_with("objects/natural/vegetation/bushes/")) {
            return "bush";
        }
        if (view.starts_with("objects/natural/vegetation/")) {
            return "plant";
        }
        if (view.starts_with("objects/natural/rocks/") || view.starts_with("objects/natural/stones/")) {
            return "rock";
        }
        return nullptr;
    }

    /** The mode name one of the three traces goes by, or false. */
    inline bool ParseWorldRayMode(const std::string &name, KCDC::Shared::RPC::WorldRayMode &mode) {
        if (name == "cover") {
            mode = KCDC::Shared::RPC::WorldRayMode::Cover;
            return true;
        }
        if (name == "anything") {
            mode = KCDC::Shared::RPC::WorldRayMode::Anything;
            return true;
        }
        if (name == "ground") {
            mode = KCDC::Shared::RPC::WorldRayMode::Ground;
            return true;
        }
        return false;
    }
} // namespace KCDC::Scripting
