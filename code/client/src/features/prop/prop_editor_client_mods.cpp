#include "prop_editor.h"

#include "core/application.h"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>

namespace KCDC::Features::Prop {
    void MapEditor::ListClientMods() {
        _clientModsListed = true;
        _clientModList.clear();
        _clientModListError.clear();
        if (_clientModsFolder[0] != '\0') {
            _clientModList = Shared::StreamedAssets::ListLocalMods(std::filesystem::u8path(_clientModsFolder), _clientModListError);
        }
    }

    void MapEditor::SetClientModsFolder(const std::string &folder) {
        std::snprintf(_clientModsFolder, sizeof(_clientModsFolder), "%s", folder.c_str());
        _clientModsListed = false;
    }

    bool MapEditor::ClientModsMatchLoaded() const {
        return _clientMods == Core::gApplication->GetStreamedAssetService().GetLocalMods();
    }

    bool MapEditor::ApplyClientMods(const EditorSnapshot &snapshot, std::string &error) {
        if (_projectPath.empty() || IsProjectDirty(snapshot)) {
            error = "Save the project first: applying mods reloads the level.";
            return false;
        }
        const std::string folder            = _clientModsFolder;
        const std::vector<std::string> mods = _clientMods;
        const std::string project           = _projectPath;
        if (!Core::gApplication->RequestOfflineBuilderLevel(snapshot.level, error)) {
            return false;
        }
        Core::gApplication->SetOfflineBuilderMods(folder, mods);
        RequestProject(project);
        return true;
    }

    void MapEditor::RenderModsTab(const EditorSnapshot &snapshot) {
        if (!Core::gApplication->IsOfflineBuilderReady()) {
            ImGui::TextWrapped("In a session, the server decides which client mods are loaded.");
            return;
        }
        ImGui::TextWrapped("Select your server's resources folder, the one that holds the client mods. Then tick the mods this project needs. They load the same way a server streams them to players.");
        ImGui::InputText("Resources folder", _clientModsFolder, sizeof(_clientModsFolder));
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            _clientModsListed = false;
        }
        if (ImGui::Button("Choose resources folder...")) {
            _directoryTarget = _clientModsFolder;
            BrowseDirectory(_clientModsFolder[0] != '\0' ? _clientModsFolder : MapFiles::Directory());
            ImGui::OpenPopup(kDirectoryPicker);
        }
        RenderDirectoryPicker();
        ImGui::SameLine();
        if (ImGui::Button("Refresh list")) {
            _clientModsListed = false;
        }
        if (!_clientModsListed) {
            ListClientMods();
        }
        if (!_clientModListError.empty()) {
            ImGui::TextWrapped("%s", _clientModListError.c_str());
        }

        ImGui::SeparatorText("Client mods");
        if (_clientModList.empty() && _clientModListError.empty()) {
            ImGui::TextDisabled(_clientModsFolder[0] == '\0' ? "No folder selected." : "No client mods in this folder.");
        }
        for (const Shared::StreamedAssets::LocalMod &mod : _clientModList) {
            bool ticked = std::binary_search(_clientMods.begin(), _clientMods.end(), mod.resource);
            ImGui::PushID(mod.resource.c_str());
            if (ImGui::Checkbox(mod.resource.c_str(), &ticked)) {
                if (ticked) {
                    _clientMods.insert(std::lower_bound(_clientMods.begin(), _clientMods.end(), mod.resource), mod.resource);
                }
                else {
                    _clientMods.erase(std::remove(_clientMods.begin(), _clientMods.end(), mod.resource), _clientMods.end());
                }
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%s  %.0f MB", mod.version.c_str(), static_cast<double>(mod.bytes) / (1024.0 * 1024.0));
            if (!mod.risky.empty()) {
                std::string asks;
                for (const std::string &name : mod.risky) {
                    asks += (asks.empty() ? "" : ", ") + name;
                }
                ImGui::TextWrapped("Its scripts ask for %s. Ticking it accepts that.", asks.c_str());
            }
            ImGui::PopID();
        }
        for (const std::string &name : _clientMods) {
            const bool found = std::any_of(_clientModList.begin(), _clientModList.end(), [&name](const Shared::StreamedAssets::LocalMod &mod) { return mod.resource == name; });
            if (!found && _clientModsListed) {
                ImGui::TextWrapped("%s is ticked but not in this folder; it will not load.", name.c_str());
            }
        }

        ImGui::Separator();
        const bool matches = ClientModsMatchLoaded();
        ImGui::TextUnformatted(matches ? "The ticked mods are loaded." : "The level needs reloading to use the ticked mods.");
        for (const std::string &warning : Core::gApplication->GetStreamedAssetService().GetLocalModWarnings()) {
            ImGui::TextWrapped("%s", warning.c_str());
        }
        ImGui::BeginDisabled(matches || !IsSettled(snapshot));
        if (ImGui::Button("Save and reload the level")) {
            std::string error;
            if (SaveProject(snapshot, false) && !ApplyClientMods(snapshot, error)) {
                _notice = error;
            }
        }
        ImGui::EndDisabled();
        ImGui::TextDisabled("Reloading keeps your saved project and restarts its scene.");
    }
} // namespace KCDC::Features::Prop
