#include "core/application.h"
#include "core/dlc.h"
#include "features/camera/camera_view.h"
#include "prop_editor.h"
#include "prop_editor_capture.h"
#include "prop_editor_mcp.h"
#include "sdk/cryengine/entitysystem/i_entity_system.h"
#include "sdk/cryengine/physics/c_physical_world.h"
#include "shared/features/world_builder/world_document.h"
#include "shared/game/dlc_catalog.h"
#include "shared/game/levels.h"
#include "shared/game/prop_models.h"
#include "shared/game/vfx_catalog.h"

// After application.h, which brings in winsock before anything includes windows.h.
#include "common/module_support.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <stdexcept>
#include <unordered_set>

namespace KCDC::Features::Prop {
    namespace {
        nlohmann::json Vec(const SDK::Vec3 &v) {
            return {v.x, v.y, v.z};
        }
        nlohmann::json Quat(const SDK::Quat &q) {
            return {q.x, q.y, q.z, q.w};
        }
        SDK::Vec3 ReadVec(const nlohmann::json &value) {
            if (!Shared::WorldBuilder::Numbers(value, 3)) {
                throw std::runtime_error("Expected three finite coordinates");
            }
            return {value[0].get<float>(), value[1].get<float>(), value[2].get<float>()};
        }
        SDK::Quat ReadQuat(const nlohmann::json &value) {
            if (!Shared::WorldBuilder::Pose({{"position", {0, 0, 0}}, {"rotation", value}})) {
                throw std::runtime_error("Expected a finite, nonzero quaternion [x,y,z,w]");
            }
            float length = 0;
            for (const auto &number : value) {
                length += number.get<float>() * number.get<float>();
            }
            length = std::sqrt(length);
            return {value[0].get<float>() / length, value[1].get<float>() / length, value[2].get<float>() / length, value[3].get<float>() / length};
        }
        KCDC::Common::Spawn::PropPhysics Physics(const std::string &name) {
            if (const auto physics = KCDC::Common::Spawn::PropPhysicsFromString(name)) {
                return *physics;
            }
            throw std::runtime_error("Physics must be static, rigid or none");
        }
        nlohmann::json Tuning(const Shared::Game::VfxTuning &t) {
            return {{"scale", t.scale}, {"count", t.countScale}, {"speed", t.speedScale}, {"time", t.timeScale}, {"strength", t.strength}, {"pulse", t.pulsePeriod}, {"prime", t.prime}};
        }
        nlohmann::json Text(std::size_t limit = 256) {
            return {{"type", "string"}, {"maxLength", limit}};
        }
        nlohmann::json Number(float low, float high) {
            return {{"type", "number"}, {"minimum", low}, {"maximum", high}};
        }
        nlohmann::json Vector(std::size_t count, float low = -1000000, float high = 1000000) {
            return {{"type", "array"}, {"minItems", count}, {"maxItems", count}, {"items", Number(low, high)}};
        }
        nlohmann::json Object(nlohmann::json properties, nlohmann::json required = nlohmann::json::array()) {
            return {{"type", "object"}, {"properties", std::move(properties)}, {"required", std::move(required)}, {"additionalProperties", false}};
        }
    } // namespace

