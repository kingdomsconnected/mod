#include "prop_editor.h"

#include "common/module_support.h"
#include "shared/features/world_builder/world_document.h"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace KCDC::Features::Prop {
    namespace {
        constexpr const char *kLeavePrompt   = "Save before leaving?";
        constexpr const char *kReplacePrompt = "Unsaved project";

        constexpr std::size_t kMaxRecentProjects = 10;

        // The unsaved check is cheap enough to run twice a second; the
        // autosave it may carry out is not, so that is rarer.
        constexpr std::chrono::milliseconds kProjectCheckInterval {500};
        constexpr std::chrono::seconds kAutosaveInterval {30};

        std::string RecoveryPath() {
            return MapFiles::Directory() + "/recovery.project.json";
        }

        // A modal centred on the screen, whatever window opened it.
        void CentreNextPopup() {
            ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5F, 0.5F));
        }
    } // namespace

    MapDocument MapEditor::BuildDocument(const EditorSnapshot &snapshot) const {
        MapDocument document     = snapshot.unresolvedDocument != nullptr ? *snapshot.unresolvedDocument : MapDocument {};
        document.layers          = *snapshot.layers;
        document.worldLayers     = snapshot.worldLayers;
        document.viewport        = _viewport;
        document.environment     = _environment.Preview();
        document.propFavorites   = _library.GetFavoriteFolders();
        document.effectFavorites = _effectLibrary.GetFavoriteFolders();
        document.level           = snapshot.level;
        document.exportDirectory = _exportDirectory;
        document.resourceName    = _mapName;
        for (auto &object : document.objects) {
            if (!document.layers.Find(object.layer)) {
                object.layer = "default";
            }
        }
        for (auto &effect : document.effects) {
            if (!document.layers.Find(effect.layer)) {
                effect.layer = "default";
            }
        }
        document.objects.reserve(document.objects.size() + snapshot.rows.size());
        for (const SceneRow &row : snapshot.rows) {
            document.objects.push_back(DocumentObject {row.model, row.position, row.rotation, row.scale, row.physics, row.group, snapshot.layers->LayerOf("prop:" + std::to_string(row.id)), row.kind, row.settings});
        }
        for (auto &entry : document.lights) {
            if (!document.layers.Find(entry.layer)) {
                entry.layer = "default";
            }
        }
        for (const auto &row : snapshot.lightRows) {
            document.lights.push_back({row.position, row.tuning, row.group, snapshot.layers->LayerOf("light:" + std::to_string(row.id))});
        }
        document.effects.reserve(document.effects.size() + snapshot.effectRows.size());
        for (const EffectRow &row : snapshot.effectRows) {
            document.effects.push_back(DocumentEffect {row.name, row.position, row.rotation, row.tuning, row.group, snapshot.layers->LayerOf("effect:" + std::to_string(row.id))});
        }
        document.world.insert(document.world.end(), snapshot.worldDocument.begin(), snapshot.worldDocument.end());
        document.views        = _views;
        document.areas        = _areas.Authored();
        document.patrolRoutes = _patrols.Authored();
        document.areaGroups   = _areas.Groups();
        if (snapshot.cameraHeld) {
            document.camera = DocumentView {"", snapshot.camera.position, snapshot.camera.yawDegrees, snapshot.camera.pitchDegrees};
        }
        return document;
    }

    void MapEditor::RequestProject(const std::string &path) {
        std::scoped_lock lock(_commandMutex);
        _requestedProject    = path;
        _requestedNewProject = false;
    }

    void MapEditor::RequestNewProject() {
        std::scoped_lock lock(_commandMutex);
        _requestedProject.clear();
        _requestedNewProject = true;
    }

    std::string MapEditor::UniqueGroup(const std::string &stem, const std::unordered_set<std::string> &taken) {
        if (!taken.contains(stem)) {
            return stem;
        }
        for (std::size_t suffix = 2;; ++suffix) {
            std::string candidate = stem + "-" + std::to_string(suffix);
            if (!taken.contains(candidate)) {
                return candidate;
            }
        }
    }

    MapDocument MapEditor::SelectedDocument(const EditorSnapshot &snapshot) const {
        // Use the active editor tab's selection when copying or saving a blueprint.
        if (_patrols.IsTabVisible()) {
            MapDocument document = _patrols.SelectedDocument();
            document.level       = snapshot.level;
            return document;
        }
        if (_areas.IsTabVisible()) {
            MapDocument document = _areas.SelectedDocument();
            document.level       = snapshot.level;
            return document;
        }
        MapDocument document = snapshot.selectedDocument;
        document.level       = snapshot.level;
        return document;
    }

    void MapEditor::CopySelection(const EditorSnapshot &snapshot) {
        if (!IsSettled(snapshot)) {
            _notice = "Finish the current edit before copying.";
            return;
        }
        const std::string script = Shared::WorldBuilder::ApiCalls(MapDocumentToJson(SelectedDocument(snapshot)));
        if (script.empty()) {
            _notice = "Select a placed object, effect, area, patrol route or level edit first.";
            return;
        }
        ImGui::SetClipboardText(script.c_str());
        _notice = "Copied the selection as server API calls.";
    }

    void MapEditor::ReplaceProject(const MapDocument &document) {
        _viewport = document.viewport;
        _environment.Adopt(document.environment);
        _library.SetFavoriteFolders(document.propFavorites);
        _effectLibrary.SetFavoriteFolders(document.effectFavorites);
        _views = document.views;
        ++_viewsRevision;
        std::snprintf(_mapName, sizeof(_mapName), "%s", document.resourceName.c_str());
        _areas.Import(document.areas, true, document.areaGroups);
        _patrols.Import(document.patrolRoutes, true);
        std::snprintf(_exportDirectory, sizeof(_exportDirectory), "%s", document.exportDirectory.c_str());
        Command command;
        command.kind     = CommandKind::ReplaceDocument;
        command.document = std::make_shared<MapDocument>(document);
        Queue(std::move(command));
        // The baseline is taken once the tick has applied it, so it carries
        // what this level could not resolve exactly as a save would.
        _baselineAfter = _nextCommand.load(std::memory_order_acquire);
    }

    bool MapEditor::IsProjectDirty(const EditorSnapshot &snapshot) const {
        return _savedProject == nullptr || ProjectToJson(BuildDocument(snapshot)) != *_savedProject;
    }

    void MapEditor::MarkProjectSaved(nlohmann::json project) {
        _savedProject = std::make_shared<const nlohmann::json>(std::move(project));
        _projectDirty = false;
        ++_projectGeneration;
    }

    void MapEditor::NewProject(const EditorSnapshot &snapshot) {
        MapDocument document;
        document.level = snapshot.level;
        ReplaceProject(document);
        _projectPath.clear();
    }

    void MapEditor::OpenProject(const std::string &path, const EditorSnapshot &snapshot) {
        std::string text;
        std::string error;
        MapDocument document;
        if (!MapFiles::ReadPath(path, text, error) || !ParseProject(text, document, error)) {
            _notice = error;
            return;
        }
        if (!document.level.empty() && !snapshot.level.empty() && document.level != snapshot.level) {
            _notice = "This project uses " + document.level + ". Open it from the main menu to load that level.";
            return;
        }
        std::error_code recoveryError;
        const bool recovery = std::filesystem::equivalent(std::filesystem::u8path(path), std::filesystem::u8path(RecoveryPath()), recoveryError);
        _projectPath        = recovery ? std::string {} : path;
        ReplaceProject(document);
        if (recovery) {
            _recoveredProject = true;
            _notice           = "Recovered autosave. Save it as a named project.";
            return;
        }
        RememberProject(path);
        _notice = "Opened " + path;
    }

    void MapEditor::RememberProject(const std::string &path) {
        _recentProjects.erase(std::remove(_recentProjects.begin(), _recentProjects.end(), path), _recentProjects.end());
        _recentProjects.insert(_recentProjects.begin(), path);
        if (_recentProjects.size() > kMaxRecentProjects) {
            _recentProjects.resize(kMaxRecentProjects);
        }
        // Best effort: a list that cannot be written only forgets.
        std::string error;
        MapFiles::WritePath(MapFiles::RecentProjectsPath(), nlohmann::json(_recentProjects).dump(2), error);
    }

    bool MapEditor::SaveProject(const EditorSnapshot &snapshot, bool saveAs) {
        if (!IsSettled(snapshot)) {
            _notice = "Finish the current edit before saving.";
            return false;
        }
        std::string path = _projectPath;
        if (saveAs || path.empty()) {
            path = MapFiles::ChoosePath(true, false, path.empty() ? MapFiles::Directory() + "/" + MapFiles::SanitizeName(_mapName) + ".project.json" : path, _notice);
        }
        if (path.empty()) {
            return false;
        }
        nlohmann::json project = ProjectToJson(BuildDocument(snapshot));
        std::string error;
        if (!MapFiles::WritePath(path, project.dump(2), error)) {
            _notice = error;
            return false;
        }
        _projectPath = path;
        MarkProjectSaved(std::move(project));
        RememberProject(path);
        _notice = "Saved " + path;
        return true;
    }

    void MapEditor::UpdateProjectBaseline(const EditorSnapshot &snapshot) {
        if (!_projectInitialized) {
            _projectInitialized = true;
            _recentProjects     = MapFiles::RecentProjects();
            std::error_code fileError;
            _recoveryExists = std::filesystem::exists(std::filesystem::u8path(RecoveryPath()), fileError);
            MarkProjectSaved(ProjectToJson(BuildDocument(snapshot)));
        }

        if (_baselineAfter == 0 || snapshot.completedCommand < _baselineAfter) {
            return;
        }
        _baselineAfter = 0;
        if (_recoveredProject) {
            _savedProject.reset();
            _projectDirty = true;
            ++_projectGeneration;
        }
        else {
            MarkProjectSaved(ProjectToJson(BuildDocument(snapshot)));
        }
        _recoveredProject = false;
    }

    void MapEditor::UpdateProject(const EditorSnapshot &snapshot) {
        bool requestedNew = false;
        std::string requested;
        {
            std::scoped_lock lock(_commandMutex);
            requested.swap(_requestedProject);
            requestedNew = std::exchange(_requestedNewProject, false);
        }
        if (requestedNew) {
            NewProject(snapshot);
        }
        else if (!requested.empty()) {
            OpenProject(requested, snapshot);
        }

        // Asked once the scene has settled, and only then compared: the
        // prompt holds the answer while it is up.
        if (_pendingExit && !ImGui::IsPopupOpen(kLeavePrompt) && IsSettled(snapshot)) {
            if (IsProjectDirty(snapshot)) {
                ImGui::OpenPopup(kLeavePrompt);
            }
            else {
                _pendingExit = false;
                _exitRequested.store(true, std::memory_order_release);
            }
        }
        CentreNextPopup();
        if (ImGui::BeginPopupModal(kLeavePrompt, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Save your project before returning to the menu?");
            if (ImGui::Button("Save and leave") && SaveProject(snapshot, false)) {
                _pendingExit = false;
                _exitRequested.store(true, std::memory_order_release);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Discard and leave")) {
                _pendingExit = false;
                _exitRequested.store(true, std::memory_order_release);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Keep editing")) {
                _pendingExit = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        UpdateProjectBaseline(snapshot);
        if (_baselineAfter != 0) {
            return;
        }

        if (_projectCheck.valid() && _projectCheck.wait_for(std::chrono::seconds::zero()) == std::future_status::ready) {
            const ProjectCheck check = _projectCheck.get();
            if (check.generation == _projectGeneration) {
                _projectDirty = check.dirty;
            }
            if (check.autosaved) {
                _lastAutosave   = std::chrono::steady_clock::now();
                _recoveryExists = true;
            }
            if (!check.error.empty()) {
                _notice = "Recovery save failed: " + check.error;
            }
        }

        // Only the copy is taken here; turning it into JSON, comparing it and
        // writing the autosave all happen on a worker.
        const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        if (_projectCheck.valid() || now - _lastProjectCheck < kProjectCheckInterval || !IsSettled(snapshot)) {
            return;
        }
        _lastProjectCheck   = now;
        const bool autosave = now - _lastAutosave >= kAutosaveInterval;
        _projectCheck       = std::async(std::launch::async, [document = BuildDocument(snapshot), saved = _savedProject, generation = _projectGeneration, autosave, recovery = RecoveryPath()]() {
            ProjectCheck check;
            check.generation             = generation;
            const nlohmann::json current = ProjectToJson(document);
            check.dirty                  = saved == nullptr || current != *saved;
            if (check.dirty && autosave) {
                check.autosaved = MapFiles::WritePath(recovery, current.dump(2), check.error);
            }
            return check;
        });
    }

    void MapEditor::BrowseDirectory(const std::string &path) {
        std::snprintf(_directoryInput, sizeof(_directoryInput), "%s", path.c_str());
        MapFiles::ListDirectories(path, _browsedDirectory, _directoryEntries, _directoryError);
        if (_browsedDirectory.size() >= sizeof(_exportDirectory)) {
            _directoryError = "The export directory path is too long.";
            _browsedDirectory.clear();
            _directoryEntries.clear();
        }
        if (!_browsedDirectory.empty()) {
            std::snprintf(_directoryInput, sizeof(_directoryInput), "%s", _browsedDirectory.c_str());
        }
    }

    void MapEditor::RenderDirectoryPicker() {
        CentreNextPopup();
        ImGui::SetNextWindowSize(ImVec2(620.0F, 0.0F), ImGuiCond_Appearing);
        if (!ImGui::BeginPopupModal("Export folder", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            return;
        }
        ImGui::TextWrapped("Open a folder below, or enter a path to another drive or folder.");
        const bool entered = ImGui::InputText("Path", _directoryInput, sizeof(_directoryInput), ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        if (ImGui::Button("Go") || entered) {
            BrowseDirectory(std::string(_directoryInput));
        }
        std::string navigate;
        ImGui::BeginDisabled(_browsedDirectory.empty());
        if (ImGui::Button("Parent folder")) {
            const auto parent = std::filesystem::u8path(_browsedDirectory).parent_path().u8string();
            navigate.assign(parent.begin(), parent.end());
        }
        ImGui::EndDisabled();
        if (!_directoryError.empty()) {
            ImGui::TextWrapped("%s", _directoryError.c_str());
        }
        if (ImGui::BeginChild("Folders", ImVec2(0.0F, 240.0F), ImGuiChildFlags_Borders)) {
            for (std::size_t index = 0; index < _directoryEntries.size(); ++index) {
                const std::string &name = _directoryEntries[index];
                ImGui::PushID(static_cast<int>(index));
                // Keep filenames containing ImGui's ## marker visible verbatim.
                const ImVec2 labelPosition = ImGui::GetCursorScreenPos();
                if (ImGui::Selectable("##folder", false, ImGuiSelectableFlags_NoAutoClosePopups)) {
                    const auto child = (std::filesystem::u8path(_browsedDirectory) / std::filesystem::u8path(name)).u8string();
                    navigate.assign(child.begin(), child.end());
                }
                ImGui::GetWindowDrawList()->AddText(labelPosition, ImGui::GetColorU32(ImGuiCol_Text), name.c_str());
                ImGui::PopID();
            }
        }
        ImGui::EndChild();
        if (!navigate.empty()) {
            BrowseDirectory(navigate);
        }
        ImGui::BeginDisabled(_browsedDirectory.empty());
        if (ImGui::Button("Use this folder")) {
            std::snprintf(_exportDirectory, sizeof(_exportDirectory), "%s", _browsedDirectory.c_str());
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    void MapEditor::RenderFiles(const EditorSnapshot &snapshot) {
        ImGui::TextWrapped("%s%s", _projectPath.empty() ? "Untitled project" : _projectPath.c_str(), _projectDirty ? " *" : "");
        if (ImGui::Button("New")) {
            _pendingNew = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Open...")) {
            _pendingOpen = MapFiles::ChoosePath(false, false, _projectPath, _notice);
        }
        ImGui::SameLine();
        if (ImGui::Button("Save")) {
            SaveProject(snapshot, false);
        }
        ImGui::SameLine();
        if (ImGui::Button("Save as...")) {
            SaveProject(snapshot, true);
        }
        ImGui::TextDisabled("Ctrl+S saves; Ctrl+Shift+S saves a copy.");
        if (ImGui::BeginCombo("Recent projects", "Choose a project")) {
            for (const std::string &path : _recentProjects) {
                if (ImGui::Selectable(path.c_str())) {
                    _pendingOpen = path;
                }
            }
            ImGui::EndCombo();
        }
        if (_recoveryExists && ImGui::Button("Recover autosaved project")) {
            _pendingOpen = RecoveryPath();
        }
        ImGui::TextWrapped("Recovery is saved every 30 seconds while editing. Save your project to keep a named copy.");

        // As with leaving: compared once the scene has settled, held while the prompt is up.
        bool proceed = false;
        if ((_pendingNew || !_pendingOpen.empty()) && !ImGui::IsPopupOpen(kReplacePrompt) && IsSettled(snapshot)) {
            if (IsProjectDirty(snapshot)) {
                ImGui::OpenPopup(kReplacePrompt);
            }
            else {
                proceed = true;
            }
        }
        CentreNextPopup();
        if (ImGui::BeginPopupModal(kReplacePrompt, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Save this project before replacing it?");
            if (ImGui::Button("Save and continue") && SaveProject(snapshot, false)) {
                proceed = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Discard changes")) {
                proceed = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                _pendingNew = false;
                _pendingOpen.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        if (proceed) {
            if (_pendingNew) {
                NewProject(snapshot);
                std::snprintf(_mapName, sizeof(_mapName), "%s", "untitled");
            }
            else {
                OpenProject(_pendingOpen, snapshot);
            }
            _pendingNew = false;
            _pendingOpen.clear();
        }

        ImGui::SeparatorText("Export for a server");
        ImGui::InputText("Resource name", _mapName, sizeof(_mapName));
        ImGui::InputText("Output directory", _exportDirectory, sizeof(_exportDirectory));
        if (ImGui::Button("Choose export directory...")) {
            BrowseDirectory(_exportDirectory[0] != '\0' ? _exportDirectory : MapFiles::Directory());
            ImGui::OpenPopup("Export folder");
        }
        RenderDirectoryPicker();
        ImGui::TextWrapped("Choose a local server folder or any staging folder to upload later. Your project stays separate.");
        if (ImGui::Button("Export world resource")) {
            if (!IsSettled(snapshot)) {
                _notice = "Finish the current edit before exporting.";
            }
            else if (_exportDirectory[0] == '\0') {
                _notice = "Enter an output directory first.";
            }
            else if (snapshot.level.empty()) {
                _notice = "Load a level before exporting.";
            }
            else {
                std::string error;
                const nlohmann::json resource = Shared::WorldBuilder::ExportResource(MapDocumentToJson(BuildDocument(snapshot)));
                if (!Shared::WorldBuilder::ValidateContent(resource, error)) {
                    _notice = error;
                }
                else {
                    const std::u8string utf8 = (std::filesystem::u8path(_exportDirectory) / (MapFiles::SanitizeName(_mapName) + ".world.json")).u8string();
                    const std::string path(utf8.begin(), utf8.end());
                    if (MapFiles::WritePath(path, resource.dump(2), error)) {
                        _notice = "Exported " + path + ". Add this file to mod.world_resources in server.json.";
                    }
                    else {
                        _notice = error;
                    }
                }
            }
        }
        ImGui::BeginDisabled(!IsSettled(snapshot));
        if (ImGui::Button("Copy whole project as API calls")) {
            const std::string script = Shared::WorldBuilder::ApiCalls(MapDocumentToJson(BuildDocument(snapshot)));
            ImGui::SetClipboardText(script.c_str());
            _notice = "Copied server API calls.";
        }
        ImGui::EndDisabled();
    }

    void MapEditor::RenderBlueprints(const EditorSnapshot &snapshot) {
        ImGui::TextWrapped("Select props and effects with Ctrl+click. A group moves as one; a blueprint saves that selection for other projects.");
        ImGui::InputText("Group name", _groupName, sizeof(_groupName));
        if (ImGui::Button("Group selection")) {
            Command command;
            command.kind  = CommandKind::GroupSelection;
            command.group = _groupName;
            Queue(std::move(command));
        }
        ImGui::SameLine();
        if (ImGui::Button("Ungroup")) {
            Command command;
            command.kind = CommandKind::GroupSelection;
            Queue(std::move(command));
        }

        ImGui::BeginDisabled(!IsSettled(snapshot));
        if (ImGui::Button("Save selection as blueprint...")) {
            std::string text;
            std::string error;
            if (!SerializeBlueprint(SelectedDocument(snapshot), text, error)) {
                _notice = error;
            }
            else {
                const std::string path = MapFiles::ChoosePath(true, true, MapFiles::Directory() + "/" + MapFiles::SanitizeName(_groupName) + ".blueprint.json", _notice);
                if (!path.empty() && MapFiles::WritePath(path, text, error)) {
                    _blueprintPath = path;
                    _notice        = "Saved blueprint " + path;
                }
                else if (!path.empty()) {
                    _notice = error;
                }
            }
        }
        ImGui::EndDisabled();

        if (ImGui::Button("Open blueprint...")) {
            const std::string path = MapFiles::ChoosePath(false, true, _blueprintPath, _notice);
            if (!path.empty()) {
                _blueprintPath = path;
            }
        }
        ImGui::TextWrapped("%s", _blueprintPath.c_str());

        ImGui::BeginDisabled(!snapshot.cameraHeld || !IsSettled(snapshot));
        const bool layersEditable = std::all_of(snapshot.layers->Layers().begin(), snapshot.layers->Layers().end(), [](const EditorLayer &layer) {
            return layer.visible && !layer.locked;
        });
        ImGui::BeginDisabled(!layersEditable);
        const bool placeBlueprint = !_blueprintPath.empty() && ImGui::Button("Place blueprint in front of camera");
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        if (placeBlueprint) {
            std::string text;
            std::string error;
            MapDocument document;
            if (!MapFiles::ReadPath(_blueprintPath, text, error) || !ParseBlueprint(text, document, error)) {
                _notice = error;
                return;
            }

            // Each placement is its own group, so two copies select apart.
            std::unordered_set<std::string> taken;
            for (const SceneRow &row : snapshot.rows) {
                taken.insert(row.group);
            }
            for (const EffectRow &row : snapshot.effectRows) {
                taken.insert(row.group);
            }
            for (const auto &[id, areaGroup] : _areas.Groups()) {
                taken.insert(areaGroup);
            }
            for (const auto &row : snapshot.lightRows) {
                taken.insert(row.group);
            }
            const std::string group = UniqueGroup(_groupName, taken);
            const SDK::Vec3 pivot   = KCDC::Common::ToNative(KCDC::Common::ToGlm(snapshot.camera.position) + KCDC::Common::ToGlm(Camera::PoseForward(snapshot.camera)) * 8.0F);
            OffsetDocument(document, pivot);
            for (DocumentObject &object : document.objects) {
                object.group = group;
            }
            for (auto &entry : document.lights) {
                entry.group = group;
            }
            for (DocumentEffect &effect : document.effects) {
                effect.group = group;
            }
            for (const auto &area : document.areas) {
                document.areaGroups[area.id] = group;
            }
            _areas.Import(document.areas, false, document.areaGroups);
            _patrols.Import(document.patrolRoutes, false);
            Command command;
            command.kind     = CommandKind::Import;
            command.document = std::make_shared<MapDocument>(std::move(document));
            Queue(std::move(command));
            _notice = "Placed blueprint. Click a member to select its group.";
        }
        if (ImGui::Button("Copy selection as API calls")) {
            CopySelection(snapshot);
        }
    }
} // namespace KCDC::Features::Prop
