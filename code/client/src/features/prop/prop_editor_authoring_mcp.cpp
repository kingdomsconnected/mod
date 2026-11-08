#include "prop_editor_authoring_mcp.h"
#include "prop_editor.h"
#include "shared/features/world_builder/world_document.h"
#include "shared/game/area_catalog.h"
#include "shared/game/npc_roles.h"
#include "shared/pose_math.h"
#include "shared/scripting/level_guid.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <unordered_set>
#include <utils/string_utils.h>

namespace KCDC::Features::Prop {
    namespace {
        using Json = nlohmann::json;
        Json Text(std::size_t maximum = 256, bool required = true) {
            return {{"type", "string"}, {"minLength", required ? 1 : 0}, {"maxLength", maximum}};
        }
        Json Number(double low, double high) {
            return {{"type", "number"}, {"minimum", low}, {"maximum", high}};
        }
        Json Integer(std::int64_t low, std::int64_t high) {
            return {{"type", "integer"}, {"minimum", low}, {"maximum", high}};
        }
        Json Boolean() {
            return {{"type", "boolean"}};
        }
        Json Enum(std::initializer_list<const char *> values) {
            return {{"type", "string"}, {"enum", values}};
        }
        Json Object(Json properties = Json::object(), Json required = Json::array()) {
            return {{"type", "object"}, {"properties", std::move(properties)}, {"required", std::move(required)}, {"additionalProperties", false}};
        }
        Json Array(Json item, int low, int high) {
            return {{"type", "array"}, {"items", std::move(item)}, {"minItems", low}, {"maxItems", high}};
        }
        Json VectorSchema() {
            return Object({{"x", Number(-16383.99, 16383.99)}, {"y", Number(-16383.99, 16383.99)}, {"z", Number(-16383.99, 16383.99)}}, {"x", "y", "z"});
        }
        Json QuaternionSchema() {
            return Object({{"x", Number(-1, 1)}, {"y", Number(-1, 1)}, {"z", Number(-1, 1)}, {"w", Number(-1, 1)}}, {"x", "y", "z", "w"});
        }
        Json TuningSchema() {
            return Object({{"scale", Number(0.01, 32)}, {"count", Number(0.01, 16)}, {"speed", Number(0, 16)}, {"time", Number(0.01, 16)}, {"strength", Number(-1, 1)}, {"pulse", Number(0, 600)}, {"prime", Boolean()}},
                {"scale", "count", "speed", "time", "strength", "pulse", "prime"});
        }
        Json AreaSchema() {
            return Object({{"id", Text(128)}, {"name", Text(256, false)}, {"type", Enum({"box", "shape", "sphere"})}, {"position", VectorSchema()}, {"rotation", QuaternionSchema()}, {"min", VectorSchema()}, {"max", VectorSchema()}, {"points", Array(VectorSchema(), 3, 256)},
                              {"height", Number(0, 16383.99)}, {"radius", Number(0.001, 16383.99)}, {"labels", Array(Text(256), 0, 256)}, {"metadata", {{"type", "object"}, {"additionalProperties", true}}},
                              {"virtualWorld", {{"type", "integer"}, {"minimum", 0}, {"maximum", 4294967295ULL}}}, {"enabled", Boolean()}, {"collision", Boolean()}, {"collisionFloor", Boolean()}, {"collisionCeiling", Boolean()}, {"collisionThickness", Number(0.05, 2)}},
                {"id", "type", "position"});
        }
        Json RouteSchema() {
            const auto point = Object({{"position", VectorSchema()}, {"speed", Enum({"walk", "jog", "run"})}, {"waitSeconds", Number(0, 3600)}, {"radius", Number(0.1, 100)}}, {"position"});
            return Object({{"id", Text(128)}, {"name", Text(256, false)}, {"mode", Enum({"once", "loop", "pingPong"})}, {"speed", Enum({"walk", "jog", "run"})}, {"waitSeconds", Number(0, 3600)}, {"radius", Number(0.1, 100)}, {"points", Array(point, 1, 256)}}, {"id", "points"});
        }
        Json PlacementSchema() {
            return Object({{"physics", Enum({"rigid", "static", "none"})}, {"alignToSurface", Boolean()}, {"snapToGrid", Boolean()}, {"gridStep", Number(0.1, 50)}, {"randomYaw", Boolean()}, {"randomScale", Boolean()}, {"scaleMin", Number(0.05, 20)},
                {"scaleMax", Number(0.05, 20)}, {"heightOffset", Number(-20, 20)}, {"fallbackDistance", Number(0.5, 200)}, {"effectTuning", TuningSchema()}, {"oneShotRepeat", Number(0, 30)}});
        }
        Json ViewSchema() {
            return Object({{"name", Text(256)}, {"position", VectorSchema()}, {"yawDegrees", Number(-360, 360)}, {"pitchDegrees", Number(-89, 89)}});
        }
        Json PageProperties() {
            return {{"offset", Integer(0, 1000000)}, {"limit", Integer(1, 200)}, {"query", Text(256, false)}};
        }
        void Require(bool ok, const std::string &error) {
            if (!ok) {
                throw std::runtime_error(error);
            }
        }
        template <typename V>
        Json Vector(const V &v) {
            return {{"x", v.x}, {"y", v.y}, {"z", v.z}};
        }
        glm::vec3 ReadVector(const Json &node) {
            return {node.at("x").get<float>(), node.at("y").get<float>(), node.at("z").get<float>()};
        }
        Json Tuning(const Shared::Game::VfxTuning &t) {
            return {{"scale", t.scale}, {"count", t.countScale}, {"speed", t.speedScale}, {"time", t.timeScale}, {"strength", t.strength}, {"pulse", t.pulsePeriod}, {"prime", t.prime}};
        }
        Json Placement(const PlacementOptions &p) {
            return {{"physics", KCDC::Common::Spawn::PropPhysicsToString(p.physics)}, {"alignToSurface", p.alignToSurface}, {"snapToGrid", p.snapToGrid}, {"gridStep", p.gridStep}, {"randomYaw", p.randomYaw}, {"randomScale", p.randomScale}, {"scaleMin", p.scaleMin},
                {"scaleMax", p.scaleMax}, {"heightOffset", p.heightOffset}, {"fallbackDistance", p.fallbackDistance}, {"effectTuning", Tuning(p.effectTuning)}, {"oneShotRepeat", p.oneShotRepeat}};
        }
        Json Asset(const AssetLibraryItem &item) {
            std::string model;
            const PlacedKind kind = SplitWorldObjectKey(item.path, model);
            return {{"name", item.name}, {"path", item.path}, {"directories", item.directories}, {"detail", item.detail}, {"placedKind", PlacedKindName(kind)}};
        }
        Json View(const DocumentView &view, std::size_t index) {
            return {{"index", index}, {"name", view.name}, {"position", Vector(view.position)}, {"yawDegrees", view.yawDegrees}, {"pitchDegrees", view.pitchDegrees}};
        }
        bool Matches(const std::string &value, const std::string &query) {
            return query.empty() || Framework::Utils::StringUtils::ContainsIgnoreCase(value, query);
        }
        Json Page(const Json &items, const Json &args) {
            const auto offset = args.value("offset", std::size_t(0)), limit = args.value("limit", std::size_t(50));
            auto page      = Json::array();
            const auto end = std::min(items.size(), offset + limit);
            for (auto i = offset; i < end; ++i) {
                page.push_back(items[i]);
            }
            return {{"items", std::move(page)}, {"total", items.size()}, {"nextOffset", end < items.size() ? Json(end) : Json(nullptr)}};
        }
        Shared::Game::AreaDefinition ReadArea(Json node) {
            const auto type = node.at("type").get<std::string>();
            Require(type != "box" || (node.contains("min") && node.contains("max")), "Box areas require min and max.");
            Require(type != "sphere" || node.contains("radius"), "Sphere areas require radius.");
            Require(type != "shape" || (node.contains("points") && node.contains("height")), "Polygon areas require points and height.");
            if (node.contains("rotation")) {
                const auto &r = node.at("rotation");
                Require(Shared::Game::IsUsableRotation(glm::quat(r.at("w").get<float>(), r.at("x").get<float>(), r.at("y").get<float>(), r.at("z").get<float>())), "Rotation must be nonzero and finite.");
            }
            if (node.contains("virtualWorld")) {
                // JSON Schema integers also allow integral numeric values such
                // as 0.0. The input schema has already checked range and type.
                node["virtualWorld"] = node.at("virtualWorld").get<std::uint32_t>();
            }
            Shared::Game::AreaDefinition area;
            std::string error;
            Require(Shared::Game::AreaDefinitionFromJson(node, area, error), error);
            return area;
        }
        const Shared::Game::LevelArea *FindLevelArea(const std::string &level, const std::string &id) {
            const auto guid     = KCDC::Scripting::ParseLevelGuid(id);
            const auto &catalog = Shared::Game::g_LevelAreas;
            const auto found    = std::find_if(catalog.begin(), catalog.end(), [&](const auto &row) {
                return row.level == level && row.guid == guid;
            });
            return found == catalog.end() ? nullptr : &*found;
        }
        std::vector<const Shared::Game::LevelArea *> LevelPolygons(const Shared::Game::LevelArea &source) {
            std::vector<const Shared::Game::LevelArea *> polygons;
            std::vector<const Shared::Game::LevelArea *> pending {&source};
            std::unordered_set<std::uint64_t> visited;
            while (!pending.empty()) {
                const auto *area = pending.back();
                pending.pop_back();
                if (!visited.insert(area->guid).second) {
                    continue;
                }
                if (area->areaClass != Shared::Game::LevelAreaClass::AreaUnion) {
                    polygons.push_back(area);
                }
                else {
                    for (std::size_t i = 0; i < area->memberCount; ++i) {
                        const auto *member = FindLevelArea(source.level, KCDC::Scripting::FormatLevelGuid(Shared::Game::g_LevelAreaMembers[area->firstMember + i]));
                        Require(member != nullptr, "Union contains an unavailable member.");
                        pending.push_back(member);
                    }
                }
            }
            return polygons;
        }
        Json LevelArea(const Shared::Game::LevelArea &area, bool detail) {
            Json value = {{"id", KCDC::Scripting::FormatLevelGuid(area.guid)}, {"level", area.level}, {"name", area.name}, {"class", Shared::Game::LevelAreaClassName(area.areaClass)}, {"labels", area.labels}, {"location", area.location}, {"height", area.height},
                {"bounds", {{"min", Vector(area.boundsMin)}, {"max", Vector(area.boundsMax)}}}};
            if (detail) {
                value["points"]  = Json::array();
                value["members"] = Json::array();
                value["doors"]   = Json::array();
                for (std::size_t i = 0; i < area.pointCount; ++i) {
                    value["points"].push_back(Vector(Shared::Game::g_LevelAreaPoints[area.firstPoint + i]));
                }
                for (std::size_t i = 0; i < area.memberCount; ++i) {
                    value["members"].push_back(KCDC::Scripting::FormatLevelGuid(Shared::Game::g_LevelAreaMembers[area.firstMember + i]));
                }
                for (std::size_t i = 0; i < area.doorCount; ++i) {
                    const auto &door = Shared::Game::g_LevelAreaDoors[area.firstDoor + i];
                    value["doors"].push_back({{"id", KCDC::Scripting::FormatLevelGuid(door.doorGuid)}, {"link", Shared::Game::AreaDoorLinkName(door.link)}});
                }
            }
            return value;
        }
    } // namespace

