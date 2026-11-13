#include "prop_editor_project_mcp.h"

#include "prop_editor.h"
#include "core/application.h"
#include "shared/features/world_builder/mcp_file_policy.h"
#include "shared/features/world_builder/world_document.h"

#include <Windows.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <stdexcept>

namespace KCDC::Features::Prop {
    namespace {
        std::string AllowedPath(const std::string &input, const std::string &exportRoot, bool writing = false) {
            return Shared::WorldBuilder::McpFilePolicy::Resolve(input, MapFiles::Directory(), exportRoot, writing);
        }

        void RequireExtension(const std::string &path, const std::string &extension) {
            if (path.size() < extension.size() || path.compare(path.size() - extension.size(), extension.size(), extension) != 0) {
                throw std::runtime_error("Destination must end in " + extension);
            }
        }

        void Write(const std::string &path, const std::string &text, bool overwrite) {
            std::error_code code;
            if (!overwrite && std::filesystem::exists(std::filesystem::u8path(path), code)) {
                throw std::runtime_error("Destination exists; set overwrite to replace it");
            }
            std::string error;
            if (!MapFiles::WritePath(path, text, error)) {
                throw std::runtime_error(error);
            }
        }

        nlohmann::json Counts(const MapDocument &document) {
            // Each kind by the array it saves to, as wb_document_get pages them.
            const auto placed = [&document](PlacedKind kind) {
                return std::count_if(document.objects.begin(), document.objects.end(), [kind](const DocumentObject &object) {
                    return object.kind == kind;
                });
            };
            return {{"objects", placed(PlacedKind::Prop)}, {"doors", placed(PlacedKind::Door)}, {"smartObjects", placed(PlacedKind::SmartObject)}, {"items", placed(PlacedKind::Item)}, {"effects", document.effects.size()}, {"lights", document.lights.size()},
                {"world", document.world.size()}, {"areas", document.areas.size()}, {"patrolRoutes", document.patrolRoutes.size()}, {"views", document.views.size()}};
        }

        MapDocument ParseDocument(const nlohmann::json &value) {
            MapDocument document;
            std::string error;
            const auto text = value.dump();
            if (value.value("format", "") == Shared::WorldBuilder::kProjectFormat) {
                if (!ParseProject(text, document, error)) {
                    throw std::runtime_error(error);
                }
            }
            else if (value.value("format", "") == Shared::WorldBuilder::kBlueprintFormat) {
                if (!ParseBlueprint(text, document, error)) {
                    throw std::runtime_error(error);
                }
            }
            else {
                auto map = value;
                if (value.value("format", "") == Shared::WorldBuilder::kResourceFormat) {
                    nlohmann::json resource;
                    if (!Shared::WorldBuilder::ReadResource(text, resource, error)) {
                        throw std::runtime_error(error);
                    }
                    map["format"] = "kcdc.map";
                }
                std::size_t skipped = 0;
                if (!Shared::WorldBuilder::ValidateContent(map, error) || !MapDocumentFromJson(map, document, skipped, error) || skipped != 0) {
                    throw std::runtime_error(error.empty() ? "Document contains malformed entries" : error);
                }
            }
            return document;
        }

        nlohmann::json ReadJson(const std::string &path) {
            std::string text;
            std::string error;
            if (!MapFiles::ReadPath(path, text, error)) {
                throw std::runtime_error(error);
            }
            const auto value = nlohmann::json::parse(
                text,
                [](int depth, nlohmann::json::parse_event_t, nlohmann::json &) {
                    if (depth > 64) {
                        throw std::runtime_error("Document nesting limit exceeded");
                    }
                    return true;
                },
                false);
            if (!value.is_object()) {
                throw std::runtime_error("File does not contain a JSON document");
            }
            return value;
        }

        nlohmann::json Text(std::size_t maxLength = 1000) {
            return {{"type", "string"}, {"minLength", 1}, {"maxLength", maxLength}};
        }

