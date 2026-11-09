#pragma once

#include "common/spawn/prop_spawner.h"
#include "prop_editor_environment.h"
#include "prop_editor_favorites.h"
#include "prop_editor_layers.h"
#include "prop_editor_viewport.h"
#include "prop_editor_world_objects.h"
#include "sdk/types.h"
#include "shared/features/area/area_definition.h"
#include "shared/features/level_edit/level_edit_definition.h"
#include "shared/features/light/light_tuning.h"
#include "shared/features/patrol/patrol_route.h"
#include "shared/features/vfx/vfx_tuning.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace KCDC::Features::Prop {
    /**
     * One thing the editor placed, as a document carries it: a mesh in
     * `objects`, or a door, smart object or item in its own array, which is
     * what `kind` says.
     */
    struct DocumentObject {
        std::string model; // a prop's mesh path; a door's leaf, a blueprint or an item class
        SDK::Vec3 position {};
        SDK::Quat rotation {0.0F, 0.0F, 0.0F, 1.0F};
        SDK::Vec3 scale {1.0F, 1.0F, 1.0F};
        KCDC::Common::Spawn::PropPhysics physics = KCDC::Common::Spawn::PropPhysics::Static;
        std::string group;
        std::string layer = "default";
        PlacedKind kind   = PlacedKind::Prop;
        nlohmann::json settings; // everything else its kind's entry holds; see NormalizeWorldObject
    };

    /** One particle effect the editor placed. */
    struct DocumentEffect {
        std::string effect; // the catalog name, `WH_Particels.fires.torch_a`
        SDK::Vec3 position {};
        SDK::Quat rotation {0.0F, 0.0F, 0.0F, 1.0F};
        Shared::Game::VfxTuning tuning;
        std::string group;
        std::string layer = "default";
    };

    /** One point light placed independently of models and particle effects. */
    struct DocumentLight {
        SDK::Vec3 position {};
        Shared::Game::LightTuning tuning;
        std::string group;
        std::string layer = "default";
    };

    /** A saved camera position. */
    struct DocumentView {
        std::string name;
        SDK::Vec3 position {};
        float yawDegrees   = 0.0F;
        float pitchDegrees = 0.0F;
    };

    struct DocumentWorldLayer {
        Shared::Game::LevelEditDefinition object;
        std::string layer;
    };

    /** Authored scene and editor preferences, shared by project and legacy map readers. */
    struct MapDocument {
        EditorLayers layers;
        std::vector<DocumentWorldLayer> worldLayers;
        EditorViewport viewport;
        EnvironmentPreview environment;
        FavoriteFolders propFavorites;
        FavoriteFolders effectFavorites;
        std::string level;
        std::string exportDirectory;
        std::string resourceName = "untitled";
        /** The resources folder the owner picked, and the client mods ticked in it. */
        std::string clientModsFolder;
        std::vector<std::string> clientMods;
        std::vector<DocumentObject> objects;
        std::vector<DocumentEffect> effects;
        std::vector<DocumentLight> lights;
        std::vector<Shared::Game::LevelEditDefinition> world; // the format the server's LevelEdit reads too
        std::vector<DocumentView> views;
        std::optional<DocumentView> camera;

        /** Areas drawn for scripts, in the format `Area.create` takes. */
        std::vector<Shared::Game::AreaDefinition> areas;
        std::vector<Shared::Game::PatrolRouteDefinition> patrolRoutes;
        std::unordered_map<std::string, std::string> areaGroups;
    };

    /** The scene as the `kcdc.map` object every export, blueprint and API copy is built from. */
    nlohmann::json MapDocumentToJson(const MapDocument &document);
    std::string SerializeMapDocument(const MapDocument &document);

    /** Counts malformed entries in `skipped`; false only when it is not a map at all. */
    bool MapDocumentFromJson(const nlohmann::json &root, MapDocument &document, std::size_t &skipped, std::string &error);
    bool ParseMapDocument(const std::string &text, MapDocument &document, std::size_t &skipped, std::string &error);

    /** A `.project.json`: the scene plus the editor's export preferences. Compared whole to tell whether a project is unsaved. */
    nlohmann::json ProjectToJson(const MapDocument &document);
    std::string SerializeProject(const MapDocument &document);

    /** Refuses anything but a valid project; a malformed entry fails the whole file rather than being dropped. */
    bool ParseProject(const std::string &text, MapDocument &document, std::string &error);

    /** Recent project paths, newest first; ignores invalid entries and keeps at most ten unique paths. */
    std::vector<std::string> ParseRecentProjects(const std::string &text);

    /** Moves every object, effect, light and area by `offset`; level edits stay where the level put them. */
    void OffsetDocument(MapDocument &document, const SDK::Vec3 &offset);

    /** A `.blueprint.json` relative to its first member. Refuses level edits, which only mean something in their own level. */
    bool SerializeBlueprint(MapDocument document, std::string &text, std::string &error);
    bool ParseBlueprint(const std::string &text, MapDocument &document, std::string &error);

    /** Saved maps: `maps/<name>.json` beside KCDCClient.dll. */
    class MapFiles final {
      public:
        /** Created on first use; empty when the module path is unknown. */
        static const std::string &Directory();

        static std::string RecentProjectsPath();
        static std::vector<std::string> RecentProjects();

        /** Keeps letters, digits, `-` and `_`; everything else becomes `_`. */
        static std::string SanitizeName(const std::string &name);

        /** Writes a UTF-8 path through a staged file, so a failed save never leaves half a document. At most 32 MiB. */
        static bool WritePath(const std::string &path, const std::string &text, std::string &error);

        /** Reads a UTF-8 path whole. At most 32 MiB. */
        static bool ReadPath(const std::string &path, std::string &text, std::string &error);

        /**
         * The native open or save dialog for a project or blueprint. Modal: it
         * blocks the calling thread, and the game's frame with it, until the
         * player answers. Empty on cancellation or failure; failures also set
         * `error`.
         */
        static std::string ChoosePath(bool save, bool blueprint, const std::string &current, std::string &error);

        /** Lists child folder names and resolves the current directory as UTF-8 for the in-editor browser. */
        static bool ListDirectories(const std::string &path, std::string &directory, std::vector<std::string> &entries, std::string &error);
    };
} // namespace KCDC::Features::Prop