    nlohmann::json AuthoringMcp::Tools() {
        Json tools     = Json::array();
        const auto add = [&](const char *name, const char *description, Json schema, bool readOnly, bool destructive = false) {
            tools.push_back({{"name", name}, {"description", description}, {"inputSchema", std::move(schema)}, {"outputSchema", {{"type", "object"}, {"additionalProperties", true}}},
                {"annotations", {{"readOnlyHint", readOnly}, {"destructiveHint", destructive}, {"idempotentHint", readOnly}, {"openWorldHint", false}}}});
        };
        auto properties                  = PageProperties();
        properties["kind"]               = Enum({"prop", "effect"});
        properties["includeDevelopment"] = Boolean();
        add("wb_assets_search",
            "Search canonical prop paths or effect names, including negative search terms; paginated. The prop library also lists doors, smart objects and items under `doors/`, `interactive/` and `items/`, keyed `door:`, `smart:` and `item:`, which wb_props_create places; "
            "`placedKind` says which.",
            Object(properties, {"kind"}), true);
        add("wb_asset_get", "Read a canonical asset catalog entry.", Object({{"kind", Enum({"prop", "effect"})}, {"path", Text(1024)}}, {"kind", "path"}), true);
        add("wb_asset_preview", "Start or stop a pinned asset turntable preview. Requires the editor camera for start.", Object({{"kind", Enum({"prop", "effect"})}, {"path", Text(1024)}, {"enabled", Boolean()}}, {"kind"}), false);
        add("wb_asset_preferences_get", "Read favorite and recently placed canonical asset paths.", Object({{"kind", Enum({"prop", "effect"})}}, {"kind"}), true);
        add("wb_asset_preferences_update", "Set a catalog asset's favorite flag.", Object({{"kind", Enum({"prop", "effect"})}, {"path", Text(1024)}, {"favorite", Boolean()}}, {"kind", "path", "favorite"}), false);
        add("wb_viewport_get", "Read project gizmo space, picking, snapping and camera-relative grid preferences.", Object(), true);
        add("wb_keymap_get", "Read the editor's key bindings: every remappable action with its chord, the Default and W E R presets' chords, and any conflicts.", Object(), true);
        add("wb_keymap_update",
            "Rebind editor keys for this user. Applies an optional preset (default or wer) first, then keys: an object of action IDs to chords such as Ctrl+D, or an empty string to unbind. Saved with the editor settings; nothing changes when a chord is invalid.",
            Object({{"preset", Enum({"default", "wer"})}, {"keys", {{"type", "object"}, {"additionalProperties", {{"type", "string"}, {"maxLength", 64}}}}}}), false);
        add("wb_favorites_get", "Read project favorite folders and active folder; legacy asset preferences remain separate.", Object({{"kind", Enum({"prop", "effect"})}}, {"kind"}), true);
        add("wb_favorites_update", "Edit project favorite folders or their contents. Deleting a custom folder requires confirm=true; it never removes scene objects.",
            Object({{"kind", Enum({"prop", "effect"})}, {"action", Enum({"create", "rename", "delete", "activate", "add", "remove", "move"})}, {"folderId", Integer(1, std::numeric_limits<std::uint32_t>::max())},
                       {"targetFolderId", Integer(1, std::numeric_limits<std::uint32_t>::max())}, {"name", Text(80)}, {"path", Text(1024)}, {"confirm", Boolean()}},
                {"kind", "action"}),
            false);
        add("wb_viewport_update", "Patch project viewport preferences. Scale gizmos always use local axes. Grid spacing follows translation snap by default.",
            Object({{"settings", Object({{"space", Enum({"world", "local"})}, {"snapTranslate", Boolean()}, {"snapRotate", Boolean()}, {"snapScale", Boolean()}, {"snapTranslateStep", Number(0.01, 10)}, {"snapRotateStep", Number(1, 90)}, {"snapScaleStep", Number(0.01, 10)},
                                     {"pickPreferPhysics", Boolean()}, {"grid", Boolean()}, {"gridMatchSnap", Boolean()}, {"gridStep", Number(0.01, 50)}, {"gridHeightOffset", Number(-100, 100)}, {"gridLineWidth", Number(1, 8)}})}},
                {"settings"}),
            false);
        add("wb_environment_get", "Read the project's preview conditions -- pinned hour, clock speed, sky preset, rain -- with the live clock, sky and weather state and the warnings the two produce together.", Object(), true);
        add("wb_environment_update",
            "Patch the project's preview conditions. Offline only. The engine is reconciled with whatever changed, on the editor's next frame, so read wb_environment_get to confirm it landed. Naming the preset already chosen blends it in again. Preset names belong to the "
            "level: query wb_environment_presets_query first.",
            Object({{"settings", EnvironmentPreview::Schema()}}, {"settings"}), false);
        add("wb_environment_presets_query", "List the sky presets this level loaded and the weather profiles it schedules them from. A profile name is not a preset name and cannot be blended.", Object(PageProperties()), true);
        add("wb_environment_action",
            "Run one environment action. forceUpdate, rebuildClouds and refreshProfiles take no value; skipHours takes 0-72 game hours; setWorldHour takes 0-24 and may also move the clock back, which only the offline builder may do. These move the game's own clock rather "
            "than the project's preview.",
            Object({{"action", Enum({"forceUpdate", "rebuildClouds", "refreshProfiles", "skipHours", "setWorldHour"})}, {"value", Number(0, 72)}}, {"action"}), false);
        add("wb_placement_get", "Read placement brush settings and active asset.", Object(), true);
        add("wb_placement_update", "Patch placement settings; scaleMin must not exceed scaleMax.", Object({{"settings", PlacementSchema()}}, {"settings"}), false);
        properties          = PageProperties();
        properties["group"] = Text(256, false);
        add("wb_areas_query", "List authored areas, filtered by text or group.", Object(properties), true);
        add("wb_area_get", "Read one authored area and its group.", Object({{"id", Text(128)}}, {"id"}), true);
        add("wb_area_create", "Create a validated authored area with a unique ID.", Object({{"area", AreaSchema()}, {"group", Text(256, false)}}, {"area"}), false);
        add("wb_area_update", "Replace an authored area's full definition. Update points through the points array.", Object({{"id", Text(128)}, {"area", AreaSchema()}}, {"id", "area"}), false);
        for (const auto name : {"wb_areas_duplicate", "wb_areas_delete"}) {
            add(name, "Duplicate or delete all named authored areas atomically.", Object({{"ids", Array(Text(128), 1, 256)}}, {"ids"}), false, std::string(name) == "wb_areas_delete");
        }
        add("wb_areas_group", "Set the group of authored areas; empty group ungroups.", Object({{"ids", Array(Text(128), 1, 256)}, {"group", Text(256, false)}}, {"ids", "group"}), false);
        add("wb_area_test_points", "Test authored or compiled level area containment, independently of enabled/world filters.", Object({{"id", Text(128)}, {"source", Enum({"authored", "level"})}, {"level", Text(128)}, {"points", Array(VectorSchema(), 1, 256)}}, {"id", "points"}),
            true);
        properties           = PageProperties();
        properties["level"]  = Text(128);
        properties["class"]  = Enum({"TriggerArea", "AreaUnion", "SmartAreaShape"});
        properties["near"]   = VectorSchema();
        properties["radius"] = Number(0, 32768);
        add("wb_level_areas_query", "Query compiled level areas and bounds; level defaults to the current level.", Object(properties), true);
        add("wb_level_area_get", "Read a compiled level area including polygon, union membership, labels and door links.", Object({{"id", Text(18)}, {"level", Text(128)}}, {"id"}), true);
        add("wb_area_copy_from_level", "Copy a level area or union into editable polygon areas using the editor's existing conversion.", Object({{"id", Text(18)}, {"prefix", Text(96)}, {"group", Text(256, false)}}, {"id", "prefix"}), false);
        add("wb_npc_roles_query", "Discover compiled NPC roles for patrol preview, including soul, class, outfit and character metadata.", Object(PageProperties()), true);
        add("wb_patrols_query", "List authored patrol routes.", Object(PageProperties()), true);
        add("wb_patrol_get", "Read an authored patrol route.", Object({{"id", Text(128)}}, {"id"}), true);
        add("wb_patrol_create", "Create a validated patrol route.", Object({{"route", RouteSchema()}}, {"route"}), false);
        add("wb_patrol_update", "Replace a patrol route; insert, reorder or remove points through the full points array.", Object({{"id", Text(128)}, {"route", RouteSchema()}}, {"id", "route"}), false);
        add("wb_patrol_duplicate", "Duplicate an authored route with a fresh ID.", Object({{"id", Text(128)}}, {"id"}), false);
        add("wb_patrol_delete", "Delete an authored route and stop its preview.", Object({{"id", Text(128)}}, {"id"}), false, true);
        add("wb_patrol_preview_start", "Queue offline NPC patrol preview for an authored route and catalog NPC role.", Object({{"id", Text(128)}, {"role", Text(128)}}, {"id", "role"}), false);
        add("wb_patrol_preview_get", "Read patrol preview activity, progress and resolved path.", Object(), true);
        add("wb_patrol_preview_stop", "Stop the preview NPC through the game command queue.", Object(), false);
        add("wb_views_list", "List saved camera views; indices belong to this document revision. Paginated and filtered by name.", Object(PageProperties()), true);
        add("wb_view_get", "Read a saved camera view by index.", Object({{"index", Integer(0, 1000000)}}, {"index"}), true);
        properties              = ViewSchema()["properties"];
        properties["overwrite"] = Boolean();
        add("wb_view_save", "Save the current camera or supplied pose under a name; overwrite must be explicit.", Object(properties, {"name"}), false);
        add("wb_view_update", "Patch a saved camera view by index.", Object({{"index", Integer(0, 1000000)}, {"view", ViewSchema()}}, {"index", "view"}), false);
        add("wb_view_delete", "Delete a saved camera view by index.", Object({{"index", Integer(0, 1000000)}}, {"index"}), false, true);
        add("wb_view_restore", "Move the editor camera to a saved view by index.", Object({{"index", Integer(0, 1000000)}}, {"index"}), false);
        return tools;
    }