        nlohmann::json Tool(const char *name, const char *description, bool readOnly, nlohmann::json properties, nlohmann::json required = nlohmann::json::array()) {
            return {{"name", name}, {"description", description}, {"inputSchema", {{"type", "object"}, {"properties", std::move(properties)}, {"required", std::move(required)}, {"additionalProperties", false}}}, {"outputSchema", {{"type", "object"}}},
                {"annotations", {{"readOnlyHint", readOnly}, {"destructiveHint", !readOnly}, {"idempotentHint", readOnly}, {"openWorldHint", false}}}};
        }
    } // namespace

    nlohmann::json ProjectMcp::Tools() {
        const nlohmann::json boolean {{"type", "boolean"}};
        const nlohmann::json offset {{"type", "integer"}, {"minimum", 0}, {"maximum", 1000000}};
        const nlohmann::json limit {{"type", "integer"}, {"minimum", 1}, {"maximum", 200}};
        const nlohmann::json selection {{"type", "string"}, {"enum", {"scene", "areas", "patrols", "all"}}};
        return nlohmann::json::array({
            Tool("wb_project_get", "Read project identity, dirty state, counts and allowed file roots.", true, nlohmann::json::object()),
            Tool("wb_projects_list", "List allowed recent project paths and recovery availability.", true, nlohmann::json::object()),
            Tool("wb_project_new", "Replace with a new empty project. Dirty projects require discardChanges=true.", false, {{"discardChanges", boolean}}),
            Tool("wb_project_open", "Open an explicit .project.json on the current level; no dialogs. Dirty projects require discardChanges=true.", false, {{"path", Text()}, {"discardChanges", boolean}}, {"path"}),
            Tool("wb_project_recover", "Open the current recovery project; requires discardChanges=true if dirty.", false, {{"discardChanges", boolean}}),
            Tool("wb_project_save", "Save the project to an explicit .project.json under an allowed root, using a staged write.", false, {{"path", Text()}, {"overwrite", boolean}}, {"path"}),
            Tool("wb_project_update", "Set the resource name or an export directory within currently allowed roots. Select other export roots in World Builder.", false, {{"resourceName", Text(63)}, {"exportDirectory", Text()}}),
            Tool("wb_client_mods_get", "Read the client mods folder, the mods found in it, which are ticked and which are loaded.", true, nlohmann::json::object()),
            Tool("wb_client_mods_update", "Set the resources folder (within currently allowed roots; select others in World Builder) and which client mods are ticked. Offline only. Takes effect after wb_client_mods_apply.", false, {{"folder", Text()}, {"mods", {{"type", "array"}, {"maxItems", 64}, {"items", Text(64)}}}}),
            Tool("wb_client_mods_apply", "Reload the level with the ticked client mods and reopen the saved project. Offline only; the project must be saved. Invalidates object IDs and drops this connection until the level is back.", false, nlohmann::json::object()),
            Tool("wb_document_get", "Read a paginated document collection; read project metadata with wb_project_get.", true,
                {{"collection", {{"type", "string"}, {"enum", {"objects", "doors", "smartObjects", "items", "effects", "lights", "world", "areas", "patrolRoutes", "views"}}}}, {"offset", offset}, {"limit", limit}}, {"collection"}),
            Tool("wb_document_validate", "Validate a complete project, map, blueprint or world document without changing the editor.", true, {{"document", {{"type", "object"}}}}, {"document"}),
            Tool("wb_document_import", "Deprecated compatibility import. Use wb_project_open or wb_blueprint_place. replace=true requires discardChanges if dirty.", false, {{"document", {{"type", "object"}}}, {"replace", boolean}, {"discardChanges", boolean}}, {"document"}),
            Tool("wb_files_list", "List a bounded page of files and directories within an allowed root. Paths are relative to maps unless absolute.", true, {{"path", Text()}, {"offset", offset}, {"limit", limit}}),
            Tool("wb_blueprint_save", "Save selected scene objects, areas or patrols as a .blueprint.json; selection=all includes the entire scene.", false, {{"path", Text()}, {"selection", selection}, {"overwrite", boolean}}, {"path"}),
            Tool("wb_blueprint_place", "Place an explicit .blueprint.json at a world position with a fresh group, without changing the clipboard.", false,
                {{"path", Text()}, {"position", {{"type", "array"}, {"minItems", 3}, {"maxItems", 3}, {"items", {{"type", "number"}, {"minimum", -1000000}, {"maximum", 1000000}}}}}, {"group", Text(95)}}, {"path", "position"}),
            Tool("wb_world_export", "Validate and export the scene to an explicit .world.json under an allowed root.", false, {{"path", Text()}, {"overwrite", boolean}}, {"path"}),
            Tool("wb_api_export", "Write server API calls for the selected objects or whole scene to an explicit .js under an allowed root.", false, {{"path", Text()}, {"selection", selection}, {"overwrite", boolean}}, {"path"}),
        });
    }

