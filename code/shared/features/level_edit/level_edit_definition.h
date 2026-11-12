#pragma once

#include "shared/features/grass/grass_clearing.h"
#include "shared/scripting/level_guid.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>

namespace KCDC::Shared::Game {
    /**
     * How far apart two placements of one mesh may be and still be the same
     * brush. A brush's only key is its mesh and where the level put it, and
     * the editor, the server and the layer physics all match it to this.
     */
    inline constexpr float kLevelEditKeySlack = 0.05F;

    /** What a level edit is keyed by: the level gives an entity an identity, a brush none, and the grass is not objects at all. */
    enum class LevelEditKind : std::uint8_t {
        Brush,  // keyed by its mesh path and where the level put it
        Entity, // keyed by its level EntityGuid
        Grass,  // a box of the terrain's merged vegetation, keyed by the box itself
    };

    /** How a kind is spelled in a map's `world` array and by `levelEdit.kind`. */
    inline const char *LevelEditKindToString(LevelEditKind kind) {
        switch (kind) {
        case LevelEditKind::Entity: return "entity";
        case LevelEditKind::Grass: return "grass";
        case LevelEditKind::Brush: break;
        }
        return "brush";
    }

    /**
     * One change to an object the level itself placed -- taken out, moved,
     * swapped for another mesh, or a mix of those -- as one entry of a World
     * Builder map's `world` array holds it, `LevelEdit.apply` takes it and
     * `levelEdit.toJSON()` writes it. Kept here so the editor's files, the
     * server's scripts and the wire all read one format.
     */
    struct LevelEditDefinition {
        LevelEditKind kind = LevelEditKind::Brush;

        std::uint64_t guid = 0; // Entity only
        std::string name;       // the brush's mesh path, or the entity's name
        std::string className;  // Entity only, informational

        /** Where the level put it: half of a brush's key. For grass, the centre of the box. */
        glm::vec3 at {0.0F};

        /**
         * Grass only: half the box along its own two ground axes and up, and
         * its turn about the vertical in degrees. With `at` they are the
         * whole edit, key and all -- see `GrassClearing`. `hidden` takes the
         * plants in the box out; nothing else applies to grass.
         */
        glm::vec3 halfSize {0.0F};
        float yawDegrees = 0.0F;

        /** The box a grass edit clears. */
        GrassClearing Clearing() const {
            return GrassClearing {at, halfSize, yawDegrees};
        }

        bool hidden = false;

        /**
         * A mesh that stands in the object's place, or empty: a prop catalog
         * path or a streamed one. The object goes out of the world, collision
         * included, and this mesh stands where it stood -- or where a move
         * puts it -- as solid static scenery. A felled tree leaves its stump.
         */
        std::string model;

        /** Whether the object is out of the world: taken out, or swapped for `model`. */
        bool IsRemoved() const {
            return hidden || !model.empty();
        }

        /** Whether `position`, `rotation` and `scale` say where it stands instead. */
        bool moved = false;
        glm::vec3 position {0.0F};
        glm::quat rotation {1.0F, 0.0F, 0.0F, 0.0F};
        glm::vec3 scale {1.0F};

        /** Whether another run or another machine can find the object again from this. */
        bool HasKey() const {
            if (kind == LevelEditKind::Grass) {
                return Clearing().IsSane();
            }
            return kind == LevelEditKind::Entity ? guid != 0 : !name.empty();
        }
    };

    namespace LevelEditDefinitionJson {
        /** Rejects non-finite numbers, so a hand-edited file cannot put NaN into a transform. */
        inline bool ReadFloats(const nlohmann::json &node, std::size_t count, float *out) {
            if (node.is_array()) {
                if (node.size() != count) {
                    return false;
                }
                for (std::size_t index = 0; index < count; ++index) {
                    if (!node[index].is_number() || !std::isfinite(node[index].get<float>())) {
                        return false;
                    }
                    out[index] = node[index].get<float>();
                }
                return true;
            }

            // A script's own Vector3 or Quaternion reads too, not only the map's arrays.
            if (node.is_object()) {
                static constexpr const char *kKeys[] = {"x", "y", "z", "w"};
                for (std::size_t index = 0; index < count; ++index) {
                    const auto found = node.find(kKeys[index]);
                    if (found == node.end() || !found->is_number() || !std::isfinite(found->get<float>())) {
                        return false;
                    }
                    out[index] = found->get<float>();
                }
                return true;
            }
            return false;
        }

        inline bool ReadVector(const nlohmann::json &parent, const char *key, glm::vec3 &out) {
            const auto found = parent.find(key);
            float values[3] {};
            if (found == parent.end() || !ReadFloats(*found, 3, values)) {
                return false;
            }
            out = glm::vec3 {values[0], values[1], values[2]};
            return true;
        }

        inline bool ReadRotation(const nlohmann::json &parent, const char *key, glm::quat &out) {
            const auto found = parent.find(key);
            float values[4] {};
            if (found == parent.end() || !ReadFloats(*found, 4, values)) {
                return false;
            }
            const glm::quat read {values[3], values[0], values[1], values[2]};
            if (glm::length(read) < 1e-4F) {
                return false;
            }
            out = glm::normalize(read);
            return true;
        }

