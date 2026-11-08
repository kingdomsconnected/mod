#pragma once

#include "sdk/types.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace KCDC::Features::Prop {
    /**
     * What a placed record stands for. A prop is a mesh; the rest are things
     * the server builds as the real article from the export's `doors`,
     * `smartObjects` and `items` arrays -- a door that opens and locks, a
     * bench to sit on or a station to craft at, a stack to pick up -- and
     * which the editor shows as a preview in the meantime.
     */
    enum class PlacedKind : std::uint8_t {
        Prop,
        Door,
        SmartObject,
        Item,
    };

    /** What a preview shows where the game will not draw the real thing: a door's leaf in a ghost, a worn item. */
    inline constexpr const char *kWorldObjectStandInMesh = "objects/special/primitive_cube_noproxy.cgf";

    /** `prop`, `door`, `smartObject` or `item`: the MCP's and the document's word for it. */
    const char *PlacedKindName(PlacedKind kind);
    std::optional<PlacedKind> PlacedKindFromName(std::string_view name);

    /** The document array a kind other than a prop exports to: `doors`, `smartObjects`, `items`. */
    const char *WorldObjectArray(PlacedKind kind);

    /**
     * A library row's key: `door:<model>`, `smart:<blueprint>`, `item:<class>`,
     * and a bare mesh path for a prop -- so favourites, recents and search
     * treat the four alike.
     */
    std::string WorldObjectKey(PlacedKind kind, std::string_view model);

    /** The kind a key names, with the model after its prefix; a key without one is a prop's mesh path. */
    PlacedKind SplitWorldObjectKey(std::string_view key, std::string &model);

    /**
     * Checks `model` and `settings` with the readers the server loads the
     * export through, and writes both back canonically: a door's leaf by its
     * catalog path, a blueprint by its catalog name, a shipped item class by
     * its GUID (a name the client does not know, a custom item's, is kept as
     * written for the server to resolve), and `settings` holding exactly the
     * fields its kind takes. Null or missing settings are the defaults.
     */
    bool NormalizeWorldObject(PlacedKind kind, std::string &model, nlohmann::json &settings, std::string &error);

    /** The entry a kind's document array carries, in the format `Door.spawn`, `SmartObject.spawn` and a world resource's `items` take. */
    nlohmann::json WorldObjectToJson(PlacedKind kind, const std::string &model, const nlohmann::json &settings, const SDK::Vec3 &position, const SDK::Quat &rotation);

    /** Reads such an entry back; false, with `error` saying why, for one the server would refuse. */
    bool WorldObjectFromJson(PlacedKind kind, const nlohmann::json &entry, std::string &model, nlohmann::json &settings, SDK::Vec3 &position, SDK::Quat &rotation, std::string &error);

    /** What reads in a list: a door leaf's file stem, a blueprint's last segment, an item class's name. */
    std::string WorldObjectName(PlacedKind kind, const std::string &model);

    /** One door leaf, blueprint or item class the library offers beside the meshes. */
    struct WorldObjectRow {
        PlacedKind kind = PlacedKind::Door;
        std::string model; // canonical, as NormalizeWorldObject leaves it
        std::string name;
        std::vector<std::string> directories; // `doors`, `interactive/<what>`, `items/<category>`
        std::string detail;
    };

    /**
     * Every door leaf the levels hang, every placeable blueprint and every
     * shipped item class, in that order. The library lists them after the
     * mesh catalog, so a library row past the last mesh is one of these.
     */
    const std::vector<WorldObjectRow> &WorldObjectRows();

    /** The world object a mesh library row stands for, or null for a mesh's row. */
    const WorldObjectRow *WorldObjectAtLibraryRow(int index);
} // namespace KCDC::Features::Prop
