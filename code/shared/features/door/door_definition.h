#pragma once

#include "shared/features/level_edit/level_edit_definition.h"
#include "shared/game/door_catalog.h"
#include "shared/pose_math.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace KCDC::Shared::Game {
    /**
     * A door hung where the level has none, as `Door.spawn` takes it, a World
     * Builder map's `doors` array holds it and `door.toJSON()` writes it.
     * Kept here so the editor's files, the server's scripts and a world
     * resource all read one format.
     */
    struct DoorDefinition {
        /** A leaf from `g_DoorModels`, by path or file stem. */
        std::string model;

        glm::vec3 position {0.0F};
        glm::quat rotation {1.0F, 0.0F, 0.0F, 0.0F};

        /** What a script finds it by; empty for none. */
        std::string name;

        bool locked = false;

        /** `esLockTypes`: what each side takes to get through it while it is locked. */
        DoorLockType lock = DoorLockType::ManualKey;

        bool lockpickable = true;

        /** 0 to 1, or negative for AnimDoor.lua's own. */
        float lockDifficulty = -1.0F;

        /** The item class that works the keyhole, as GUID text; empty for none. */
        std::string keyItem;

        /** A name past this is no name; the same bound an area's id has. */
        static constexpr std::size_t kMaxNameLength = 64;
    };

    /** How a script spells a lock type. */
    inline const char *DoorLockTypeName(DoorLockType lock) {
        switch (lock) {
        case DoorLockType::None: return "none";
        case DoorLockType::Manual: return "bolt";
        case DoorLockType::ManualKey: return "boltAndKey";
        case DoorLockType::Key: return "key";
        }
        return "boltAndKey";
    }

    inline bool ParseDoorLockType(std::string_view name, DoorLockType &lock) {
        for (DoorLockType candidate : {DoorLockType::None, DoorLockType::Manual, DoorLockType::ManualKey, DoorLockType::Key}) {
            if (name == DoorLockTypeName(candidate)) {
                lock = candidate;
                return true;
            }
        }
        return false;
    }

    /** The row a model names, by its path or its file stem, or `kNoDoorModel`. */
    inline std::uint16_t ResolveDoorModel(std::string_view model) {
        if (model.empty()) {
            return kNoDoorModel;
        }
        const std::string path(model);
        if (const std::uint16_t index = FindDoorModelIndex(path.c_str()); index != kNoDoorModel) {
            return index;
        }
        for (std::size_t index = 0; index < g_DoorModels.size(); ++index) {
            std::string_view stem(g_DoorModels[index].path);
            stem = stem.substr(stem.find_last_of('/') + 1);
            stem = stem.substr(0, stem.find_last_of('.'));
            if (stem == model) {
                return static_cast<std::uint16_t>(index);
            }
        }
        return kNoDoorModel;
    }

    /** The entry as a map's `doors` array stores it: arrays for vectors, `[x, y, z, w]` for the rotation. */
    inline nlohmann::json DoorDefinitionToJson(const DoorDefinition &door) {
        nlohmann::json entry {
            {"model", door.model},
            {"position", nlohmann::json::array({door.position.x, door.position.y, door.position.z})},
            {"rotation", nlohmann::json::array({door.rotation.x, door.rotation.y, door.rotation.z, door.rotation.w})},
            {"locked", door.locked},
            {"lock", DoorLockTypeName(door.lock)},
            {"lockpickable", door.lockpickable},
        };
        if (!door.name.empty()) {
            entry["name"] = door.name;
        }
        if (door.lockDifficulty >= 0.0F) {
            entry["lockDifficulty"] = door.lockDifficulty;
        }
        if (!door.keyItem.empty()) {
            entry["keyItem"] = door.keyItem;
        }
        return entry;
    }

    /**
     * Reads one entry. `model` and `position` are required; the rest default
     * to a closed, unlocked door with a bolt on one side and a keyhole on the
     * other, as most of the level's own doors are. False, with `error` saying
     * why, for an entry no client could hang.
     */
    inline bool DoorDefinitionFromJson(const nlohmann::json &entry, DoorDefinition &door, std::string &error) {
        door = DoorDefinition {};
        if (!entry.is_object()) {
            error = "a door must be an object";
            return false;
        }

        door.model = LevelEditDefinitionJson::ReadString(entry, "model");
        if (ResolveDoorModel(door.model) == kNoDoorModel) {
            error = door.model.empty() ? "a door needs a model" : "'" + door.model + "' is not a door model; Door.models() lists them";
            return false;
        }
        if (!LevelEditDefinitionJson::ReadVector(entry, "position", door.position) || !IsWorldPosition(door.position)) {
            error = "position must be where the door hangs, within 16384 m of the origin, as [x, y, z] or a Vector3";
            return false;
        }
        if (entry.contains("rotation") && !LevelEditDefinitionJson::ReadRotation(entry, "rotation", door.rotation)) {
            error = "rotation must be a quaternion, as [x, y, z, w] or a Quaternion";
            return false;
        }

        door.name = LevelEditDefinitionJson::ReadString(entry, "name");
        if (door.name.size() > DoorDefinition::kMaxNameLength) {
            error = "name is longer than 64 characters";
            return false;
        }

        door.locked = LevelEditDefinitionJson::ReadBool(entry, "locked");
        if (const std::string lock = LevelEditDefinitionJson::ReadString(entry, "lock"); !lock.empty() && !ParseDoorLockType(lock, door.lock)) {
            error = "lock must be 'none', 'bolt', 'boltAndKey' or 'key', not '" + lock + "'";
            return false;
        }
        if (const auto lockpickable = entry.find("lockpickable"); lockpickable != entry.end()) {
            if (!lockpickable->is_boolean()) {
                error = "lockpickable must be true or false";
                return false;
            }
            door.lockpickable = lockpickable->get<bool>();
        }
        if (const auto difficulty = entry.find("lockDifficulty"); difficulty != entry.end() && !difficulty->is_null()) {
            if (!difficulty->is_number() || !std::isfinite(difficulty->get<float>()) || difficulty->get<float>() < 0.0F || difficulty->get<float>() > 1.0F) {
                error = "lockDifficulty must be a number from 0 to 1";
                return false;
            }
            door.lockDifficulty = difficulty->get<float>();
        }
        door.keyItem = LevelEditDefinitionJson::ReadString(entry, "keyItem");
        return true;
    }
} // namespace KCDC::Shared::Game