    bool ProjectMcp::Execute(MapEditor &editor, const EditorSnapshot &snapshot, const std::string &name, const nlohmann::json &arguments, nlohmann::json &result) {
        static const auto tools = Tools();
        if (std::none_of(tools.begin(), tools.end(), [&](const auto &tool) {
                return tool.at("name") == name;
            })) {
            return false;
        }
        const std::string exportRoot = editor._exportDirectory;
        if (name == "wb_project_get") {
            result = {{"path", editor._projectPath}, {"level", snapshot.level}, {"resourceName", editor._mapName}, {"exportDirectory", exportRoot}, {"mapsDirectory", MapFiles::Directory()}, {"dirty", editor.IsProjectDirty(snapshot)},
                {"counts", Counts(editor.BuildDocument(snapshot))}};
            return true;
        }
        if (name == "wb_projects_list") {
            auto recent = nlohmann::json::array();
            for (const auto &path : editor._recentProjects) {
                try {
                    recent.push_back(AllowedPath(path, exportRoot));
                }
                catch (const std::runtime_error &) { /* Other user-selected locations are outside the MCP file roots. */
                }
            }
            result = {{"recent", std::move(recent)}, {"recoveryAvailable", editor._recoveryExists}};
            return true;
        }
        if (name == "wb_files_list") {
            const auto directory = std::filesystem::u8path(AllowedPath(arguments.value("path", MapFiles::Directory()), exportRoot));
            std::error_code error;
            std::vector<std::filesystem::directory_entry> entries;
            for (std::filesystem::directory_iterator iterator(directory, error); !error && iterator != std::filesystem::directory_iterator(); iterator.increment(error)) {
                if (entries.size() == 10000) {
                    throw std::runtime_error("Directory contains more than 10000 entries; choose a smaller folder");
                }
                if ((GetFileAttributesW(iterator->path().c_str()) & FILE_ATTRIBUTE_REPARSE_POINT) == 0) {
                    entries.push_back(*iterator);
                }
            }
            if (error) {
                throw std::runtime_error("Cannot list directory: " + error.message());
            }
            std::sort(entries.begin(), entries.end(), [](const auto &left, const auto &right) {
                return left.path().filename() < right.path().filename();
            });
            const auto start = std::min(arguments.value("offset", std::size_t(0)), entries.size());
            const auto end   = std::min(start + arguments.value("limit", std::size_t(100)), entries.size());
            auto rows        = nlohmann::json::array();
            for (auto index = start; index < end; ++index) {
                rows.push_back({{"name", Shared::WorldBuilder::McpFilePolicy::Utf8(entries[index].path().filename())}, {"path", Shared::WorldBuilder::McpFilePolicy::Utf8(entries[index].path())}, {"directory", entries[index].is_directory(error)}});
            }
            result = {{"path", Shared::WorldBuilder::McpFilePolicy::Utf8(directory)}, {"items", std::move(rows)}, {"total", entries.size()}, {"nextOffset", end < entries.size() ? nlohmann::json(end) : nlohmann::json(nullptr)}};
            return true;
        }
        if (name == "wb_client_mods_get") {
            editor.ListClientMods();
            auto mods = nlohmann::json::array();
            for (const auto &mod : editor._clientModList) {
                mods.push_back({{"resource", mod.resource}, {"version", mod.version}, {"description", mod.description}, {"lanes", mod.lanes}, {"bytes", mod.bytes}, {"risky", mod.risky}, {"ticked", std::binary_search(editor._clientMods.begin(), editor._clientMods.end(), mod.resource)}});
            }
            const auto &assets = Core::gApplication->GetStreamedAssetService();
            result             = {{"folder", editor._clientModsFolder}, {"listError", editor._clientModListError}, {"mods", std::move(mods)}, {"ticked", editor._clientMods}, {"loaded", assets.GetLocalMods()}, {"warnings", assets.GetLocalModWarnings()}};
            return true;
        }
        if (name == "wb_document_get") {
            const auto document = MapDocumentToJson(editor.BuildDocument(snapshot));
            // A map without doors, smart objects or items carries no array for them.
            const auto rows     = document.value(arguments.at("collection").get<std::string>(), nlohmann::json::array());
            const auto start    = std::min(arguments.value("offset", std::size_t(0)), rows.size());
            const auto end      = std::min(start + arguments.value("limit", std::size_t(100)), rows.size());
            auto page           = nlohmann::json::array();
            for (auto index = start; index < end; ++index) {
                page.push_back(rows.at(index));
            }
            result = {{"collection", arguments.at("collection")}, {"items", std::move(page)}, {"total", rows.size()}, {"nextOffset", end < rows.size() ? nlohmann::json(end) : nlohmann::json(nullptr)}};
            return true;
        }
        if (name == "wb_document_validate") {
            const auto document = ParseDocument(arguments.at("document"));
            result              = {{"valid", true}, {"level", document.level}, {"counts", Counts(document)}};
            return true;
        }
        if (!editor.IsSettled(snapshot)) {
            throw std::runtime_error("Wait for the current editor operation to finish");
        }
        if ((name == "wb_document_import" && !arguments.value("replace", false)) || name == "wb_blueprint_place") {
            if (std::any_of(snapshot.layers->Layers().begin(), snapshot.layers->Layers().end(), [](const EditorLayer &layer) {
                    return !layer.visible || layer.locked;
                })) {
                throw std::runtime_error("Show and unlock layers before importing a document.");
            }
        }
        auto requireDiscard = [&] {
            if (editor.IsProjectDirty(snapshot) && !arguments.value("discardChanges", false)) {
                throw std::runtime_error("Project has unsaved changes; save first or explicitly set discardChanges=true");
            }
        };
        auto validateProjectPreferences = [&](const MapDocument &document) {
            if (document.resourceName.size() >= sizeof(editor._mapName) || document.exportDirectory.size() >= sizeof(editor._exportDirectory)) {
                throw std::runtime_error("Project preferences exceed editor limits");
            }
            if (document.resourceName.find('\0') != std::string::npos) {
                throw std::runtime_error("Resource names cannot contain NUL characters");
            }
            if (!document.exportDirectory.empty()) {
                AllowedPath(document.exportDirectory, exportRoot);
            }
            if (!document.level.empty() && document.level != snapshot.level) {
                throw std::runtime_error("Document belongs to a different level; open that level before importing");
            }
        };
        if (name == "wb_project_new") {
            requireDiscard();
            editor.NewProject(snapshot);
            result = {{"replaced", true}};
        }
        else if (name == "wb_project_open" || name == "wb_project_recover") {
            requireDiscard();
            const auto path = AllowedPath(name == "wb_project_recover" ? MapFiles::Directory() + "/recovery.project.json" : arguments.at("path").get<std::string>(), exportRoot);
            RequireExtension(path, ".project.json");
            const auto value = ReadJson(path);
            if (value.value("format", "") != Shared::WorldBuilder::kProjectFormat) {
                throw std::runtime_error("Expected a World Builder project");
            }
            const auto document = ParseDocument(value);
            validateProjectPreferences(document);
            editor.ReplaceProject(document);
            const bool recovery      = name == "wb_project_recover" || Shared::WorldBuilder::McpFilePolicy::Canonical(std::filesystem::u8path(path)) == Shared::WorldBuilder::McpFilePolicy::Canonical(std::filesystem::u8path(MapFiles::Directory() + "/recovery.project.json"));
            editor._projectPath      = recovery ? std::string() : path;
            editor._recoveredProject = recovery;
            if (!recovery) {
                editor.RememberProject(path);
            }
            result = {{"path", path}, {"recovered", recovery}, {"counts", Counts(document)}};
        }
        else if (name == "wb_project_save") {
            const auto path = AllowedPath(arguments.at("path").get<std::string>(), exportRoot, true);
            RequireExtension(path, ".project.json");
            const auto project = ProjectToJson(editor.BuildDocument(snapshot));
            Write(path, project.dump(2), arguments.value("overwrite", false));
            editor._projectPath = path;
            editor.MarkProjectSaved(project);
            editor.RememberProject(path);
            result = {{"path", path}, {"saved", true}};
        }
        else if (name == "wb_client_mods_update") {
            if (!Core::gApplication->IsOfflineBuilderReady()) {
                throw std::runtime_error("Client mods are chosen in the offline World Builder");
            }
            if (!arguments.contains("folder") && !arguments.contains("mods")) {
                throw std::runtime_error("Provide folder or mods");
            }
            if (arguments.contains("folder")) {
                const auto folder = AllowedPath(arguments.at("folder").get<std::string>(), exportRoot);
                if (folder.size() >= sizeof(editor._clientModsFolder)) {
                    throw std::runtime_error("Resources folder exceeds editor limits");
                }
                editor.SetClientModsFolder(folder);
            }
            if (arguments.contains("mods")) {
                auto mods = arguments.at("mods").get<std::vector<std::string>>();
                std::sort(mods.begin(), mods.end());
                mods.erase(std::unique(mods.begin(), mods.end()), mods.end());
                editor._clientMods = std::move(mods);
            }
            result = {{"folder", editor._clientModsFolder}, {"ticked", editor._clientMods}, {"loaded", editor.ClientModsMatchLoaded()}};
        }
        else if (name == "wb_client_mods_apply") {
            if (!Core::gApplication->IsOfflineBuilderReady()) {
                throw std::runtime_error("Client mods are chosen in the offline World Builder");
            }
            std::string error;
            if (!editor.ApplyClientMods(snapshot, error)) {
                throw std::runtime_error(error);
            }
            result = {{"transitionAccepted", true}, {"loadingComplete", false}, {"reconnectRequired", true}};
        }
        else if (name == "wb_project_update") {
            if (!arguments.contains("resourceName") && !arguments.contains("exportDirectory")) {
                throw std::runtime_error("Provide resourceName or exportDirectory");
            }
            const auto directory = arguments.contains("exportDirectory") ? AllowedPath(arguments.at("exportDirectory").get<std::string>(), exportRoot) : exportRoot;
            if (directory.size() >= sizeof(editor._exportDirectory)) {
                throw std::runtime_error("Export directory exceeds editor limits");
            }
            if (arguments.contains("resourceName")) {
                const auto resourceName = arguments.at("resourceName").get<std::string>();
                if (resourceName.size() >= sizeof(editor._mapName) || resourceName.find('\0') != std::string::npos) {
                    throw std::runtime_error("Resource name must fit in 63 UTF-8 bytes and cannot contain NUL characters");
                }
                std::snprintf(editor._mapName, sizeof(editor._mapName), "%s", resourceName.c_str());
            }
            std::snprintf(editor._exportDirectory, sizeof(editor._exportDirectory), "%s", directory.c_str());
            result = {{"resourceName", editor._mapName}, {"exportDirectory", directory}};
        }
        else if (name == "wb_document_import") {
            auto document = ParseDocument(arguments.at("document"));
            validateProjectPreferences(document);
            if (arguments.value("replace", false)) {
                requireDiscard();
                editor.ReplaceProject(document);
                editor._recoveredProject = true; // Imported data is unsaved, even after the queued replacement settles.
            }
            else {
                // Merged documents contribute content, not another project's layer IDs.
                for (auto &object : document.objects) {
                    object.layer = "default";
                }
                for (auto &effect : document.effects) {
                    effect.layer = "default";
                }
                for (auto &entry : document.lights) {
                    entry.layer = "default";
                }
                document.worldLayers.clear();
                editor._areas.Import(document.areas, false, document.areaGroups);
                editor._patrols.Import(document.patrolRoutes, false);
                editor._views.insert(editor._views.end(), document.views.begin(), document.views.end());
                ++editor._viewsRevision;
                MapEditor::Command command;
                command.kind     = MapEditor::CommandKind::Import;
                command.document = std::make_shared<MapDocument>(document);
                editor.Queue(std::move(command));
            }
            result = {{"counts", Counts(document)}, {"replaced", arguments.value("replace", false)}};
        }
        else if (name == "wb_blueprint_place") {
            const auto path = AllowedPath(arguments.at("path").get<std::string>(), exportRoot);
            RequireExtension(path, ".blueprint.json");
            const auto value = ReadJson(path);
            if (value.value("format", "") != Shared::WorldBuilder::kBlueprintFormat) {
                throw std::runtime_error("Expected a World Builder blueprint");
            }
            auto document = ParseDocument(value);
            std::unordered_set<std::string> taken;
            for (const auto &row : snapshot.rows) {
                taken.insert(row.group);
            }
            for (const auto &row : snapshot.effectRows) {
                taken.insert(row.group);
            }
            for (const auto &row : snapshot.lightRows) {
                taken.insert(row.group);
            }
            for (const auto &[id, group] : editor._areas.Groups()) {
                taken.insert(group);
            }
            const auto group     = MapEditor::UniqueGroup(arguments.value("group", "Blueprint"), taken);
            const auto &position = arguments.at("position");
            OffsetDocument(document, {position.at(0).get<float>(), position.at(1).get<float>(), position.at(2).get<float>()});
            for (auto &object : document.objects) {
                object.group = group;
            }
            for (auto &effect : document.effects) {
                effect.group = group;
            }
            for (auto &entry : document.lights) {
                entry.group = group;
            }
            for (const auto &area : document.areas) {
                document.areaGroups[area.id] = group;
            }
            editor._areas.Import(document.areas, false, document.areaGroups);
            editor._patrols.Import(document.patrolRoutes, false);
            MapEditor::Command command;
            command.kind     = MapEditor::CommandKind::Import;
            command.document = std::make_shared<MapDocument>(document);
            editor.Queue(std::move(command));
            result = {{"path", path}, {"group", group}, {"counts", Counts(document)}};
        }
        else {
            const auto path = AllowedPath(arguments.at("path").get<std::string>(), exportRoot, true);
            MapDocument document;
            const auto selection = arguments.value("selection", name == "wb_world_export" ? "all" : "scene");
            if (selection == "all") {
                document = editor.BuildDocument(snapshot);
            }
            else if (selection == "areas") {
                document = editor._areas.SelectedDocument();
            }
            else if (selection == "patrols") {
                document = editor._patrols.SelectedDocument();
            }
            else {
                document = snapshot.selectedDocument;
            }
            document.level = snapshot.level;
            std::string text;
            std::string error;
            if (name == "wb_blueprint_save") {
                RequireExtension(path, ".blueprint.json");
                if (!SerializeBlueprint(document, text, error)) {
                    throw std::runtime_error(error);
                }
            }
            else if (name == "wb_api_export") {
                RequireExtension(path, ".js");
                text = Shared::WorldBuilder::ApiCalls(MapDocumentToJson(document));
            }
            else {
                RequireExtension(path, ".world.json");
                const auto resource = Shared::WorldBuilder::ExportResource(MapDocumentToJson(document));
                if (snapshot.level.empty() || !Shared::WorldBuilder::ValidateContent(resource, error)) {
                    throw std::runtime_error(error.empty() ? "The scene has no level" : error);
                }
                text = resource.dump(2);
            }
            Write(path, text, arguments.value("overwrite", false));
            result = {{"path", path}, {"bytes", text.size()}, {"counts", Counts(document)}};
        }
        return true;
    }
} // namespace KCDC::Features::Prop