        inline std::string ReadString(const nlohmann::json &parent, const char *key) {
            const auto found = parent.find(key);
            return found != parent.end() && found->is_string() ? found->get<std::string>() : std::string();
        }

        inline bool ReadBool(const nlohmann::json &parent, const char *key) {
            const auto found = parent.find(key);
            return found != parent.end() && found->is_boolean() && found->get<bool>();
        }
    } // namespace LevelEditDefinitionJson

    /** The entry as a map's `world` array stores it: arrays for vectors, `[x, y, z, w]` for the rotation. */
    inline nlohmann::json LevelEditDefinitionToJson(const LevelEditDefinition &edit) {
        if (edit.kind == LevelEditKind::Grass) {
            return nlohmann::json {
                {"kind", LevelEditKindToString(edit.kind)},
                {"at", nlohmann::json::array({edit.at.x, edit.at.y, edit.at.z})},
                {"halfSize", nlohmann::json::array({edit.halfSize.x, edit.halfSize.y, edit.halfSize.z})},
                {"yaw", edit.yawDegrees},
                {"hidden", edit.hidden},
            };
        }
        nlohmann::json entry {
            {"kind", LevelEditKindToString(edit.kind)},
            {"name", edit.name},
            {"at", nlohmann::json::array({edit.at.x, edit.at.y, edit.at.z})},
            {"hidden", edit.hidden},
        };
        if (edit.kind == LevelEditKind::Entity) {
            // Text, as script spells it: a JSON number cannot hold 64 bits.
            entry["guid"]  = KCDC::Scripting::FormatLevelGuid(edit.guid);
            entry["class"] = edit.className;
        }
        if (!edit.model.empty()) {
            entry["model"] = edit.model;
        }
        if (edit.moved) {
            entry["position"] = nlohmann::json::array({edit.position.x, edit.position.y, edit.position.z});
            entry["rotation"] = nlohmann::json::array({edit.rotation.x, edit.rotation.y, edit.rotation.z, edit.rotation.w});
            entry["scale"]    = nlohmann::json::array({edit.scale.x, edit.scale.y, edit.scale.z});
        }
        return entry;
    }

    /**
     * Reads one entry. `at` and the key are required; `position` makes it a
     * move, and a move's rotation and scale default to none; `model` swaps
     * the object for that mesh. False, with `error` saying why, for an entry
     * nothing could be found again by.
     */
    inline bool LevelEditDefinitionFromJson(const nlohmann::json &entry, LevelEditDefinition &edit, std::string &error) {
        edit = LevelEditDefinition {};
        if (!entry.is_object()) {
            error = "an entry must be an object";
            return false;
        }

        const std::string kind = LevelEditDefinitionJson::ReadString(entry, "kind");
        if (kind == "entity") {
            edit.kind = LevelEditKind::Entity;
        }
        else if (kind == "grass") {
            edit.kind = LevelEditKind::Grass;
        }
        else if (kind.empty() || kind == "brush") {
            edit.kind = LevelEditKind::Brush;
        }
        else {
            error = "kind must be 'brush', 'entity' or 'grass', not '" + kind + "'";
            return false;
        }

        if (!LevelEditDefinitionJson::ReadVector(entry, "at", edit.at)) {
            error = edit.kind == LevelEditKind::Grass ? "at must be the centre of the box, as [x, y, z] or a Vector3" : "at must be where the level put the object, as [x, y, z] or a Vector3";
            return false;
        }

        if (edit.kind == LevelEditKind::Grass) {
            if (!LevelEditDefinitionJson::ReadVector(entry, "halfSize", edit.halfSize)) {
                error = "a grass edit needs halfSize, half the box along its two ground axes and up, as [x, y, z] or a Vector3";
                return false;
            }
            if (const auto yaw = entry.find("yaw"); yaw != entry.end()) {
                if (!yaw->is_number() || !std::isfinite(yaw->get<float>())) {
                    error = "yaw must be the box's turn about the vertical, in degrees";
                    return false;
                }
                edit.yawDegrees = yaw->get<float>();
            }
            if (!edit.HasKey()) {
                error = "a grass edit's half sizes must each be between 0.01 and 128 metres";
                return false;
            }
            edit.hidden = LevelEditDefinitionJson::ReadBool(entry, "hidden");
            return true;
        }

        edit.name      = LevelEditDefinitionJson::ReadString(entry, "name");
        edit.className = LevelEditDefinitionJson::ReadString(entry, "class");
        edit.guid      = KCDC::Scripting::ParseLevelGuid(LevelEditDefinitionJson::ReadString(entry, "guid"));
        edit.hidden    = LevelEditDefinitionJson::ReadBool(entry, "hidden");
        edit.model     = LevelEditDefinitionJson::ReadString(entry, "model");
        if (!edit.HasKey()) {
            error = edit.kind == LevelEditKind::Entity ? "an entity edit needs its level guid as sixteen hex digits" : "a brush edit needs its mesh path as name";
            return false;
        }

        edit.moved = LevelEditDefinitionJson::ReadVector(entry, "position", edit.position);
        if (edit.moved) {
            LevelEditDefinitionJson::ReadRotation(entry, "rotation", edit.rotation);
            LevelEditDefinitionJson::ReadVector(entry, "scale", edit.scale);
        }
        return true;
    }
} // namespace KCDC::Shared::Game