    bool AuthoringMcp::Execute(MapEditor &editor, const EditorSnapshot &snapshot, const std::string &name, const nlohmann::json &args, nlohmann::json &result) {
        std::string error;
        if (name == "wb_keymap_get" || name == "wb_keymap_update") {
            if (name == "wb_keymap_update") {
                EditorKeymap next = editor._keymap;
                if (args.contains("preset")) {
                    next.ApplyPreset(args.at("preset").get<std::string>());
                }
                if (args.contains("keys")) {
                    Require(next.UpdateFromJson(args.at("keys"), error), error);
                }
                editor._keymap = next;
            }
            result = {{"keys", editor._keymap.ToJson()}, {"actions", Json::array()}, {"conflicts", Json::array()}};
            for (std::size_t index = 0; index < EditorActions().size(); ++index) {
                const EditorActionInfo &info = EditorActions()[index];
                result["actions"].push_back({{"id", info.id}, {"label", info.label}, {"chord", KeyChordName(editor._keymap.Chord(static_cast<EditorAction>(index)))}, {"default", KeyChordName(info.standard)}, {"wer", KeyChordName(info.wer)}});
            }
            for (const auto &[first, second] : editor._keymap.Conflicts()) {
                result["conflicts"].push_back({EditorActions()[static_cast<std::size_t>(first)].id, EditorActions()[static_cast<std::size_t>(second)].id});
            }
            return true;
        }
        if (name == "wb_favorites_get" || name == "wb_favorites_update") {
            auto &library = args.at("kind") == "effect" ? editor._effectLibrary : editor._library;
            if (name == "wb_favorites_update") {
                const auto action = args.at("action").get<std::string>();
                const auto folder = args.value("folderId", library.GetFavoriteFolders().ActiveId());
                bool accepted     = false;
                if (action == "create") {
                    accepted = library.AddFavoriteFolder(args.at("name").get<std::string>(), error) != 0;
                }
                else if (action == "rename") {
                    accepted = library.RenameFavoriteFolder(folder, args.at("name").get<std::string>(), error);
                }
                else if (action == "delete") {
                    Require(args.value("confirm", false), "Folder deletion requires confirm=true.");
                    accepted = library.DeleteFavoriteFolder(folder, error);
                }
                else if (action == "activate") {
                    accepted = library.SetActiveFavoriteFolder(folder, error);
                }
                else if (action == "move") {
                    accepted = library.MoveProjectFavorite(args.at("path").get<std::string>(), folder, args.at("targetFolderId").get<std::uint32_t>(), error);
                }
                else if (action == "add" || action == "remove") {
                    accepted = library.SetProjectFavorite(args.at("path").get<std::string>(), action == "add", folder, error);
                }
                Require(accepted, error);
            }
            result = library.GetFavoriteFolders().ToJson();
            return true;
        }
        if (name.starts_with("wb_environment_")) {
            Require(snapshot.offline, "The environment is offline only: a connected session owns the clock and the weather, and a sky changed here would be this client's alone.");
            if (name == "wb_environment_update") {
                Require(editor._environment.Patch(args.at("settings"), error), error);
            }
            else if (name == "wb_environment_action") {
                Require(editor._environment.RunAction(args.at("action").get<std::string>(), args.value("value", 0.0F), error), error);
            }
            if (name == "wb_environment_presets_query") {
                result = editor._environment.DescribePresets(args.value("query", std::string {}), args.value("offset", std::size_t(0)), args.value("limit", std::size_t(50)));
            }
            else {
                result          = editor._environment.Describe();
                result["scope"] = "Preview conditions for this client's offline level. Saved with the project, reconciled with the engine on the editor's next frame, and absent from every export.";
            }
            return true;
        }
        if (name == "wb_viewport_get" || name == "wb_viewport_update") {
            if (name == "wb_viewport_update") {
                auto settings = editor._viewport.ToJson();
                settings.update(args.at("settings"));
                Require(EditorViewport::FromJson(settings, editor._viewport, error), error);
            }
            result                      = editor._viewport.ToJson();
            result["effectiveGridStep"] = editor._viewport.GridStep();
            return true;
        }
        const bool authoredRead = name.ends_with("_get") || name.ends_with("_query") || name.ends_with("_export") || name == "wb_area_test_points";
        if (!authoredRead && name != "wb_patrol_preview_stop" && (name.starts_with("wb_area") || name.starts_with("wb_patrol")) && !snapshot.layers->Editable("area:default")) {
            Require(false, "Default layer is hidden or locked.");
        }
        const auto queuePatrolChange = [&] {
            MapEditor::Command command;
            command.kind = MapEditor::CommandKind::PatrolPreview;
            if (editor._patrols.TakePreviewRequest(command.patrolRoute, command.previewRole)) {
                editor.Queue(std::move(command));
                result["queued"] = true;
            }
        };
        if (name == "wb_assets_search" || name == "wb_asset_get" || name == "wb_asset_preview" || name == "wb_asset_preferences_get" || name == "wb_asset_preferences_update") {
            const bool effect = args.at("kind") == "effect";
            auto &library     = effect ? editor._effectLibrary : editor._library;
            if (name == "wb_assets_search") {
                const auto matches = library.Search(args.value("query", std::string()), args.value("includeDevelopment", false));
                const auto offset = args.value("offset", std::size_t(0)), limit = args.value("limit", std::size_t(50));
                const auto end = std::min(matches.size(), offset + limit);
                auto rows      = Json::array();
                for (auto i = offset; i < end; ++i) {
                    rows.push_back(Asset(library.Items()[matches[i]]));
                }
                result = {{"items", std::move(rows)}, {"total", matches.size()}, {"nextOffset", end < matches.size() ? Json(end) : Json(nullptr)}};
            }
            else if (name == "wb_asset_preferences_get") {
                result = {{"favorites", library.Favorites()}, {"recent", library.Recent()}};
            }
            else if (name == "wb_asset_preview" && !args.value("enabled", true)) {
                MapEditor::Command command;
                command.kind             = MapEditor::CommandKind::Preview;
                command.enable           = false;
                editor._mcpPreviewPinned = false;
                editor._previewSent      = AssetLibrary::kNone;
                editor.Queue(std::move(command));
                result = {{"queued", true}, {"enabled", false}};
            }
            else {
                Require(args.contains("path"), "A canonical asset path is required.");
                const auto path   = args.at("path").get<std::string>();
                const auto *asset = library.Find(path);
                Require(asset != nullptr, "Unknown catalog asset: " + path);
                if (name == "wb_asset_get") {
                    result          = {{"asset", Asset(*asset)}};
                    const int index = static_cast<int>(asset - library.Items().data());
                    if (snapshot.previewModel == index && snapshot.previewIsEffect == effect) {
                        result["preview"] = {{"active", true}, {"sizeKnown", snapshot.previewSized}};
                        if (snapshot.previewSized) {
                            result["preview"]["size"] = Vector(snapshot.previewSize);
                        }
                    }
                }
                else if (name == "wb_asset_preferences_update") {
                    Require(library.SetFavorite(path, args.at("favorite").get<bool>()), "Unknown catalog asset.");
                    result = {{"path", path}, {"favorite", args.at("favorite")}};
                }
                else {
                    Require(snapshot.cameraHeld, "The editor camera must be active to preview an asset.");
                    const auto index = static_cast<int>(asset - library.Items().data());
                    MapEditor::Command command;
                    command.kind              = MapEditor::CommandKind::Preview;
                    command.enable            = true;
                    command.effect            = effect;
                    command.id                = static_cast<std::uint64_t>(index);
                    editor._mcpPreviewPinned  = true;
                    editor._previewSent       = index;
                    editor._previewSentEffect = effect;
                    editor.Queue(std::move(command));
                    result = {{"queued", true}, {"asset", Asset(*asset)}};
                }
            }
            return true;
        }
        if (name == "wb_placement_get" || name == "wb_placement_update") {
            if (name == "wb_placement_update") {
                auto value = Placement(editor._placementDraft);
                value.update(args.at("settings"));
                PlacementOptions draft;
                const auto physics     = value.at("physics").get<std::string>();
                draft.physics          = KCDC::Common::Spawn::PropPhysicsFromString(physics).value_or(KCDC::Common::Spawn::PropPhysics::Static);
                draft.alignToSurface   = value.at("alignToSurface").get<bool>();
                draft.snapToGrid       = value.at("snapToGrid").get<bool>();
                draft.gridStep         = value.at("gridStep").get<float>();
                draft.randomYaw        = value.at("randomYaw").get<bool>();
                draft.randomScale      = value.at("randomScale").get<bool>();
                draft.scaleMin         = value.at("scaleMin").get<float>();
                draft.scaleMax         = value.at("scaleMax").get<float>();
                draft.heightOffset     = value.at("heightOffset").get<float>();
                draft.fallbackDistance = value.at("fallbackDistance").get<float>();
                draft.oneShotRepeat    = value.at("oneShotRepeat").get<float>();
                draft.effectTuning     = Shared::WorldBuilder::ReadTuning(value.at("effectTuning"));
                Require(draft.scaleMin <= draft.scaleMax, "scaleMin must not exceed scaleMax.");
                Require(draft.effectTuning.IsSane(), "Invalid effect tuning.");
                editor._placementDraft    = draft;
                editor._placementSent     = draft;
                editor._placementEverSent = true;
                MapEditor::Command command;
                command.kind      = MapEditor::CommandKind::Placement;
                command.placement = draft;
                editor.Queue(std::move(command));
            }
            result = {{"settings", Placement(editor._placementDraft)}, {"armed", snapshot.armedModel != AssetLibrary::kNone}, {"armedKind", snapshot.armedIsEffect ? "effect" : "prop"}, {"yawDegrees", snapshot.armedYaw}, {"scale", snapshot.armedScale}};
            if (snapshot.armedModel != AssetLibrary::kNone) {
                const auto &items = (snapshot.armedIsEffect ? editor._effectLibrary : editor._library).Items();
                if (snapshot.armedModel >= 0 && static_cast<std::size_t>(snapshot.armedModel) < items.size()) {
                    result["armedPath"] = items[snapshot.armedModel].path;
                }
            }
            if (name == "wb_placement_update") {
                result["queued"] = true;
            }
            return true;
        }
        if (name == "wb_areas_query") {
            auto rows        = Json::array();
            const auto query = args.value("query", std::string());
            for (const auto &area : editor._areas.Authored()) {
                const auto found = editor._areas.Groups().find(area.id);
                const auto group = found == editor._areas.Groups().end() ? std::string() : found->second;
                if ((!Matches(area.id, query) && !Matches(area.name, query)) || (args.contains("group") && group != args.at("group").get<std::string>())) {
                    continue;
                }
                rows.push_back({{"id", area.id}, {"name", area.name}, {"type", Shared::Game::AreaShapeTypeName(area.shape.type)}, {"position", Vector(area.shape.position)}, {"group", group}, {"enabled", area.enabled}, {"collision", area.collision},
                    {"collisionFloor", area.collisionOptions.floor}, {"collisionCeiling", area.collisionOptions.ceiling}, {"collisionThickness", area.collisionOptions.thickness}});
            }
            result = Page(rows, args);
            return true;
        }
        if (name == "wb_area_test_points" && args.value("source", std::string("authored")) == "level") {
            const auto *source = FindLevelArea(args.value("level", snapshot.level), args.at("id").get<std::string>());
            Require(source != nullptr, "Level area not found.");
            const auto polygons = LevelPolygons(*source);
            auto points         = Json::array();
            for (const auto &point : args.at("points")) {
                const auto position = ReadVector(point);
                const bool inside   = std::any_of(polygons.begin(), polygons.end(), [&](const auto *area) {
                    return Shared::Game::LevelAreaContains(*area, position);
                });
                points.push_back({{"point", point}, {"inside", inside}});
            }
            result = {{"id", args.at("id")}, {"source", "level"}, {"results", std::move(points)}};
            return true;
        }
        if (name == "wb_area_get" || name == "wb_area_test_points") {
            const auto id    = args.at("id").get<std::string>();
            const auto *area = editor._areas.Find(id);
            Require(area != nullptr, "Area not found: " + id);
            if (name == "wb_area_get") {
                const auto group = editor._areas.Groups().find(id);
                result           = {{"area", Shared::Game::AreaDefinitionToJson(*area)}, {"group", group == editor._areas.Groups().end() ? std::string() : group->second}};
            }
            else {
                Require(area->shape.IsValid() && Shared::Game::IsUsableRotation(area->shape.rotation), "The area has invalid geometry.");
                const auto world = area->shape.WorldPoints();
                auto points      = Json::array();
                for (const auto &point : args.at("points")) {
                    points.push_back({{"point", point}, {"inside", area->shape.Contains(ReadVector(point), world)}});
                }
                result = {{"id", id}, {"results", std::move(points)}, {"enabled", area->enabled}};
            }
            return true;
        }
        if (name == "wb_area_create" || name == "wb_area_update") {
            const auto area = ReadArea(args.at("area"));
            if (name == "wb_area_create") {
                Require(editor._areas.Create(area, error), error);
                if (args.contains("group")) {
                    Require(editor._areas.SetGroup({area.id}, args.at("group").get<std::string>(), error), error);
                }
            }
            else {
                Require(editor._areas.Update(args.at("id").get<std::string>(), area, error), error);
            }
            result = {{"area", Shared::Game::AreaDefinitionToJson(area)}};
            return true;
        }
        if (name == "wb_areas_duplicate" || name == "wb_areas_delete" || name == "wb_areas_group") {
            const auto ids = args.at("ids").get<std::vector<std::string>>();
            std::unordered_set<std::string> seen;
            for (const auto &id : ids) {
                Require(editor._areas.Find(id) != nullptr, "Area not found: " + id);
                Require(seen.insert(id).second, "Duplicate input area ID: " + id);
            }
            if (name == "wb_areas_group") {
                Require(editor._areas.SetGroup(ids, args.at("group").get<std::string>(), error), error);
                result = {{"ids", ids}, {"group", args.at("group")}};
            }
            else if (name == "wb_areas_delete") {
                for (const auto &id : ids) {
                    Require(editor._areas.Remove(id, error), error);
                }
                result = {{"deletedIds", ids}};
            }
            else {
                auto areas  = editor._areas.Authored();
                auto groups = editor._areas.Groups();
                std::unordered_set<std::string> taken;
                for (const auto &area : areas) {
                    taken.insert(area.id);
                }
                auto created = Json::array();
                for (const auto &id : ids) {
                    auto copy = *editor._areas.Find(id);
                    for (std::size_t suffix = 1;; ++suffix) {
                        copy.id = id.substr(0, 110) + "_" + std::to_string(suffix);
                        if (taken.insert(copy.id).second) {
                            break;
                        }
                    }
                    if (const auto group = groups.find(id); group != groups.end()) {
                        groups[copy.id] = group->second;
                    }
                    created.push_back(copy.id);
                    areas.push_back(std::move(copy));
                }
                Require(editor._areas.ReplaceState(areas, groups, editor._areas.SelectedIds(), error), error);
                result = {{"createdIds", std::move(created)}};
            }
            return true;
        }
        if (name == "wb_level_areas_query") {
            const auto level = args.value("level", snapshot.level), query = args.value("query", std::string());
            const auto nearPoint = args.contains("near") ? std::optional(ReadVector(args.at("near"))) : std::nullopt;
            auto rows            = Json::array();
            for (const auto &area : Shared::Game::g_LevelAreas) {
                if (area.level != level || (!Matches(area.name, query) && !Matches(area.labels, query) && !Matches(area.location, query))) {
                    continue;
                }
                if (args.contains("class") && args.at("class") != Shared::Game::LevelAreaClassName(area.areaClass)) {
                    continue;
                }
                if (nearPoint && Shared::Game::DistanceToBox(*nearPoint, Shared::Game::ToVec3(area.boundsMin), Shared::Game::ToVec3(area.boundsMax)) > args.value("radius", 100.0F)) {
                    continue;
                }
                rows.push_back(LevelArea(area, false));
            }
            result = Page(rows, args);
            return true;
        }
        if (name == "wb_level_area_get" || name == "wb_area_copy_from_level") {
            const auto *source = FindLevelArea(args.value("level", snapshot.level), args.at("id").get<std::string>());
            Require(source != nullptr, "Level area not found.");
            if (name == "wb_level_area_get") {
                result = {{"area", LevelArea(*source, true)}};
                return true;
            }
            auto areas  = editor._areas.Authored();
            auto groups = editor._areas.Groups();
            std::unordered_set<std::string> taken;
            for (const auto &area : areas) {
                taken.insert(area.id);
            }
            auto created      = Json::array();
            const auto prefix = args.at("prefix").get<std::string>();
            for (const auto *area : LevelPolygons(*source)) {
                std::string id = prefix;
                for (std::size_t suffix = 2; taken.contains(id); ++suffix) {
                    id = prefix + "_" + std::to_string(suffix);
                }
                taken.insert(id);
                Shared::Game::AreaDefinition copy;
                Require(EditableLevelArea(*area, std::span(Shared::Game::g_LevelAreaPoints).subspan(area->firstPoint, area->pointCount), id, copy), "This level area cannot be converted to an editable polygon.");
                areas.push_back(std::move(copy));
                created.push_back(id);
                if (args.contains("group") && !args.at("group").get<std::string>().empty()) {
                    groups[id] = args.at("group").get<std::string>();
                }
            }
            Require(!created.empty(), "The level area has no editable members.");
            Require(editor._areas.ReplaceState(areas, groups, editor._areas.SelectedIds(), error), error);
            result = {{"createdIds", std::move(created)}};
            return true;
        }
        if (name == "wb_npc_roles_query") {
            const auto query = args.value("query", std::string());
            auto roles       = Json::array();
            for (const auto &role : Shared::Game::g_NpcRoles) {
                if (!Matches(std::string(role.name), query) && !Matches(std::string(role.character), query) && !Matches(std::string(role.actorClass), query)) {
                    continue;
                }
                roles.push_back({{"name", role.name}, {"soulGuid", role.soulGuid}, {"actorClass", role.actorClass}, {"clothingPresetGuid", role.clothingPresetGuid}, {"character", role.character}});
            }
            result = Page(roles, args);
            return true;
        }
        if (name == "wb_patrols_query") {
            auto rows        = Json::array();
            const auto query = args.value("query", std::string());
            for (const auto &route : editor._patrols.Authored()) {
                if (Matches(route.id, query) || Matches(route.name, query)) {
                    rows.push_back({{"id", route.id}, {"name", route.name}, {"mode", Shared::Game::PatrolModeName(route.mode)}, {"pointCount", route.points.size()}});
                }
            }
            result = Page(rows, args);
            return true;
        }
        if (name == "wb_patrol_get") {
            const auto *route = editor._patrols.Find(args.at("id").get<std::string>());
            Require(route != nullptr, "Patrol route not found.");
            result = {{"route", Shared::Game::PatrolRouteToJson(*route, true)}};
            return true;
        }
        if (name == "wb_patrol_create" || name == "wb_patrol_update") {
            Shared::Game::PatrolRouteDefinition route;
            Require(Shared::Game::PatrolRouteFromJson(args.at("route"), route, error), error);
            if (name == "wb_patrol_create") {
                Require(editor._patrols.Create(route, error), error);
            }
            else {
                Require(editor._patrols.Update(args.at("id").get<std::string>(), route, error), error);
            }
            result = {{"route", Shared::Game::PatrolRouteToJson(route, true)}};
            queuePatrolChange();
            return true;
        }
        if (name == "wb_patrol_duplicate") {
            std::string created;
            Require(editor._patrols.Duplicate(args.at("id").get<std::string>(), created, error), error);
            result = {{"id", created}};
            queuePatrolChange();
            return true;
        }
        if (name == "wb_patrol_delete") {
            Require(editor._patrols.Remove(args.at("id").get<std::string>(), error), error);
            result = {{"deletedId", args.at("id")}};
            queuePatrolChange();
            return true;
        }
        if (name == "wb_patrol_preview_get") {
            auto path = Json::array();
            for (const auto &point : snapshot.patrolPath) {
                path.push_back(Vector(point));
            }
            result = {{"active", snapshot.patrolPreviewActive}, {"progress", snapshot.patrolProgress}, {"status", snapshot.patrolPreviewStatus}, {"path", std::move(path)}};
            return true;
        }
        if (name == "wb_patrol_preview_start" || name == "wb_patrol_preview_stop") {
            if (name == "wb_patrol_preview_start") {
                Require(snapshot.cameraHeld, "The editor camera must be active for patrol preview.");
                Require(editor._patrols.RequestPreview(args.at("id").get<std::string>(), args.at("role").get<std::string>(), error), error);
            }
            else {
                editor._patrols.StopPreview();
            }
            MapEditor::Command command;
            command.kind = MapEditor::CommandKind::PatrolPreview;
            Require(editor._patrols.TakePreviewRequest(command.patrolRoute, command.previewRole), "No patrol preview request was queued.");
            editor.Queue(std::move(command));
            result = {{"queued", true}};
            return true;
        }
        if (name == "wb_views_list") {
            auto views       = Json::array();
            const auto query = args.value("query", std::string());
            for (std::size_t i = 0; i < editor._views.size(); ++i) {
                if (Matches(editor._views[i].name, query)) {
                    views.push_back(View(editor._views[i], i));
                }
            }
            result = Page(views, args);
            return true;
        }
        const auto patchView = [](DocumentView &view, const Json &patch) {
            if (patch.contains("name")) {
                view.name = patch.at("name").get<std::string>();
            }
            if (patch.contains("position")) {
                const auto position = ReadVector(patch.at("position"));
                view.position       = {position.x, position.y, position.z};
            }
            if (patch.contains("yawDegrees")) {
                view.yawDegrees = patch.at("yawDegrees").get<float>();
            }
            if (patch.contains("pitchDegrees")) {
                view.pitchDegrees = patch.at("pitchDegrees").get<float>();
            }
        };
        if (name == "wb_view_save") {
            DocumentView view {args.at("name").get<std::string>(), snapshot.camera.position, snapshot.camera.yawDegrees, snapshot.camera.pitchDegrees};
            patchView(view, args);
            std::size_t index = editor._views.size();
            for (std::size_t i = 0; i < editor._views.size(); ++i) {
                if (editor._views[i].name == view.name) {
                    Require(index == editor._views.size(), "Multiple views share that name; use indexed update.");
                    Require(args.value("overwrite", false), "A view has that name; pass overwrite=true or choose another name.");
                    index = i;
                }
            }
            if (index == editor._views.size()) {
                editor._views.push_back(view);
            }
            else {
                editor._views[index] = view;
            }
            ++editor._viewsRevision;
            result = {{"view", View(view, index)}};
            return true;
        }
        if (name == "wb_view_get" || name == "wb_view_update" || name == "wb_view_delete" || name == "wb_view_restore") {
            const auto index = args.at("index").get<std::size_t>();
            Require(index < editor._views.size(), "View index is no longer present; list views again.");
            auto view = editor._views[index];
            if (name == "wb_view_update") {
                patchView(view, args.at("view"));
                editor._views[index] = view;
                ++editor._viewsRevision;
            }
            if (name == "wb_view_delete") {
                editor._views.erase(editor._views.begin() + index);
                ++editor._viewsRevision;
                result = {{"deletedIndex", index}};
            }
            else {
                result = {{"view", View(view, index)}};
            }
            if (name == "wb_view_restore") {
                Require(snapshot.cameraHeld, "The editor camera must be active to restore a view.");
                MapEditor::Command command;
                command.kind     = MapEditor::CommandKind::CameraView;
                command.position = view.position;
                command.value    = view.yawDegrees;
                command.value2   = view.pitchDegrees;
                editor.Queue(std::move(command));
                result["queued"] = true;
            }
            return true;
        }
        return false;
    }
} // namespace KCDC::Features::Prop