    nlohmann::json EditorMcp::NativeTools() {
        auto tools     = nlohmann::json::array();
        const auto add = [&](const char *name, const char *description, bool read, nlohmann::json properties = nlohmann::json::object(), nlohmann::json required = nlohmann::json::array()) {
            tools.push_back({{"name", name}, {"description", description}, {"inputSchema", Object(std::move(properties), std::move(required))}, {"outputSchema", {{"type", "object"}}},
                {"annotations", {{"readOnlyHint", read}, {"destructiveHint", !read}, {"idempotentHint", read}, {"openWorldHint", false}}}});
        };
        const auto ids     = nlohmann::json {{"type", "array"}, {"minItems", 1}, {"maxItems", 5000}, {"items", Text()}};
        const auto boolean = nlohmann::json {{"type", "boolean"}};
        const auto physics = nlohmann::json {{"type", "string"}, {"enum", {"static", "rigid", "none"}}};
        const auto tuning =
            Object({{"scale", Number(Shared::Game::VfxTuning::kMinScale, Shared::Game::VfxTuning::kMaxScale)}, {"count", Number(Shared::Game::VfxTuning::kMinScale, Shared::Game::VfxTuning::kMaxCountScale)}, {"speed", Number(0, Shared::Game::VfxTuning::kMaxSpeedScale)},
                       {"time", Number(Shared::Game::VfxTuning::kMinScale, Shared::Game::VfxTuning::kMaxTimeScale)}, {"strength", Number(-1, 1)}, {"pulse", Number(0, Shared::Game::VfxTuning::kMaxPulsePeriod)}, {"prime", boolean}},
                {"scale", "count", "speed", "time", "strength", "pulse", "prime"});
        add("wb_get_state", "Read editor readiness, revision, counts and history availability.", true);
        add("wb_editor_open", "Open the offline editor viewport.", false);
        add("wb_editor_close", "Close the viewport while retaining the offline session and local MCP listener.", false);
        for (auto &tool : MapTravelTools()) {
            tools.push_back(std::move(tool));
        }
        add("wb_levels_list", "List supported levels, their required DLC and local DLC availability. Availability does not mean the level has finished loading.", true);
        add("wb_level_load", "Load another offline level; requires discardChanges=true. Invalidates object IDs and pending work.", false, {{"level", Text(128)}, {"discardChanges", boolean}}, {"level", "discardChanges"});
        add("wb_objects_query", "Query authored props, effects, lights and adopted level objects. Search matches names, paths and groups. Returns an operation; poll wb_operation_get.", true,
            {{"query", Text()}, {"kind", {{"type", "string"}, {"enum", {"all", "prop", "effect", "light", "world"}}}}, {"offset", {{"type", "integer"}, {"minimum", 0}}}, {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 500}}}});
        add("wb_object_get", "Read an object using its opaque ID. Returns an operation.", true, {{"id", Text()}}, {"id"});
        // Every field a door, smart object or item takes; each kind reads its own and refuses what it does not know the value of.
        const auto settings = Object({{"name", Text(64)}, {"locked", boolean}, {"lock", {{"type", "string"}, {"enum", {"none", "bolt", "boltAndKey", "key"}}}}, {"lockpickable", boolean}, {"lockDifficulty", Number(0, 1)}, {"keyItem", Text(36)},
            {"amount", {{"type", "integer"}, {"minimum", 1}, {"maximum", 10000}}}, {"quality", {{"type", "integer"}, {"minimum", 0}, {"maximum", 255}}}, {"health", Number(0, 1)}, {"condition", Number(0, 1)}});
        add("wb_props_create",
            "Place a catalog prop at an explicit pose, or a door, smart object or item by the library key wb_assets_search lists for it: `door:<leaf path or stem>`, `smart:<blueprint>`, `item:<class GUID or name>`. Those take `settings` -- a door's name, locked, lock "
            "(none, bolt, boltAndKey, key), lockpickable, lockDifficulty and keyItem; a smart object's name; an item's name, amount, quality, health and condition -- and no scale or physics: the server builds the real one from the export. Returns its opaque ID through an operation.",
            false, {{"model", Text(1024)}, {"position", Vector(3)}, {"rotation", Vector(4, -1, 1)}, {"scale", Vector(3, 0.01F, 100)}, {"physics", physics}, {"settings", settings}, {"group", Text()}}, {"model", "position"});
        add("wb_object_settings_update", "Replace a placed door's, smart object's or item's settings, checked as the server will load them, as one undo step. Omitted fields take their defaults.", false, {{"id", Text()}, {"settings", settings}}, {"id", "settings"});
        add("wb_effects_create", "Place a catalog particle effect.", false, {{"effect", Text(1024)}, {"position", Vector(3)}, {"rotation", Vector(4, -1, 1)}, {"tuning", tuning}, {"group", Text()}}, {"effect", "position"});
        add("wb_lights_create", "Place a standalone point light. Color is RGB from 0 to 1, intensity is a diffuse multiplier, radius is metres, and castShadows defaults to false.", false,
            {{"position", Vector(3, -16384, 16384)}, {"color", Vector(3, 0, 1)}, {"intensity", Number(0, 100)}, {"radius", Number(0.1F, 100)}, {"castShadows", boolean}, {"group", Text()}}, {"position"});
        add("wb_light_update", "Replace color, intensity and radius; optional castShadows preserves its current value when omitted. Use wb_objects_transform for position.", false,
            {{"id", Text()}, {"color", Vector(3, 0, 1)}, {"intensity", Number(0, 100)}, {"radius", Number(0.1F, 100)}, {"castShadows", boolean}}, {"id", "color", "intensity", "radius"});
        add("wb_effect_update", "Replace an effect's complete tuning.", false, {{"id", Text()}, {"tuning", tuning}}, {"id", "tuning"});
        const auto transform = Object({{"id", Text()}, {"position", Vector(3)}, {"rotation", Vector(4, -1, 1)}, {"scale", Vector(3, 0.01F, 100)}}, {"id"});
        add("wb_objects_transform", "Transform explicit objects. Prevalidates all IDs and poses; native failures are reported per operation.", false, {{"objects", {{"type", "array"}, {"minItems", 1}, {"maxItems", 256}, {"items", transform}}}}, {"objects"});
        for (const char *name : {"wb_objects_duplicate", "wb_objects_delete", "wb_objects_drop_to_ground"}) {
            add(name, "Apply the named editor action to explicit object IDs. Native work completes asynchronously.", false, {{"ids", ids}}, {"ids"});
        }
        add("wb_objects_convert", "Replace adopted level objects with editable static props of the same catalog mesh: each original is hidden and a prop placed at its pose, as one undo step. Fails when any object has no catalog mesh.", false, {{"ids", ids}}, {"ids"});
        add("wb_prop_physics_set", "Change a prop's physics and rebuild its native entity.", false, {{"id", Text()}, {"physics", physics}}, {"id", "physics"});
        add("wb_prop_collision_rebuild", "Rebuild a prop's collision from its current scale.", false, {{"id", Text()}}, {"id"});
        add("wb_selection_get", "Read native object, authored area and patrol selections.", true);
        add("wb_selection_set", "Replace all selection with explicit IDs. Omitted collections and patrol are cleared.", false,
            {{"ids", {{"type", "array"}, {"maxItems", 5000}, {"items", Text()}}}, {"areas", {{"type", "array"}, {"maxItems", 256}, {"items", Text()}}}, {"patrol", Text()}});
        add("wb_groups_update", "Set the group of authored props, effects or lights.", false, {{"ids", ids}, {"group", Text()}}, {"ids", "group"});
        const auto filter     = Object({{"kinds", {{"type", "array"}, {"minItems", 1}, {"maxItems", 6}, {"items", {{"type", "string"}, {"enum", {"prop", "effect", "light", "brush", "vegetation", "entity"}}}}}}, {"text", Text()}, {"mesh", Text(1024)}, {"className", Text(128)},
                {"layer", Text(128)}, {"group", Text()}, {"physics", physics}, {"includeHidden", boolean}});
        const auto selectMode = nlohmann::json {{"type", "string"}, {"enum", {"replace", "add", "remove"}}};
        const auto ndcPoint   = Vector(2, -1, 1);
        add("wb_select_similar", "Select objects with the same mesh as an object within a radius; an entity without a mesh matches by class. The filter's layer, group, text, physics and includeHidden still apply. Mode replace, add or remove; up to 5000 selected.", false,
            {{"id", Text()}, {"radius", Number(1, 250)}, {"filter", filter}, {"mode", selectMode}, {"visibleOnly", boolean}}, {"id"});
        add("wb_select_in_radius", "Select objects inside a sphere that pass the filter, adopting level objects. Mode replace, add or remove; up to 5000 selected.", false,
            {{"center", Vector(3)}, {"radius", Number(0.1F, 250)}, {"filter", filter}, {"mode", selectMode}, {"visibleOnly", boolean}}, {"center", "radius"});
        add("wb_select_brush",
            "One dab of the editor's selection brush at a screen point in normalized device coordinates through the editor camera: what a click there would pick and the first plant whose box the ray enters before the surface -- a tree by its crown -- wherever their pivots "
            "stand, and every object whose position is within radius metres (3 by default) of the surface there. All must pass the filter. Mode replace, add or remove.",
            false, {{"aim", ndcPoint}, {"radius", Number(0.25F, 50)}, {"filter", filter}, {"mode", selectMode}, {"visibleOnly", boolean}}, {"aim"});
        add("wb_select_in_rect", "Select objects whose position projects inside a screen rectangle in normalized device coordinates (-1 to 1, +y up) through the editor camera, within reach metres.", false,
            {{"min", ndcPoint}, {"max", ndcPoint}, {"reach", Number(1, 250)}, {"filter", filter}, {"mode", selectMode}, {"visibleOnly", boolean}}, {"min", "max"});
        add("wb_select_in_polygon", "Select objects whose position projects inside a closed screen outline in normalized device coordinates, as the lasso does.", false,
            {{"points", {{"type", "array"}, {"minItems", 3}, {"maxItems", 256}, {"items", ndcPoint}}}, {"reach", Number(1, 250)}, {"filter", filter}, {"mode", selectMode}, {"visibleOnly", boolean}}, {"points"});
        add("wb_select_in_area", "Select objects inside an area shape, given as an area definition such as wb_area_get returns (type, position, rotation, min/max, points/height or radius).", false,
            {{"area", {{"type", "object"}}}, {"filter", filter}, {"mode", selectMode}, {"visibleOnly", boolean}}, {"area"});
        add("wb_layers_get", "Read editor layer definitions and custom object membership with opaque object IDs. Default members are implicit.", true);
        add("wb_layers_update", "Create, rename, activate, hide, lock, delete or assign an editor layer. Hidden or locked objects cannot be edited. Deletion requires confirm=true and returns members to Default.", false,
            {{"action", {{"type", "string"}, {"enum", {"create", "rename", "activate", "visibility", "lock", "delete", "assign"}}}}, {"layerId", Text(128)}, {"name", Text(128)}, {"visible", boolean}, {"locked", boolean}, {"confirm", boolean}, {"ids", ids}}, {"action"});
        add("wb_history_get", "Read the shared editor undo and redo labels.", true);
        add("wb_history_undo", "Undo the latest shared editor history entry.", false);
        add("wb_history_redo", "Redo the latest shared editor history entry.", false);
        add("wb_batch_apply", "Run a bounded ordered batch of native tools. Each step reports its result; the batch is not atomic and stops at the first failure.", false,
            {{"steps", {{"type", "array"}, {"minItems", 1}, {"maxItems", 64}, {"items", Object({{"tool", Text(128)}, {"arguments", {{"type", "object"}}}}, {"tool", "arguments"})}}}}, {"steps"});
        add("wb_level_objects_query", "Browse nearby native level objects; returns generation-scoped row IDs. Does not adopt objects.", true, {{"origin", Vector(3)}, {"radius", Number(1, 1000)}, {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 1000}}}});
        add("wb_level_object_get", "Read a browsed row or adopted native object.", true, {{"id", Text()}}, {"id"});
        add("wb_level_object_update", "Adopt a browsed object and move, scale, rotate, hide or swap it with editor history. `model` stands an asset-library mesh in its place, collision included; an empty string takes the swap back.", false,
            {{"id", Text()}, {"position", Vector(3)}, {"rotation", Vector(4, -1, 1)}, {"scale", Vector(3, 0.01F, 100)}, {"hidden", boolean}, {"model", {{"type", "string"}, {"maxLength", 260}}}}, {"id"});
        add("wb_level_edits_restore", "Restore explicit adopted level objects or all edits. Restoring releases their IDs.", false, {{"ids", ids}, {"all", boolean}});
        add("wb_camera_get", "Read the editor camera pose and lens.", true);
        add("wb_camera_set", "Set camera position, angles, movement speed or field of view.", false, {{"position", Vector(3)}, {"yaw", Number(-36000, 36000)}, {"pitch", Number(-89, 89)}, {"fov", Number(20, 140)}, {"speed", Number(0.05F, 400)}});
        add("wb_camera_frame", "Frame a world position or current native selection.", false, {{"position", Vector(3)}, {"distance", Number(0.1F, 10000)}});
        add("wb_raycast", "Cast a bounded ray using the editor's native picking system.", true, {{"origin", Vector(3)}, {"direction", Vector(3, -1, 1)}, {"distance", Number(0.01F, 5000)}}, {"origin", "direction"});
        add("wb_pick_at",
            "What a viewport click at a screen point in normalized device coordinates would select, without selecting it. A solid object is found where the ray strikes its collision; an object without collision by its oriented box in front of that surface. `kind` is prop, "
            "effect, light, level, terrain or none; `by` is collision or bounds; `id` is set for a prop, effect, light or an adopted level object.",
            true, {{"aim", ndcPoint}}, {"aim"});
        add("wb_pose_anchors_query",
            "List where the game poses a body within `radius` metres (40 by default) of `center` (the editor camera by default): each part of a placed smart object that carries a pose helper, and the level's own entities of those classes. Each has `position`, `yaw` in "
            "degrees as wb_camera_get reads it, `label` (the helper class, or a level entity's name), `class`, `part`, and `objectId`, the placed smart object's ID or null for the level's own. Pass a position and yaw to wb_figure_place to stand the scale figure there.",
            true, {{"center", Vector(3)}, {"radius", Number(1, 150)}});
        add("wb_ground_probe", "Probe terrain and native collision below explicit positions.", true, {{"points", {{"type", "array"}, {"minItems", 1}, {"maxItems", 256}, {"items", Vector(3)}}}}, {"points"});
        add("wb_project_points", "Project world points into normalized screen coordinates through the active native camera.", true, {{"points", {{"type", "array"}, {"minItems", 1}, {"maxItems", 256}, {"items", Vector(3)}}}}, {"points"});
        for (const char *name : {"wb_operation_get", "wb_operation_wait"}) {
            add(name, "Read operation state without blocking the editor. Pending results include pollAfterMs.", true, {{"operationId", Text()}}, {"operationId"});
        }
        add("wb_operation_cancel", "Cancel a queued operation before execution. Already running work cannot be undone by cancellation.", false, {{"operationId", Text()}}, {"operationId"});
        add("wb_logs_read", "Read the bounded MCP operation audit log.", true);
        add("wb_capture_viewport", "Request a PNG frame, or poll captureId. Captures have independent frame metadata; document revision is not proof a change was rendered.", true, {{"captureId", Text()}, {"includeOverlays", boolean}});
        return tools;
    }

    nlohmann::json EditorMcp::Native(MapEditor &editor, const std::string &name, const nlohmann::json &arguments) {
        const auto snapshot = editor.ReadSnapshot();
        if (name == "wb_map_get") {
            return snapshot->mapTravel;
        }
        const auto id       = [&](SelectionKind kind, std::uint64_t value) {
            return _instance + "/" + std::to_string(_generation.load()) + "/" + (kind == SelectionKind::Placed ? "prop" : kind == SelectionKind::Light ? "light" : kind == SelectionKind::Effect ? "effect" : "world") + "/" + std::to_string(value);
        };
        if (name == "wb_selection_get") {
            auto ids = nlohmann::json::array();
            if (snapshot->selection.kind != SelectionKind::None) {
                ids.push_back(id(snapshot->selection.kind, snapshot->selection.id));
            }
            for (const auto &ref : snapshot->extraSelection) {
                ids.push_back(id(ref.kind, ref.id));
            }
            return {{"ids", ids}, {"count", ids.size()}, {"areas", editor._areas.SelectedIds()}, {"patrols", editor._patrols.SelectedIds()}};
        }
        if (name == "wb_history_get") {
            return {{"undo", snapshot->undoLabel}, {"redo", snapshot->redoLabel}};
        }
        if (name == "wb_camera_get") {
            return {{"position", Vec(snapshot->camera.position)}, {"yaw", snapshot->camera.yawDegrees}, {"pitch", snapshot->camera.pitchDegrees}, {"fov", snapshot->camera.fovDegrees}, {"requestedFov", snapshot->cameraRequestedFov}, {"aspectRatio", snapshot->camera.aspectRatio},
                {"speed", snapshot->cameraSpeed}};
        }
        if (name == "wb_capture_viewport") {
            if (arguments.contains("captureId")) {
                const auto text       = arguments.at("captureId").get<std::string>();
                std::uint64_t capture = 0;
                const auto parsed     = std::from_chars(text.data(), text.data() + text.size(), capture);
                if (parsed.ec != std::errc {} || parsed.ptr != text.data() + text.size()) {
                    throw std::runtime_error("Invalid capture ID");
                }
                auto result         = EditorCapture::Get().GetResult(capture);
                result["captureId"] = text;
                result["status"]    = result.at("state");
                result.erase("state");
                result.erase("id");
                if (result["status"] == "queued" || result["status"] == "capturing" || result["status"] == "encoding") {
                    result["pollAfterMs"] = 50;
                }
                return result;
            }
            const auto capture = EditorCapture::Get().Request(arguments.value("includeOverlays", false));
            if (!capture) {
                throw std::runtime_error("A viewport capture is already in flight");
            }
            return {{"captureId", std::to_string(capture)}, {"status", "queued"}, {"pollAfterMs", 50}};
        }
        if (name == "wb_selection_set") {
            if (arguments.contains("areas")) {
                for (const auto &area : arguments["areas"]) {
                    if (!editor._areas.Find(area.get<std::string>())) {
                        throw std::runtime_error("Unknown area ID");
                    }
                }
            }
            if (arguments.contains("patrol") && arguments["patrol"] != "" && !editor._patrols.Find(arguments["patrol"].get<std::string>())) {
                throw std::runtime_error("Unknown patrol ID");
            }
        }
        Operation operation;
        operation.id              = _instance + "/" + std::to_string(++_nextOperation);
        operation.name            = name;
        operation.job             = std::make_shared<McpJob>();
        operation.job->name       = name;
        operation.job->arguments  = arguments;
        operation.job->generation = _generation.load(std::memory_order_acquire);
        MapEditor::Command command;
        command.kind = MapEditor::CommandKind::Mcp;
        command.mcp  = operation.job;
        editor.Queue(std::move(command));
        operation.serial  = editor._nextCommand.load();
        const auto result = OperationResult(operation);
        _operations.emplace(operation.id, std::move(operation));
        return result;
    }

    void EditorMcp::ExecuteGame(MapEditor &editor, const std::shared_ptr<McpJob> &job) {
        if (!job) {
            return;
        }
        auto expected = McpJob::State::Queued;
        if (!job->state.compare_exchange_strong(expected, McpJob::State::Running)) {
            return;
        }
        try {
            if (!_available.load(std::memory_order_acquire) || job->generation != _generation.load(std::memory_order_acquire) || !Core::gApplication || !Core::gApplication->IsOfflineBuilderReady()) {
                throw std::runtime_error("Offline world changed before execution");
            }
            const auto &args         = job->arguments;
            const auto &name         = job->name;
            if (editor._mapTravel.Active() && name != "wb_map_cancel" && name != "wb_map_travel") {
                throw std::runtime_error("Map travel took ownership before this command executed");
            }
            const std::string prefix = _instance + "/" + std::to_string(job->generation) + "/";
            const auto id            = [&](SelectionKind kind, std::uint64_t value) {
                return prefix + (kind == SelectionKind::Placed ? "prop/" : kind == SelectionKind::Light ? "light/" : kind == SelectionKind::Effect ? "effect/" : "world/") + std::to_string(value);
            };
            const auto ref = [&](const nlohmann::json &value) -> SelectionRef {
                const std::string text = value.get<std::string>();
                if (!text.starts_with(prefix)) {
                    throw std::runtime_error("Object ID belongs to another editor instance or level");
                }
                const auto split = text.find('/', prefix.size());
                if (split == std::string::npos) {
                    throw std::runtime_error("Malformed object ID");
                }
                const auto kind      = text.substr(prefix.size(), split - prefix.size());
                std::uint64_t number = 0;
                const auto parsed    = std::from_chars(text.data() + split + 1, text.data() + text.size(), number);
                if (!number || parsed.ec != std::errc {} || parsed.ptr != text.data() + text.size()) {
                    throw std::runtime_error("Malformed object ID");
                }
                SelectionRef result {kind == "prop" ? SelectionKind::Placed : kind == "light" ? SelectionKind::Light : kind == "effect" ? SelectionKind::Effect : kind == "world" ? SelectionKind::World : SelectionKind::None, number};
                if (result.kind == SelectionKind::None || !editor.DescribeRef(result)) {
                    throw std::runtime_error("Object no longer exists");
                }
                if (name != "wb_object_get" && name != "wb_level_object_get" && !editor.CanEdit(result)) {
                    throw std::runtime_error("Object layer is hidden or locked.");
                }
                return result;
            };
            const auto describe = [&](const SelectionRef &item) -> nlohmann::json {
                const auto pose = editor.DescribeRef(item);
                if (!pose) {
                    throw std::runtime_error("Object no longer exists");
                }
                nlohmann::json result {{"id", id(item.kind, item.id)}, {"position", Vec(pose->position)}, {"rotation", Quat(pose->rotation)}, {"scale", Vec(pose->scale)}};
                result["layerId"]  = editor._layers.LayerOf(MapEditor::LayerKey(item));
                result["editable"] = editor.CanEdit(item);
                if (pose->hasBounds) {
                    result["bounds"] = {{"min", Vec(pose->bounds.min)}, {"max", Vec(pose->bounds.max)}};
                }
                if (item.kind == SelectionKind::Placed) {
                    const auto &object = *editor._scene.Find(item.id);
                    result.update({{"kind", PlacedKindName(object.kind)}, {"model", object.model}, {"name", object.name}, {"physics", KCDC::Common::Spawn::PropPhysicsToString(object.physics)}, {"group", object.group}, {"collisionDirty", object.collisionDirty},
                        {"collisionRebuildFailed", object.collisionRebuildFailed}});
                    if (object.kind != PlacedKind::Prop) {
                        result.update({{"key", WorldObjectKey(object.kind, object.model)}, {"settings", object.settings}, {"built", object.entityId != 0}});
                    }
                }
                else if (item.kind == SelectionKind::Light) {
                    const auto &entry = *editor._lights.Find(item.id);
                    result.update({{"kind", "light"}, {"color", {entry.tuning.color.x, entry.tuning.color.y, entry.tuning.color.z}}, {"intensity", entry.tuning.intensity}, {"radius", entry.tuning.radius}, {"castShadows", entry.tuning.castShadows}, {"playing", entry.handle != 0},
                        {"group", entry.group}});
                }
                else if (item.kind == SelectionKind::Effect) {
                    const auto &effect = *editor._effects.Find(item.id);
                    result.update({{"kind", "effect"}, {"effect", effect.name}, {"tuning", Tuning(effect.tuning)}, {"playing", effect.handle != 0}, {"group", effect.group}});
                }
                else {
                    const auto &world = *editor._world.Find(item.id);
                    result.update({{"kind", "world"}, {"name", world.label}, {"detail", world.detail}, {"hidden", world.hidden}, {"loaded", world.loaded}, {"layerOff", world.layerOff}, {"moved", world.moved}, {"model", world.replacement}});
                    Shared::Game::LevelEditDefinition definition;
                    result["portable"] = WorldObjects::ToDefinition(world, definition);
                }
                return result;
            };
            const auto selectExplicit = [&](const std::vector<SelectionRef> &targets) {
                editor.SelectOnly(targets);
            };
            const auto transform = [&](const SelectionRef &target, const SDK::Vec3 &position, const SDK::Quat &rotation, const SDK::Vec3 &scale) {
                const auto before = *editor.DescribeRef(target);
                bool accepted     = false;
                if (target.kind == SelectionKind::Placed) {
                    const auto *object = editor._scene.Find(target.id);
                    if (!SDK::IEntitySystem::GetInstance()->GetEntity(object->entityId)) {
                        throw std::runtime_error("Prop entity is no longer loaded");
                    }
                    accepted = editor._scene.SetTransform(target.id, position, rotation, scale);
                }
                else if (target.kind == SelectionKind::Light) {
                    accepted = editor._lights.SetTransform(target.id, position);
                }
                else if (target.kind == SelectionKind::Effect) {
                    accepted = editor._effects.SetTransform(target.id, position, rotation);
                }
                else {
                    accepted = editor._world.SetTransform(target.id, position, rotation, scale);
                }
                const auto after = editor.DescribeRef(target);
                if (!accepted || !after) {
                    throw std::runtime_error("Object transform failed; earlier changes in this operation remain applied");
                }
                HistoryAction action;
                action.id     = target.id;
                action.kind   = target.kind == SelectionKind::Placed   ? HistoryAction::Kind::Transform
                                : target.kind == SelectionKind::Light  ? HistoryAction::Kind::LightMove
                                : target.kind == SelectionKind::Effect ? HistoryAction::Kind::EffectMove
                                                                       : HistoryAction::Kind::WorldTransform;
                action.before = {before.position, before.rotation, before.scale};
                action.after  = {after->position, after->rotation, after->scale};
                HistoryStep step;
                step.label = "MCP transform";
                step.actions.push_back(std::move(action));
                editor._history.Record(std::move(step), &editor._layers);
                if (target.kind == SelectionKind::Effect && editor._effects.Find(target.id)->handle == 0) {
                    throw std::runtime_error("Effect pose changed but its emitter could not restart; the change is in undo history");
                }
            };
            const auto hide = [&](std::uint64_t objectId, bool hidden) {
                const bool before = editor._world.Find(objectId)->hidden;
                if (!editor._world.SetHidden(objectId, hidden)) {
                    throw std::runtime_error("Level object visibility could not be updated");
                }
                if (before != hidden) {
                    HistoryAction action;
                    action.id           = objectId;
                    action.kind         = HistoryAction::Kind::WorldHide;
                    action.hiddenBefore = before;
                    action.hiddenAfter  = hidden;
                    HistoryStep step;
                    step.label = "MCP level visibility";
                    step.actions.push_back(action);
                    editor._history.Record(std::move(step), &editor._layers);
                }
            };
            const auto replace = [&](std::uint64_t objectId, const std::string &model) {
                const std::string before = editor._world.Find(objectId)->replacement;
                if (!editor._world.SetReplacement(objectId, model)) {
                    throw std::runtime_error("model must be a mesh path from the asset library, or empty to take the swap back");
                }
                if (before != model) {
                    HistoryAction action;
                    action.id                = objectId;
                    action.kind              = HistoryAction::Kind::WorldReplace;
                    action.replacementBefore = before;
                    action.replacementAfter  = model;
                    HistoryStep step;
                    step.label = "MCP level swap";
                    step.actions.push_back(action);
                    editor._history.Record(std::move(step), &editor._layers);
                }
            };
            auto result = nlohmann::json::object();
            if (name == "wb_map_open" || name == "wb_map_travel" || name == "wb_map_cancel") {
                std::string error;
                if (name == "wb_map_cancel") {
                    if (!editor._mapTravel.Cancel()) {
                        throw std::runtime_error("Placement has already started and cannot be cancelled");
                    }
                }
                else if (name == "wb_map_open") {
                    if (!editor._mapTravel.Open(editor._camera, error)) {
                        throw std::runtime_error(error);
                    }
                }
                else {
                    const auto &point = args.at("position");
                    if (!editor._mapTravel.Travel(editor._camera, {point[0].get<float>(), point[1].get<float>(), 0}, error)) {
                        throw std::runtime_error(error);
                    }
                }
                result = {{"accepted", true}, {"travel", editor._mapTravel.State()}, {"poll", "wb_map_get"}};
            }
            else if (name == "wb_layers_get" || name == "wb_layers_update") {
                if (name == "wb_layers_update") {
                    auto action = args;
                    if (action.contains("ids")) {
                        action["keys"] = nlohmann::json::array();
                        for (const auto &value : action["ids"]) {
                            action["keys"].push_back(MapEditor::LayerKey(ref(value)));
                        }
                        action.erase("ids");
                    }
                    std::string error;
                    if (!editor.ApplyLayerAction(action, error)) {
                        throw std::runtime_error(error);
                    }
                }
                result            = editor._layers.ToJson(false);
                result["members"] = nlohmann::json::array();
                const auto append = [&](SelectionKind kind, std::uint64_t number) {
                    const auto layer = editor._layers.LayerOf(MapEditor::LayerKey({kind, number}));
                    if (layer != "default") {
                        result["members"].push_back({{"id", id(kind, number)}, {"layerId", layer}});
                    }
                };
                for (const auto &object : editor._scene.Objects()) {
                    append(SelectionKind::Placed, object.id);
                }
                for (const auto &effect : editor._effects.Effects()) {
                    append(SelectionKind::Effect, effect.id);
                }
                for (const auto &entry : editor._lights.Lights()) {
                    append(SelectionKind::Light, entry.id);
                }
                for (const auto &world : editor._world.Edits()) {
                    append(SelectionKind::World, world.id);
                }
            }
            else if (name == "wb_editor_open" || name == "wb_editor_close") {
                editor.SetOpen(name == "wb_editor_open");
                result["open"] = editor.IsOpen();
            }
            else if (name == "wb_level_load") {
                if (!args.at("discardChanges").get<bool>()) {
                    throw std::runtime_error("Level load requires explicit discardChanges=true");
                }
                const auto level = args.at("level").get<std::string>();
                std::string error;
                if (!Core::gApplication->RequestOfflineBuilderLevel(level, error)) {
                    throw std::runtime_error(error);
                }
                result = {{"transitionAccepted", true}, {"level", level}, {"loadingComplete", false}, {"reconnectRequired", true}, {"scope", "Old project discarded; the listener resumes after the offline level loads. Read wb_get_state after reconnecting to confirm readiness."}};
            }
            else if (name == "wb_levels_list") {
                result["levels"] = nlohmann::json::array();
                for (const auto &level : Shared::Game::kPlayableLevels) {
                    const auto *dlc              = level.dlc.empty() ? nullptr : Shared::Game::FindDLCByName(level.dlc);
                    const std::uint16_t required = dlc ? Shared::Game::DLCBit(dlc->id) : 0;
                    const std::uint16_t missing  = required ? Core::MissingDLC(required) : 0;
                    result["levels"].push_back(
                        {{"name", level.name}, {"displayName", level.displayName}, {"dlc", level.dlc}, {"dlcDisplayName", dlc ? dlc->displayName : std::string_view {}}, {"requiredDlcMask", required}, {"missingDlcMask", missing}, {"available", missing == 0}});
                }
            }
            else if (name == "wb_objects_query") {
                const auto kind   = args.value("kind", std::string("all"));
                const auto query  = args.value("query", std::string {});
                const auto offset = args.value("offset", std::size_t {});
                const auto limit  = args.value("limit", std::size_t(100));
                std::size_t total = 0;
                result["items"]   = nlohmann::json::array();
                const auto append = [&](SelectionKind type, std::uint64_t number, const std::string &label, const std::string &detail, const std::string &group) {
                    if (!query.empty() && label.find(query) == std::string::npos && detail.find(query) == std::string::npos && group.find(query) == std::string::npos) {
                        return;
                    }
                    if (total >= offset && result["items"].size() < limit) {
                        result["items"].push_back(describe({type, number}));
                    }
                    ++total;
                };
                if (kind == "all" || kind == "prop") {
                    for (const auto &object : editor._scene.Objects()) {
                        append(SelectionKind::Placed, object.id, object.name, object.model, object.group);
                    }
                }
                if (kind == "all" || kind == "light") {
                    for (const auto &entry : editor._lights.Lights()) {
                        append(SelectionKind::Light, entry.id, "Point light", "light", entry.group);
                    }
                }
                if (kind == "all" || kind == "effect") {
                    for (const auto &effect : editor._effects.Effects()) {
                        append(SelectionKind::Effect, effect.id, effect.label, effect.name, effect.group);
                    }
                }
                if (kind == "all" || kind == "world") {
                    for (const auto &world : editor._world.Edits()) {
                        append(SelectionKind::World, world.id, world.label, world.detail, {});
                    }
                }
                result["total"]      = total;
                const auto end       = std::min(offset, total) + result["items"].size();
                result["nextOffset"] = end < total ? nlohmann::json(end) : nlohmann::json(nullptr);
            }
            else if (name == "wb_object_get") {
                result = describe(ref(args.at("id")));
            }
            else if (name == "wb_lights_create" || name == "wb_light_update") {
                Shared::Game::LightTuning tuning;
                auto settings = args;
                SelectionRef updateTarget {SelectionKind::None, 0};
                if (name == "wb_lights_create") {
                    if (!settings.contains("color")) {
                        settings["color"] = {tuning.color.x, tuning.color.y, tuning.color.z};
                    }
                    if (!settings.contains("intensity")) {
                        settings["intensity"] = tuning.intensity;
                    }
                    if (!settings.contains("radius")) {
                        settings["radius"] = tuning.radius;
                    }
                }
                else {
                    updateTarget = ref(args.at("id"));
                    if (updateTarget.kind != SelectionKind::Light) {
                        throw std::runtime_error("Expected a light ID");
                    }
                    if (!settings.contains("castShadows")) {
                        settings["castShadows"] = editor._lights.Find(updateTarget.id)->tuning.castShadows;
                    }
                }
                if (!Shared::WorldBuilder::ReadLightTuning(settings, tuning)) {
                    throw std::runtime_error("Invalid light settings");
                }
                HistoryStep step;
                std::uint64_t number = 0;
                if (name == "wb_lights_create") {
                    const auto *layer = editor._layers.Find("default");
                    if (!layer->visible || layer->locked) {
                        throw std::runtime_error("Default layer is hidden or locked.");
                    }
                    number = editor._lights.Place(ReadVec(args.at("position")), tuning);
                    if (!number) {
                        throw std::runtime_error("Native light placement failed");
                    }
                    editor._lights.SetGroup(number, args.value("group", std::string {}));
                    step.label = "MCP place light";
                    editor.AppendPlaced(step, SelectionKind::Light, number);
                }
                else {
                    number = updateTarget.id;
                    HistoryAction action;
                    action.kind              = HistoryAction::Kind::LightTuning;
                    action.id                = number;
                    action.lightTuningBefore = editor._lights.Find(number)->tuning;
                    action.lightTuningAfter  = tuning;
                    if (!editor._lights.SetTuning(number, tuning)) {
                        throw std::runtime_error("Native light update failed");
                    }
                    step.label = "MCP edit light";
                    if (!(action.lightTuningBefore == tuning)) {
                        step.actions.push_back(action);
                    }
                }
                editor._history.Record(std::move(step), &editor._layers);
                result = describe({SelectionKind::Light, number});
            }
            else if (name == "wb_props_create" || name == "wb_effects_create") {
                const auto *defaultLayer = editor._layers.Find("default");
                if (!defaultLayer->visible || defaultLayer->locked) {
                    throw std::runtime_error("Default layer is hidden or locked.");
                }
                const auto position   = ReadVec(args.at("position"));
                const auto rotation   = ReadQuat(args.value("rotation", nlohmann::json {0, 0, 0, 1}));
                std::uint64_t created = 0;
                const auto kind       = name == "wb_props_create" ? SelectionKind::Placed : SelectionKind::Effect;
                if (kind == SelectionKind::Placed) {
                    std::string model;
                    const PlacedKind placedKind = SplitWorldObjectKey(args.at("model").get<std::string>(), model);
                    if (placedKind != PlacedKind::Prop) {
                        if (args.contains("scale") || args.contains("physics")) {
                            throw std::runtime_error("Doors, smart objects and items are built at their own size, with their own physics");
                        }
                        std::string error;
                        created = editor._scene.PlaceWorldObject(placedKind, model, args.value("settings", nlohmann::json::object()), position, rotation, error);
                        if (!created) {
                            throw std::runtime_error(error);
                        }
                    }
                    else {
                        if (args.contains("settings")) {
                            throw std::runtime_error("A prop takes no settings");
                        }
                        if (!Shared::Game::FindPropModel(model)) {
                            throw std::runtime_error("Model is not in the prop catalog");
                        }
                        created = editor._scene.Place(model, position, rotation, ReadVec(args.value("scale", nlohmann::json {1, 1, 1})), Physics(args.value("physics", std::string("static"))));
                    }
                    if (created) {
                        editor._scene.SetGroup(created, args.value("group", std::string {}));
                    }
                }
                else {
                    const auto index = Shared::Game::FindVfxEffectIndex(args.at("effect").get<std::string>());
                    if (!Shared::Game::VfxEffectAt(index)) {
                        throw std::runtime_error("Effect is not in the effect catalog");
                    }
                    auto tuning = args.contains("tuning") ? Shared::WorldBuilder::ReadTuning(args["tuning"]) : Shared::Game::VfxTuning {};
                    if (!tuning.IsSane()) {
                        throw std::runtime_error("Invalid effect tuning");
                    }
                    created = editor._effects.Place(index, position, rotation, tuning);
                    if (created) {
                        editor._effects.SetGroup(created, args.value("group", std::string {}));
                    }
                }
                if (!created) {
                    throw std::runtime_error("Native placement failed");
                }
                HistoryStep step;
                step.label = "MCP place";
                editor.AppendPlaced(step, kind, created);
                editor._history.Record(std::move(step), &editor._layers);
                result = describe({kind, created});
            }
            else if (name == "wb_objects_transform") {
                std::vector<MapEditor::Command> commands;
                std::unordered_set<std::string> seen;
                for (const auto &entry : args.at("objects")) {
                    if (!seen.insert(entry.at("id").get<std::string>()).second) {
                        throw std::runtime_error("Transform IDs must be unique");
                    }
                    const auto target = ref(entry.at("id"));
                    const auto pose   = *editor.DescribeRef(target);
                    MapEditor::Command command;
                    command.kind     = MapEditor::CommandKind::Transform;
                    command.id       = target.id;
                    command.target   = target.kind;
                    command.position = entry.contains("position") ? ReadVec(entry["position"]) : pose.position;
                    command.rotation = entry.contains("rotation") ? ReadQuat(entry["rotation"]) : pose.rotation;
                    command.scale    = entry.contains("scale") ? ReadVec(entry["scale"]) : pose.scale;
                    if (target.kind == SelectionKind::Light && entry.contains("position") && !Shared::WorldBuilder::LightPosition(entry)) {
                        throw std::runtime_error("Light position must remain within world bounds");
                    }
                    if (target.kind == SelectionKind::Light && (entry.contains("rotation") || entry.contains("scale"))) {
                        throw std::runtime_error("Point lights support position only; use wb_light_update for radius");
                    }
                    if (target.kind == SelectionKind::Effect && entry.contains("scale")) {
                        throw std::runtime_error("Effect size is controlled through tuning.scale");
                    }
                    if (target.kind == SelectionKind::Placed && entry.contains("scale") && editor._scene.Find(target.id)->kind != PlacedKind::Prop) {
                        throw std::runtime_error("Doors, smart objects and items are built at their own size");
                    }
                    commands.push_back(command);
                }
                result["items"] = nlohmann::json::array();
                for (const auto &command : commands) {
                    transform({command.target, command.id}, command.position, command.rotation, command.scale);
                    result["items"].push_back(describe({command.target, command.id}));
                }
            }
            else if (name == "wb_effect_update" || name == "wb_prop_physics_set" || name == "wb_prop_collision_rebuild") {
                const auto target = ref(args.at("id"));
                HistoryAction action;
                action.id      = target.id;
                bool succeeded = false;
                bool changed   = false;
                if (name == "wb_effect_update") {
                    if (target.kind != SelectionKind::Effect) {
                        throw std::runtime_error("Expected an effect ID");
                    }
                    const auto tuning = Shared::WorldBuilder::ReadTuning(args.at("tuning"));
                    if (!tuning.IsSane()) {
                        throw std::runtime_error("Invalid effect tuning");
                    }
                    action.kind         = HistoryAction::Kind::EffectTuning;
                    action.tuningBefore = editor._effects.Find(target.id)->tuning;
                    succeeded           = editor._effects.SetTuning(target.id, tuning);
                    action.tuningAfter  = editor._effects.Find(target.id)->tuning;
                    changed             = !(action.tuningBefore == action.tuningAfter);
                    succeeded           = succeeded && editor._effects.Find(target.id)->handle != 0;
                }
                else {
                    if (target.kind != SelectionKind::Placed) {
                        throw std::runtime_error("Expected a prop ID");
                    }
                    if (name == "wb_prop_physics_set" && editor._scene.Find(target.id)->kind != PlacedKind::Prop) {
                        throw std::runtime_error("Doors, smart objects and items take the physics the server gives them");
                    }
                    if (name == "wb_prop_collision_rebuild") {
                        succeeded = editor._scene.Rebuild(target.id);
                    }
                    else {
                        action.kind          = HistoryAction::Kind::Physics;
                        action.physicsBefore = editor._scene.Find(target.id)->physics;
                        succeeded            = editor._scene.SetPhysics(target.id, Physics(args.at("physics").get<std::string>()));
                        action.physicsAfter  = editor._scene.Find(target.id)->physics;
                        changed              = action.physicsBefore != action.physicsAfter;
                        succeeded            = succeeded && !editor._scene.Find(target.id)->collisionRebuildFailed;
                    }
                }
                if (changed) {
                    HistoryStep step;
                    step.label = name == "wb_effect_update" ? "MCP effect tuning" : "MCP physics";
                    step.actions.push_back(action);
                    editor._history.Record(std::move(step), &editor._layers);
                }
                if (!succeeded) {
                    throw std::runtime_error("Native rebuild failed; any changed settings remain in undo history and can be retried");
                }
                result = describe(target);
            }
            else if (name == "wb_object_settings_update") {
                const auto target = ref(args.at("id"));
                if (target.kind != SelectionKind::Placed || editor._scene.Find(target.id)->kind == PlacedKind::Prop) {
                    throw std::runtime_error("Expected a placed door, smart object or item");
                }
                HistoryAction action;
                action.kind           = HistoryAction::Kind::Settings;
                action.id             = target.id;
                action.settingsBefore = editor._scene.Find(target.id)->settings;
                std::string error;
                if (!editor._scene.SetSettings(target.id, args.at("settings"), error)) {
                    throw std::runtime_error(error);
                }
                action.settingsAfter = editor._scene.Find(target.id)->settings;
                if (action.settingsAfter != action.settingsBefore) {
                    HistoryStep step;
                    step.label = "MCP settings";
                    step.actions.push_back(std::move(action));
                    editor._history.Record(std::move(step), &editor._layers);
                }
                result = describe(target);
            }
            else if (name == "wb_select_similar" || name == "wb_select_in_radius" || name == "wb_select_brush" || name == "wb_select_in_rect" || name == "wb_select_in_polygon" || name == "wb_select_in_area") {
                SelectionQuery query;
                std::string error;
                if (!SelectionFilterFromJson(args.value("filter", nlohmann::json()), query.filter, error)) {
                    throw std::runtime_error(error);
                }
                const std::string mode = args.value("mode", std::string("replace"));
                query.mode             = mode == "add" ? SelectionMode::Add : mode == "remove" ? SelectionMode::Remove : SelectionMode::Replace;
                query.visibleOnly      = args.value("visibleOnly", false);
                query.region.reach     = args.value("reach", SelectionRegion::kDefaultReach);
                const auto ndc         = [](const nlohmann::json &value) {
                    return glm::vec2(value.at(0).get<float>(), value.at(1).get<float>());
                };
                if (name == "wb_select_similar") {
                    query.similarTo     = ref(args.at("id"));
                    query.region.radius = args.value("radius", 50.0F);
                }
                else if (name == "wb_select_in_radius") {
                    query.region.centre = KCDC::Common::ToGlm(ReadVec(args.at("center")));
                    query.region.radius = args.at("radius").get<float>();
                }
                else if (name == "wb_select_brush") {
                    query.aimNdc        = ndc(args.at("aim"));
                    query.region.radius = args.value("radius", 3.0F);
                }
                else if (name == "wb_select_in_rect") {
                    const glm::vec2 low  = glm::min(ndc(args.at("min")), ndc(args.at("max")));
                    const glm::vec2 high = glm::max(ndc(args.at("min")), ndc(args.at("max")));
                    query.region.kind    = SelectionRegionKind::Screen;
                    query.region.outline = {low, glm::vec2(high.x, low.y), high, glm::vec2(low.x, high.y)};
                }
                else if (name == "wb_select_in_polygon") {
                    query.region.kind = SelectionRegionKind::Screen;
                    for (const auto &point : args.at("points")) {
                        query.region.outline.push_back(ndc(point));
                    }
                }
                else {
                    nlohmann::json definition = args.at("area");
                    definition["id"]          = "selection";
                    definition.erase("collision");
                    Shared::Game::AreaDefinition area;
                    if (!Shared::Game::AreaDefinitionFromJson(definition, area, error)) {
                        throw std::runtime_error("area: " + error);
                    }
                    query.region.kind = SelectionRegionKind::Area;
                    query.region.area = area.shape;
                }
                const SelectionOutcome outcome = editor.RunSelection(query);
                if (!outcome.error.empty()) {
                    throw std::runtime_error(outcome.error);
                }
                result = {{"matched", outcome.matched}, {"added", outcome.added}, {"removed", outcome.removed}, {"total", outcome.total}, {"truncated", outcome.truncated}, {"selection", nlohmann::json::array()}};
                for (const auto &target : editor.SelectionRefs()) {
                    result["selection"].push_back(id(target.kind, target.id));
                }
            }
            else if (name == "wb_objects_duplicate" || name == "wb_objects_convert" || name == "wb_objects_delete" || name == "wb_objects_drop_to_ground" || name == "wb_groups_update" || name == "wb_selection_set") {
                std::vector<SelectionRef> targets;
                std::unordered_set<std::string> seen;
                for (const auto &value : args.value("ids", nlohmann::json::array())) {
                    if (!seen.insert(value.get<std::string>()).second) {
                        throw std::runtime_error("Object IDs must be unique");
                    }
                    targets.push_back(ref(value));
                }
                if (name == "wb_groups_update") {
                    for (const auto &target : targets) {
                        if (target.kind == SelectionKind::World) {
                            throw std::runtime_error("This action supports authored props, effects and lights only");
                        }
                    }
                }
                if (name == "wb_objects_duplicate" || name == "wb_objects_convert") {
                    // Prevalidated, so a refused level object leaves nothing half done.
                    for (const auto &target : targets) {
                        if (target.kind != SelectionKind::World) {
                            if (name == "wb_objects_convert") {
                                throw std::runtime_error("Only adopted level objects can be converted to props");
                            }
                            continue;
                        }
                        const WorldEdit *edit = editor._world.Find(target.id);
                        if (edit == nullptr || MapEditor::CatalogModelOf(*edit).empty()) {
                            throw std::runtime_error("A level object's mesh is not in the asset library; nothing was changed");
                        }
                        if (name == "wb_objects_convert" && edit->hidden) {
                            throw std::runtime_error("A hidden level object cannot be converted; nothing was changed");
                        }
                    }
                }
                if (name == "wb_objects_drop_to_ground") {
                    auto *physics = SDK::CPhysicalWorld::GetInstance();
                    if (!physics) {
                        throw std::runtime_error("Physics is not ready");
                    }
                    std::vector<SDK::Vec3> positions;
                    for (const auto &target : targets) {
                        const auto pose                = *editor.DescribeRef(target);
                        SDK::IPhysicalEntity *collider = nullptr;
                        if (target.kind == SelectionKind::World) {
                            collider = editor._world.Collider(target.id);
                        }
                        else if (target.kind == SelectionKind::Placed) {
                            if (auto *entity = SDK::IEntitySystem::GetInstance()->GetEntity(editor._scene.Find(target.id)->entityId)) {
                                collider = entity->GetPhysics();
                            }
                        }
                        SDK::Vec3 ground {};
                        if (!physics->ResolveGround(pose.position, 3.0F, 200.0F, ground, collider ? &collider : nullptr, collider ? 1 : 0)) {
                            throw std::runtime_error("No ground under an object; nothing was moved");
                        }
                        positions.push_back({pose.position.x, pose.position.y, ground.z + (target.kind != SelectionKind::Light && pose.hasBounds ? pose.position.z - pose.bounds.min.z : 0.0F)});
                    }
                    for (std::size_t index = 0; index < targets.size(); ++index) {
                        const auto pose = *editor.DescribeRef(targets[index]);
                        transform(targets[index], positions[index], pose.rotation, pose.scale);
                    }
                    selectExplicit(targets);
                }
                else {
                    selectExplicit(targets);
                    if (name != "wb_selection_set") {
                        MapEditor::Command command;
                        if (name == "wb_objects_duplicate") {
                            command.kind = MapEditor::CommandKind::DuplicateSelection;
                        }
                        else if (name == "wb_objects_convert") {
                            command.kind = MapEditor::CommandKind::ConvertSelection;
                        }
                        else if (name == "wb_objects_delete") {
                            command.kind = MapEditor::CommandKind::DeleteSelection;
                        }
                        else {
                            command.kind  = MapEditor::CommandKind::GroupSelection;
                            command.group = args.at("group").get<std::string>();
                        }
                        editor.Apply(command);
                        if (name == "wb_objects_duplicate" || name == "wb_objects_convert") {
                            const auto copies           = editor.SelectionRefs();
                            const bool originalSelected = std::any_of(copies.begin(), copies.end(), [&](const SelectionRef &copy) {
                                return std::find(targets.begin(), targets.end(), copy) != targets.end();
                            });
                            if (copies.size() != targets.size() || originalSelected) {
                                throw std::runtime_error(name == "wb_objects_convert" ? "Not every object could be converted; successful conversions remain in undo history" : "Not every object could be duplicated; successful copies remain in undo history");
                            }
                        }
                        else if (name == "wb_objects_delete") {
                            for (const auto &target : targets) {
                                const bool removed = target.kind == SelectionKind::Placed   ? !editor._scene.Find(target.id)
                                                     : target.kind == SelectionKind::Light  ? !editor._lights.Find(target.id)
                                                     : target.kind == SelectionKind::Effect ? !editor._effects.Find(target.id)
                                                                                            : editor._world.Find(target.id)->hidden;
                                if (!removed) {
                                    throw std::runtime_error("Not every object could be deleted or hidden; successful changes remain in undo history");
                                }
                            }
                        }
                    }
                }
                if (name == "wb_selection_set") {
                    result["_authoredSelection"] = {{"areas", args.value("areas", nlohmann::json::array())}, {"patrol", args.value("patrol", std::string {})}};
                }
                result["selection"] = nlohmann::json::array();
                for (const auto &target : editor.SelectionRefs()) {
                    result["selection"].push_back(id(target.kind, target.id));
                }
            }
            else if (name == "wb_history_undo" || name == "wb_history_redo") {
                std::string label;
                std::vector<SelectionRef> touched;
                const auto authoredSerial = editor._history.AuthoredRestoreSerial();
                const bool changed        = name == "wb_history_undo" ? editor._history.Undo(editor._scene, editor._world, editor._effects, editor._lights, label, &editor._layers, &touched)
                                                                      : editor._history.Redo(editor._scene, editor._world, editor._effects, editor._lights, label, &editor._layers, &touched);
                editor.SelectTouched(touched);
                if (editor._history.AuthoredRestoreSerial() != authoredSerial) {
                    editor._patrolPreview.Stop();
                    if (!editor.RestoreAreaCollision()) {
                        throw std::runtime_error(editor._collisionError);
                    }
                }
                if (!changed) {
                    throw std::runtime_error("No matching history entry is available");
                }
                result = {{"label", label}, {"undo", editor._history.NextUndoLabel()}, {"redo", editor._history.NextRedoLabel()}, {"scope", "Editor history replay; missing objects are skipped and native restoration is best effort"}};
            }
            else if (name == "wb_level_objects_query") {
                ++_browseSerial;
                bool truncated = false;
                _browseEntityGuids.clear();
                editor._world.Browse(args.contains("origin") ? ReadVec(args["origin"]) : editor._camera.GetPose().position, args.value("radius", 40.0F), args.value("limit", std::size_t(100)), _browseRows, truncated);
                std::erase_if(_browseRows, [&](const LevelRow &row) {
                    WorldPick pick;
                    pick.kind     = row.kind;
                    pick.node     = row.node;
                    pick.entityId = row.entityId;
                    return editor._scene.FindByPick(pick) != 0 || (row.kind == WorldPickKind::Entity && row.entityId == editor._camera.GetAnchorEntityId());
                });
                result = {{"items", nlohmann::json::array()}, {"truncated", truncated}, {"browseSerial", std::to_string(_browseSerial)}};
                for (std::size_t index = 0; index < _browseRows.size(); ++index) {
                    const auto &row    = _browseRows[index];
                    const auto *entity = row.kind == WorldPickKind::Entity ? SDK::IEntitySystem::GetInstance()->GetEntity(row.entityId) : nullptr;
                    _browseEntityGuids.push_back(entity ? entity->GetGuid() : 0);
                    WorldPick pick;
                    pick.kind           = row.kind;
                    pick.node           = row.node;
                    pick.entityId       = row.entityId;
                    const bool canAdopt = (row.kind != WorldPickKind::Entity || _browseEntityGuids.back() != 0) && editor._world.CanAdopt(pick);
                    result["items"].push_back({{"id", prefix + "row/" + std::to_string(_browseSerial) + "/" + std::to_string(index)}, {"name", row.label}, {"detail", row.detail}, {"kind", WorldPickKindToString(row.kind)}, {"position", Vec(row.position)},
                        {"distance", row.distance}, {"hidden", row.hidden}, {"edited", row.edited}, {"canAdopt", canAdopt}});
                }
            }
            else if (name == "wb_level_object_get" || name == "wb_level_object_update") {
                if (args.contains("position")) {
                    ReadVec(args["position"]);
                }
                if (args.contains("rotation")) {
                    ReadQuat(args["rotation"]);
                }
                if (args.contains("scale")) {
                    ReadVec(args["scale"]);
                }
                const auto text = args.at("id").get<std::string>();
                SelectionRef target;
                if (text.starts_with(prefix + "row/")) {
                    // MCP browsing owns its row generation independently of UI commands.
                    const std::string rowPrefix = prefix + "row/" + std::to_string(_browseSerial) + "/";
                    if (!text.starts_with(rowPrefix)) {
                        throw std::runtime_error("Stale browser row; query level objects again");
                    }
                    std::size_t index = 0;
                    const auto parsed = std::from_chars(text.data() + rowPrefix.size(), text.data() + text.size(), index);
                    if (parsed.ec != std::errc {} || parsed.ptr != text.data() + text.size() || index >= _browseRows.size()) {
                        throw std::runtime_error("Invalid browser row");
                    }
                    const auto &row = _browseRows[index];
                    if (row.kind == WorldPickKind::RenderNode && !editor._world.IsNodeStanding(row.node, row.position)) {
                        throw std::runtime_error("Level object streamed out; query again");
                    }
                    if (row.kind == WorldPickKind::Entity) {
                        const auto *entity = SDK::IEntitySystem::GetInstance()->GetEntity(row.entityId);
                        if (!entity || entity->IsGarbage() || index >= _browseEntityGuids.size() || entity->GetGuid() != _browseEntityGuids[index]) {
                            throw std::runtime_error("Level entity identity changed; query again");
                        }
                        if (name == "wb_level_object_update" && !_browseEntityGuids[index]) {
                            throw std::runtime_error("This entity has no stable authored identity and cannot be adopted through a cached row");
                        }
                    }
                    WorldPick pick;
                    pick.kind     = row.kind;
                    pick.node     = row.node;
                    pick.entityId = row.entityId;
                    pick.hitPoint = row.position;
                    if (name == "wb_level_object_get") {
                        std::string label, detail;
                        DescribeWorldPick(pick, label, detail);
                        SDK::Vec3 position;
                        bool hidden = false;
                        if (row.kind == WorldPickKind::RenderNode) {
                            const SDK::Matrix34 matrix = row.node->GetMatrix();
                            position                   = {matrix.m[0][3], matrix.m[1][3], matrix.m[2][3]};
                            hidden                     = row.node->IsHidden();
                        }
                        else {
                            const auto *entity = SDK::IEntitySystem::GetInstance()->GetEntity(row.entityId);
                            position           = entity->GetWorldPos();
                            hidden             = entity->IsHidden();
                        }
                        const bool canAdopt = (row.kind != WorldPickKind::Entity || _browseEntityGuids[index] != 0) && editor._world.CanAdopt(pick);
                        result              = {{"id", text}, {"name", label}, {"detail", detail}, {"position", Vec(position)}, {"hidden", hidden}, {"canAdopt", canAdopt}};
                    }
                    else {
                        auto adopted = editor._world.FindByPick(pick);
                        if (!adopted) {
                            const auto *defaultLayer = editor._layers.Find("default");
                            if (!defaultLayer->visible || defaultLayer->locked) {
                                throw std::runtime_error("Default layer is hidden or locked.");
                            }
                            adopted = editor._world.Adopt(pick);
                        }
                        target = {SelectionKind::World, adopted};
                        if (!target.id) {
                            throw std::runtime_error("Level object cannot be adopted");
                        }
                    }
                }
                else {
                    target = ref(args.at("id"));
                    if (target.kind != SelectionKind::World) {
                        throw std::runtime_error("Expected a level object ID");
                    }
                }
                if (target.id) {
                    if (name == "wb_level_object_update") {
                        const auto pose = *editor.DescribeRef(target);
                        if (!editor.CanEdit(target)) {
                            throw std::runtime_error("Object layer is hidden or locked.");
                        }
                        if (args.contains("position") || args.contains("rotation") || args.contains("scale")) {
                            MapEditor::Command command;
                            command.kind     = MapEditor::CommandKind::Transform;
                            command.target   = target.kind;
                            command.id       = target.id;
                            command.position = args.contains("position") ? ReadVec(args["position"]) : pose.position;
                            command.rotation = args.contains("rotation") ? ReadQuat(args["rotation"]) : pose.rotation;
                            command.scale    = args.contains("scale") ? ReadVec(args["scale"]) : pose.scale;
                            transform(target, command.position, command.rotation, command.scale);
                        }
                        if (args.contains("hidden")) {
                            hide(target.id, args["hidden"].get<bool>());
                        }
                        if (args.contains("model")) {
                            replace(target.id, args["model"].get<std::string>());
                        }
                    }
                    result = describe(target);
                }
            }
            else if (name == "wb_level_edits_restore") {
                if (args.value("all", false) && args.contains("ids")) {
                    throw std::runtime_error("Specify either ids or all=true, not both");
                }
                std::vector<SelectionRef> targets;
                std::unordered_set<std::string> seen;
                if (!args.value("all", false)) {
                    if (!args.contains("ids")) {
                        throw std::runtime_error("Specify ids or all=true");
                    }
                    for (const auto &value : args["ids"]) {
                        if (!seen.insert(value.get<std::string>()).second) {
                            throw std::runtime_error("Level object IDs must be unique");
                        }
                        const auto target = ref(value);
                        if (target.kind != SelectionKind::World) {
                            throw std::runtime_error("Expected level object IDs");
                        }
                        targets.push_back(target);
                    }
                }
                if (args.value("all", false)) {
                    std::vector<std::uint64_t> ids;
                    for (const auto &world : editor._world.Edits()) {
                        if (!editor.CanEdit({SelectionKind::World, world.id})) {
                            throw std::runtime_error("A world layer is hidden or locked.");
                        }
                        ids.push_back(world.id);
                    }
                    for (auto id : ids) {
                        editor._world.Restore(id, editor._layers.LayerOf(MapEditor::LayerKey({SelectionKind::World, id})) != "default");
                    }
                }
                else {
                    for (const auto &target : targets) {
                        if (!editor._world.Restore(target.id, editor._layers.LayerOf(MapEditor::LayerKey(target)) != "default")) {
                            throw std::runtime_error("Native restoration failed");
                        }
                    }
                }
                const auto remainingSelection = editor.SelectionRefs();
                std::vector<SelectionRef> stillStanding;
                for (const auto &selected : remainingSelection) {
                    if (editor.DescribeRef(selected)) {
                        stillStanding.push_back(selected);
                    }
                }
                selectExplicit(stillStanding);
                result = {{"restored", true}, {"scope", "Authored level edits released; unavailable objects return with the level's defaults when streamed in"}};
            }
            else if (name == "wb_camera_set") {
                if (!editor._camera.IsActive()) {
                    throw std::runtime_error("Open the editor and wait for its camera to become active");
                }
                const auto pose = editor._camera.GetPose();
                if (args.contains("position")) {
                    editor._camera.SetPosition(ReadVec(args["position"]));
                }
                editor._camera.SetRotation(args.value("yaw", pose.yawDegrees), args.value("pitch", pose.pitchDegrees));
                if (args.contains("fov")) {
                    editor._camera.SetFieldOfView(args["fov"].get<float>());
                }
                if (args.contains("speed")) {
                    editor._camera.SetSpeed(args["speed"].get<float>());
                }
                const auto actual = editor._camera.GetPose();
                result            = {{"position", Vec(actual.position)}, {"yaw", actual.yawDegrees}, {"pitch", actual.pitchDegrees}, {"requestedFov", editor._camera.GetRequestedFieldOfView()}, {"speed", editor._camera.GetSpeed()}};
            }
            else if (name == "wb_camera_frame") {
                if (!editor._camera.IsActive()) {
                    throw std::runtime_error("Open the editor and wait for its camera to become active");
                }
                if (args.contains("position")) {
                    editor._camera.Focus(ReadVec(args["position"]), args.value("distance", 10.0F));
                }
                else {
                    const auto selection = editor.SelectionRefs();
                    if (selection.empty()) {
                        throw std::runtime_error("Nothing selected");
                    }
                    if (args.contains("distance")) {
                        editor._camera.Focus(editor.DescribeRef(selection.front())->position, args.at("distance").get<float>());
                    }
                    else {
                        MapEditor::Command command;
                        command.kind = MapEditor::CommandKind::Focus;
                        editor.Apply(command);
                    }
                }
                result = {{"position", Vec(editor._camera.GetPose().position)}, {"yaw", editor._camera.GetPose().yawDegrees}, {"pitch", editor._camera.GetPose().pitchDegrees}};
            }
            else if (name == "wb_raycast") {
                auto direction     = ReadVec(args.at("direction"));
                const float length = std::sqrt(direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
                if (length < 0.00001F) {
                    throw std::runtime_error("Ray direction cannot be zero");
                }
                direction.x /= length;
                direction.y /= length;
                direction.z /= length;
                const auto pick = editor._world.Pick(ReadVec(args.at("origin")), direction, args.value("distance", 2000.0F), PickEffort::Thorough, editor._viewport.pickPreferPhysics);
                std::string label, detail;
                DescribeWorldPick(pick, label, detail);
                const bool hit = pick.kind != WorldPickKind::None;
                result         = {{"hit", hit}, {"kind", WorldPickKindToString(pick.kind)}, {"position", hit ? Vec(pick.hitPoint) : nlohmann::json(nullptr)}, {"distance", hit ? nlohmann::json(pick.distance) : nlohmann::json(nullptr)}, {"name", label}, {"detail", detail}};
            }
            else if (name == "wb_pick_at") {
                if (!editor._camera.IsActive()) {
                    throw std::runtime_error("Open the editor and wait for its camera to become active");
                }
                const MapEditor::PickResult picked = editor.PickThrough(Camera::RayThroughNdc(editor._camera.GetPose(), args.at("aim").at(0).get<float>(), args.at("aim").at(1).get<float>()), PickEffort::Thorough);
                result                             = {{"hit", false}, {"kind", "none"}, {"id", nullptr}, {"by", nullptr}, {"name", ""}, {"detail", ""}, {"position", nullptr}, {"distance", nullptr}};
                const auto found                   = [&](const char *kind, const char *by) {
                    result["hit"]      = true;
                    result["kind"]     = kind;
                    result["by"]       = by;
                    result["position"] = Vec(picked.point);
                    result["distance"] = picked.distance;
                };
                if (picked.light != 0) {
                    found("light", "bounds");
                    result["id"]   = id(SelectionKind::Light, picked.light);
                    result["name"] = "Point light";
                }
                else if (picked.effect != 0) {
                    found("effect", "bounds");
                    result["id"] = id(SelectionKind::Effect, picked.effect);
                    if (const PlacedEffect *effect = editor._effects.Find(picked.effect)) {
                        result["name"]   = effect->label;
                        result["detail"] = effect->name;
                    }
                }
                else if (picked.placed != 0) {
                    found("prop", picked.placedMethod == PlacedPickMethod::Collision ? "collision" : "bounds");
                    result["id"] = id(SelectionKind::Placed, picked.placed);
                    if (const PlacedObject *object = editor._scene.Find(picked.placed)) {
                        result["name"]   = object->name;
                        result["detail"] = object->model;
                    }
                }
                else if (picked.world.kind != WorldPickKind::None) {
                    const bool object = picked.world.kind == WorldPickKind::RenderNode || picked.world.kind == WorldPickKind::Entity;
                    found(object ? "level" : picked.world.kind == WorldPickKind::Terrain ? "terrain" : "none", picked.world.boundsOnly ? "bounds" : "collision");
                    std::string label, detail;
                    DescribeWorldPick(picked.world, label, detail);
                    result["name"]   = label;
                    result["detail"] = detail;
                    if (const std::uint64_t adopted = editor._world.FindByPick(picked.world)) {
                        result["id"] = id(SelectionKind::World, adopted);
                    }
                }
            }
            else if (name == "wb_pose_anchors_query") {
                std::vector<PoseAnchor> anchors;
                editor.CollectPoseAnchors(args.contains("center") ? ReadVec(args.at("center")) : editor._camera.GetPose().position, args.value("radius", 40.0F), anchors);
                result["anchors"] = nlohmann::json::array();
                for (const PoseAnchor &anchor : anchors) {
                    result["anchors"].push_back({{"position", Vec(anchor.position)}, {"yaw", anchor.yawDegrees}, {"label", anchor.label}, {"class", anchor.partClass}, {"part", anchor.partName},
                        {"objectId", anchor.placedId != 0 ? nlohmann::json(id(SelectionKind::Placed, anchor.placedId)) : nlohmann::json(nullptr)}});
                }
                result["count"] = anchors.size();
            }
            else if (name == "wb_ground_probe" || name == "wb_project_points") {
                result["points"] = nlohmann::json::array();
                for (const auto &value : args.at("points")) {
                    const auto position = ReadVec(value);
                    if (name == "wb_ground_probe") {
                        auto *physics = SDK::CPhysicalWorld::GetInstance();
                        SDK::Vec3 ground {};
                        const bool hit = physics && physics->ResolveGround(position, 3.0F, 200.0F, ground);
                        result["points"].push_back({{"hit", hit}, {"position", hit ? Vec(ground) : nlohmann::json(nullptr)}});
                    }
                    else {
                        Camera::ScreenPoint point;
                        const bool front = Camera::ProjectToScreen(position, point);
                        result["points"].push_back({{"inFront", front}, {"onScreen", front && point.onScreen}, {"x", front ? nlohmann::json(point.x) : nlohmann::json(nullptr)}, {"y", front ? nlohmann::json(point.y) : nlohmann::json(nullptr)}});
                    }
                }
            }
            else if (name == "wb_batch_apply") {
                const auto tools = NativeTools();
                for (const auto &entry : args.at("steps")) {
                    const auto toolName                           = entry.at("tool").get<std::string>();
                    static constexpr std::string_view supported[] = {"wb_objects_query", "wb_object_get", "wb_props_create", "wb_effects_create", "wb_lights_create", "wb_light_update", "wb_objects_transform", "wb_effect_update", "wb_prop_physics_set", "wb_prop_collision_rebuild",
                        "wb_object_settings_update", "wb_objects_duplicate", "wb_objects_convert", "wb_objects_delete", "wb_objects_drop_to_ground", "wb_groups_update", "wb_level_objects_query", "wb_level_object_get", "wb_level_object_update", "wb_level_edits_restore",
                        "wb_camera_set", "wb_camera_frame", "wb_raycast", "wb_pose_anchors_query", "wb_pick_at", "wb_ground_probe", "wb_project_points"};
                    if (std::find(std::begin(supported), std::end(supported), toolName) == std::end(supported)) {
                        throw std::runtime_error("Tool is not supported inside a batch: " + toolName);
                    }
                    const auto tool = std::find_if(tools.begin(), tools.end(), [&](const auto &candidate) {
                        return candidate["name"] == toolName;
                    });
                    std::string error;
                    if (tool == tools.end()) {
                        throw std::runtime_error("Unregistered batch tool: " + toolName);
                    }
                    if (!Shared::WorldBuilder::McpServer::ValidateArguments(tool->at("inputSchema"), entry.at("arguments"), error)) {
                        throw std::runtime_error("Invalid batch step: " + error);
                    }
                }
                result = {{"atomic", false}, {"succeeded", true}, {"steps", nlohmann::json::array()}};
                for (const auto &entry : args.at("steps")) {
                    auto step        = std::make_shared<McpJob>();
                    step->generation = job->generation;
                    step->name       = entry.at("tool").get<std::string>();
                    step->arguments  = entry.at("arguments");
                    ExecuteGame(editor, step);
                    const bool succeeded = step->state.load() == McpJob::State::Completed;
                    result["steps"].push_back({{"tool", step->name}, {"succeeded", succeeded}, {"result", step->result}});
                    if (!succeeded) {
                        result["succeeded"] = false;
                        result["stoppedAt"] = result["steps"].size() - 1;
                        break;
                    }
                }
            }
            else {
                throw std::runtime_error("Tool has no native handler");
            }
            const bool batchFailed = name == "wb_batch_apply" && !result.at("succeeded").get<bool>();
            if (batchFailed) {
                result["error"] = "Batch stopped at the first failed step; prior successful changes remain applied";
            }
            job->result = std::move(result);
            job->state.store(batchFailed ? McpJob::State::Failed : McpJob::State::Completed, std::memory_order_release);
        }
        catch (const std::exception &exception) {
            job->result = {{"error", exception.what()}};
            job->state.store(McpJob::State::Failed, std::memory_order_release);
        }
    }
} // namespace KCDC::Features::Prop
