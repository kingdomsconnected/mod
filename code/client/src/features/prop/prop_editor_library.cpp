#include "prop_editor_library.h"

#include "prop_editor_document.h"
#include "prop_editor_support.h"
#include "prop_editor_world_objects.h"

#include "shared/game/prop_models.h"
#include "shared/game/vfx_catalog.h"

#include <imgui.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string_view>

namespace KCDC::Features::Prop {
    namespace {
        constexpr std::size_t kMaxRecent = 32;

        /** `text` split on `separator`, without the last piece (the leaf itself). */
        std::vector<std::string> Parents(std::string_view text, char separator) {
            std::vector<std::string> segments;
            std::size_t start = 0;
            for (std::size_t at = text.find(separator); at != std::string_view::npos; at = text.find(separator, start)) {
                segments.emplace_back(text.substr(start, at - start));
                start = at + 1;
            }
            return segments;
        }

        std::vector<AssetLibraryItem> BuildProps() {
            constexpr std::string_view kPrefix = "objects/";

            std::vector<AssetLibraryItem> items;
            items.reserve(Shared::Game::g_PropModels.size());
            for (const auto &model : Shared::Game::g_PropModels) {
                std::string_view path = model.path;
                if (path.substr(0, kPrefix.size()) == kPrefix) {
                    path.remove_prefix(kPrefix.size());
                }
                items.push_back(AssetLibraryItem {std::string {model.name}, std::string {model.path}, Parents(path, '/'), {}});
            }
            // After every mesh, so a mesh's row is still its catalog index.
            for (const WorldObjectRow &row : WorldObjectRows()) {
                items.push_back(AssetLibraryItem {row.name, WorldObjectKey(row.kind, row.model), row.directories, row.detail});
            }
            return items;
        }

        std::vector<AssetLibraryItem> BuildEffects() {
            std::vector<AssetLibraryItem> items;
            items.reserve(Shared::Game::g_VfxEffects.size());
            for (const auto &effect : Shared::Game::g_VfxEffects) {
                const std::string_view name = effect.name;
                const std::size_t dot       = name.find_last_of('.');

                char detail[48];
                if (effect.viewDistance > 0.0F) {
                    std::snprintf(detail, sizeof(detail), "%s  %.0f m", effect.continuous ? "loop" : "once", effect.viewDistance);
                }
                else {
                    std::snprintf(detail, sizeof(detail), "%s", effect.continuous ? "loop" : "once");
                }
                items.push_back(AssetLibraryItem {std::string {dot == std::string_view::npos ? name : name.substr(dot + 1)}, std::string {name}, Parents(name, '.'), detail});
            }
            return items;
        }
    } // namespace

    AssetLibraryCatalog AssetLibraryCatalog::Props() {
        AssetLibraryCatalog catalog;
        catalog.build      = &BuildProps;
        catalog.rootName   = "objects";
        catalog.plural     = "meshes";
        catalog.prefsName  = "library.prefs";
        catalog.searchHint = "Search: barrel -broken  taverns/";
        // Level-tool pieces, pose references and test rows, not map content.
        catalog.devRoots = {"intermediates", "test_souradnice", "preproduction", "helpers", "ui", "particles", "default", "animation_props"};
        return catalog;
    }

    AssetLibraryCatalog AssetLibraryCatalog::Effects() {
        AssetLibraryCatalog catalog;
        catalog.build      = &BuildEffects;
        catalog.rootName   = "effects";
        catalog.plural     = "effects";
        catalog.prefsName  = "effects.prefs";
        catalog.searchHint = "Search: fire -big  smokes.";
        catalog.devRoots   = {"ui"};
        return catalog;
    }

    bool AssetLibrary::IsDevRoot(const std::string &segment) const {
        return std::find(_catalog.devRoots.begin(), _catalog.devRoots.end(), segment) != _catalog.devRoots.end();
    }

    bool AssetLibrary::IsValid(int index) const {
        return index >= 0 && static_cast<std::size_t>(index) < _items.size();
    }

    int AssetLibrary::IndexOfPath(const std::string &path) const {
        const auto found = _byPath.find(path);
        return found != _byPath.end() ? found->second : kNone;
    }

    int AssetLibrary::ChildNamed(int parent, const std::string &name) {
        for (const int child : _categories[static_cast<std::size_t>(parent)].children) {
            if (_categories[static_cast<std::size_t>(child)].name == name) {
                return child;
            }
        }

        Category category;
        category.name = name;
        category.dev  = _categories[static_cast<std::size_t>(parent)].dev || (parent == 0 && IsDevRoot(name));
        _categories.push_back(std::move(category));

        const int index = static_cast<int>(_categories.size()) - 1;
        _categories[static_cast<std::size_t>(parent)].children.push_back(index);
        return index;
    }

    void AssetLibrary::EnsureBuilt() {
        if (_built) {
            return;
        }
        _built = true;

        _items = _catalog.build != nullptr ? _catalog.build() : std::vector<AssetLibraryItem> {};
        _categories.clear();
        _categories.push_back(Category {_catalog.rootName, {}, {}, false});

        _keyFolders.assign(_items.size(), std::string {});
        for (std::size_t index = 0; index < _items.size(); ++index) {
            const int row = static_cast<int>(index);
            _byPath.emplace(_items[index].path, row);
            if (std::string model; SplitWorldObjectKey(_items[index].path, model) != PlacedKind::Prop) {
                for (const std::string &segment : _items[index].directories) {
                    _keyFolders[index] += segment + "/";
                }
            }

            int node = 0;
            _categories[0].items.push_back(row);
            for (const std::string &segment : _items[index].directories) {
                node = ChildNamed(node, segment);
                _categories[static_cast<std::size_t>(node)].items.push_back(row);
            }
        }

        for (Category &category : _categories) {
            std::sort(category.children.begin(), category.children.end(), [this](int left, int right) {
                return _categories[static_cast<std::size_t>(left)].name < _categories[static_cast<std::size_t>(right)].name;
            });
        }

        LoadPrefs();
    }

    const std::vector<AssetLibraryItem> &AssetLibrary::Items() {
        EnsureBuilt();
        return _items;
    }

    const AssetLibraryItem *AssetLibrary::Find(const std::string &path) {
        EnsureBuilt();
        const int index = IndexOfPath(path);
        return IsValid(index) ? &_items[index] : nullptr;
    }

    std::vector<int> AssetLibrary::Search(const std::string &query, bool includeDevelopment) {
        EnsureBuilt();
        const auto tokens = SearchTokens(query);
        std::vector<int> matches;
        for (std::size_t index = 0; index < _items.size(); ++index) {
            const auto &directories = _items[index].directories;
            if (!includeDevelopment && !directories.empty() && IsDevRoot(directories.front())) {
                continue;
            }
            if (Passes(static_cast<int>(index), tokens)) {
                matches.push_back(static_cast<int>(index));
            }
        }
        return matches;
    }

    std::vector<std::string> AssetLibrary::Favorites() {
        EnsureBuilt();
        std::vector<std::string> paths;
        for (const auto &path : _favorites) {
            if (IndexOfPath(path) != kNone) {
                paths.push_back(path);
            }
        }
        std::sort(paths.begin(), paths.end());
        return paths;
    }

    std::vector<std::string> AssetLibrary::Recent() {
        EnsureBuilt();
        std::vector<std::string> paths;
        for (const auto &path : _recent) {
            if (IndexOfPath(path) != kNone) {
                paths.push_back(path);
            }
        }
        return paths;
    }

    bool AssetLibrary::SetFavorite(const std::string &path, bool favorite) {
        EnsureBuilt();
        const int index = IndexOfPath(path);
        if (!IsValid(index)) {
            return false;
        }
        const bool changed = favorite ? _favorites.insert(path).second : _favorites.erase(path) != 0;
        if (changed) {
            SavePrefs();
        }
        return true;
    }

    void AssetLibrary::FavoriteFoldersChanged() {
        ++_favoritesRevision;
        ++_revision;
    }

    void AssetLibrary::SetFavoriteFolders(const FavoriteFolders &folders) {
        if (_favoriteFolders == folders) {
            return;
        }
        _favoriteFolders = folders;
        _folderError.clear();
        FavoriteFoldersChanged();
    }

    void AssetLibrary::RestoreFavoriteFolders(const FavoriteFolders &folders) {
        if (_favoriteFolders.Restore(folders)) {
            _folderError.clear();
            FavoriteFoldersChanged();
        }
    }

    std::uint32_t AssetLibrary::AddFavoriteFolder(const std::string &name, std::string &error) {
        const auto id = _favoriteFolders.Add(name, error);
        if (id != 0) {
            FavoriteFoldersChanged();
        }
        return id;
    }

    bool AssetLibrary::RenameFavoriteFolder(std::uint32_t id, const std::string &name, std::string &error) {
        const auto *folder = _favoriteFolders.Find(id);
        const bool changed = folder && folder->name != name;
        if (!_favoriteFolders.Rename(id, name, error)) {
            return false;
        }
        if (changed) {
            FavoriteFoldersChanged();
        }
        return true;
    }

    bool AssetLibrary::DeleteFavoriteFolder(std::uint32_t id, std::string &error) {
        if (!_favoriteFolders.Delete(id, error)) {
            return false;
        }
        FavoriteFoldersChanged();
        return true;
    }

    bool AssetLibrary::SetActiveFavoriteFolder(std::uint32_t id, std::string &error) {
        const bool changed = _favoriteFolders.ActiveId() != id;
        if (!_favoriteFolders.SetActive(id, error)) {
            return false;
        }
        if (changed) {
            FavoriteFoldersChanged();
        }
        return true;
    }

    bool AssetLibrary::SetProjectFavorite(const std::string &path, bool favorite, std::uint32_t folderId, std::string &error) {
        const bool changed = _favoriteFolders.Contains(path, folderId) != favorite;
        if (!_favoriteFolders.Set(path, favorite, folderId, error)) {
            return false;
        }
        if (changed) {
            FavoriteFoldersChanged();
        }
        return true;
    }

    bool AssetLibrary::MoveProjectFavorite(const std::string &path, std::uint32_t sourceId, std::uint32_t targetId, std::string &error) {
        if (!_favoriteFolders.Move(path, sourceId, targetId, error)) {
            return false;
        }
        if (sourceId != targetId) {
            FavoriteFoldersChanged();
        }
        return true;
    }

    bool AssetLibrary::NoteUsed(const std::string &path) {
        EnsureBuilt();
        const int index = IndexOfPath(path);
        if (!IsValid(index)) {
            return false;
        }
        NoteUsed(index);
        return true;
    }

    bool AssetLibrary::Passes(int index, const std::vector<std::string> &tokens) const {
        const AssetLibraryItem &item = _items[static_cast<std::size_t>(index)];
        return PassesSearch(tokens, {item.name, item.path, _keyFolders[static_cast<std::size_t>(index)]});
    }

    void AssetLibrary::RefreshMatches() {
        _matches.clear();
        const std::vector<std::string> tokens = SearchTokens(_search);

        const auto consider = [&](int index) {
            if (IsValid(index) && Passes(index, tokens)) {
                _matches.push_back(index);
            }
        };

        if (_scope == Scope::Favorites) {
            for (const std::string &path : _favoriteFolders.Find(_favoriteFolders.ActiveId())->paths) {
                consider(IndexOfPath(path));
            }
            std::sort(_matches.begin(), _matches.end(), [this](int left, int right) {
                return _items[static_cast<std::size_t>(left)].name < _items[static_cast<std::size_t>(right)].name;
            });
        }
        else if (_scope == Scope::Recent) {
            for (const std::string &path : _recent) {
                consider(IndexOfPath(path));
            }
        }
        else {
            const int root      = _scope == Scope::Category ? _category : 0;
            const bool devScope = _categories[static_cast<std::size_t>(root)].dev;
            for (const int index : _categories[static_cast<std::size_t>(root)].items) {
                // Development rows stay hidden unless their own folder is open.
                const std::vector<std::string> &directories = _items[static_cast<std::size_t>(index)].directories;
                if (!_showDev && !devScope && !directories.empty() && IsDevRoot(directories.front())) {
                    continue;
                }
                consider(index);
            }
        }

        _matchedSearch   = _search;
        _matchedScope    = _scope;
        _matchedCategory = _category;
        _matchedDev      = _showDev;
        _matchedRevision = _revision;
    }

    void AssetLibrary::RenderScopes() {
        const auto scopeRow = [this](const char *label, Scope scope, std::size_t count) {
            char text[96];
            std::snprintf(text, sizeof(text), "%s  (%zu)", label, count);
            if (ImGui::Selectable(text, _scope == scope)) {
                _scope = scope;
            }
        };

        scopeRow("Everything", Scope::Everything, _categories[0].items.size());
        scopeRow("Favorites in active folder", Scope::Favorites, _favoriteFolders.Find(_favoriteFolders.ActiveId())->paths.size());
        scopeRow("Recently placed", Scope::Recent, _recent.size());
        ImGui::Separator();
    }

    void AssetLibrary::RenderFavoriteFolders() {
        ImGui::TextUnformatted("Project favorites");
        ImGui::SetNextItemWidth(-1.0F);
        if (ImGui::BeginCombo("##favorite_folder", _favoriteFolders.Find(_favoriteFolders.ActiveId())->name.c_str())) {
            for (const auto &folder : _favoriteFolders.Folders()) {
                ImGui::PushID(static_cast<int>(folder.id));
                if (ImGui::Selectable(folder.name.c_str(), folder.id == _favoriteFolders.ActiveId())) {
                    SetActiveFavoriteFolder(folder.id, _folderError);
                    _scope = Scope::Favorites;
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        if (ImGui::SmallButton("New folder")) {
            _folderName[0] = '\0';
            _editingFolder = 0;
            _folderError.clear();
            ImGui::OpenPopup("Favorite folder name");
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(_favoriteFolders.ActiveId() == FavoriteFolders::kRoot);
        if (ImGui::SmallButton("Rename")) {
            _editingFolder = _favoriteFolders.ActiveId();
            std::snprintf(_folderName, sizeof(_folderName), "%s", _favoriteFolders.Find(_editingFolder)->name.c_str());
            _folderError.clear();
            ImGui::OpenPopup("Favorite folder name");
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Delete")) {
            _editingFolder = _favoriteFolders.ActiveId();
            _folderError.clear();
            ImGui::OpenPopup("Delete favorite folder?");
        }
        ImGui::EndDisabled();
        if (!_folderError.empty()) {
            ImGui::TextWrapped("%s", _folderError.c_str());
        }
        if (ImGui::BeginPopupModal("Favorite folder name", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::InputText("Name", _folderName, sizeof(_folderName));
            if (!_folderError.empty()) {
                ImGui::TextWrapped("%s", _folderError.c_str());
            }
            if (ImGui::Button("Save")) {
                const bool saved = _editingFolder == 0 ? AddFavoriteFolder(_folderName, _folderError) != 0 : RenameFavoriteFolder(_editingFolder, _folderName, _folderError);
                if (saved) {
                    _scope = Scope::Favorites;
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        if (ImGui::BeginPopupModal("Delete favorite folder?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            const auto *folder = _favoriteFolders.Find(_editingFolder);
            ImGui::TextWrapped("Delete folder '%s' and its favorites? Placed objects are unaffected.", folder ? folder->name.c_str() : "unavailable");
            if (!_folderError.empty()) {
                ImGui::TextWrapped("%s", _folderError.c_str());
            }
            if (ImGui::Button("Delete folder")) {
                if (DeleteFavoriteFolder(_editingFolder, _folderError)) {
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        ImGui::Separator();
    }

    void AssetLibrary::RenderTree(int node) {
        const Category &category = _categories[static_cast<std::size_t>(node)];
        if (category.dev && !_showDev) {
            return;
        }

        char label[160];
        std::snprintf(label, sizeof(label), "%s  (%zu)###cat%d", category.name.c_str(), category.items.size(), node);

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (category.children.empty()) {
            flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        }
        if (_scope == Scope::Category && _category == node) {
            flags |= ImGuiTreeNodeFlags_Selected;
        }

        if (category.dev) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        }
        const bool open = ImGui::TreeNodeEx(label, flags);
        if (category.dev) {
            ImGui::PopStyleColor();
        }
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
            _scope    = Scope::Category;
            _category = node;
        }

        if (open && !category.children.empty()) {
            for (const int child : category.children) {
                RenderTree(child);
            }
            ImGui::TreePop();
        }
    }

    void AssetLibrary::RenderList(float height) {
        if (!ImGui::BeginTable("##kcdc_library_list", 3, ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp, ImVec2(0.0F, height))) {
            return;
        }

        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 18.0F);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 92.0F);
        ImGui::TableHeadersRow();

        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(_matches.size()));
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const int index              = _matches[static_cast<std::size_t>(row)];
                const AssetLibraryItem &item = _items[static_cast<std::size_t>(index)];

                ImGui::PushID(index);
                ImGui::TableNextRow();

                ImGui::TableNextColumn();
                const bool favorite = IsFavorite(index);
                ImGui::PushStyleColor(ImGuiCol_Text, favorite ? ImVec4(1.0F, 0.8F, 0.25F, 1.0F) : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                if (ImGui::SmallButton(favorite ? "*" : ".")) {
                    ToggleFavorite(index);
                }
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
                    ImGui::SetTooltip("%s '%s'", favorite ? "Remove from" : "Add to", _favoriteFolders.Find(_favoriteFolders.ActiveId())->name.c_str());
                }

                ImGui::TableNextColumn();
                if (ImGui::Selectable(item.name.c_str(), index == _selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap)) {
                    _selected = index;
                    _picked   = true;
                }
                if (ImGui::IsItemHovered()) {
                    _hovered = index;
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                    ImGui::SetTooltip("%s\n\nClick to pick it up: a preview follows the cursor,\nand a click in the world puts one down.", item.path.c_str());
                }
                if (ImGui::BeginPopupContextItem("favorite_actions")) {
                    if (ImGui::MenuItem(favorite ? "Remove from active favorites folder" : "Add to active favorites folder")) {
                        ToggleFavorite(index);
                    }
                    if (ImGui::BeginMenu("Move favorite to", favorite && _favoriteFolders.Folders().size() > 1)) {
                        for (const auto &folder : _favoriteFolders.Folders()) {
                            ImGui::PushID(static_cast<int>(folder.id));
                            if (folder.id != _favoriteFolders.ActiveId() && ImGui::MenuItem(folder.name.c_str())) {
                                MoveProjectFavorite(item.path, _favoriteFolders.ActiveId(), folder.id, _folderError);
                            }
                            ImGui::PopID();
                        }
                        ImGui::EndMenu();
                    }
                    ImGui::EndPopup();
                }

                ImGui::TableNextColumn();
                SDK::Vec3 size {};
                if (SizeOf(index, size)) {
                    ImGui::TextDisabled("%.1f x %.1f x %.1f", size.x, size.y, size.z);
                }
                else if (!item.detail.empty()) {
                    ImGui::TextDisabled("%s", item.detail.c_str());
                }

                ImGui::PopID();
            }
        }

        ImGui::EndTable();
    }

    void AssetLibrary::Render(float height) {
        EnsureBuilt();
        _hovered = kNone;

        ImGui::SetNextItemWidth(-1.0F);
        ImGui::InputTextWithHint("##kcdc_library_search", _catalog.searchHint, _search, sizeof(_search));
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
            ImGui::SetTooltip("Every word must match the name or the path.\nA word starting with - must not.");
        }

        if (ImGui::Checkbox("Show development folders", &_showDev) && !_showDev && _scope == Scope::Category && _categories[static_cast<std::size_t>(_category)].dev) {
            _scope = Scope::Everything;
        }

        RenderFavoriteFolders();

        const float treeHeight = std::max(height * 0.38F, 90.0F);
        if (ImGui::BeginChild("##kcdc_library_tree", ImVec2(0.0F, treeHeight), ImGuiChildFlags_Borders)) {
            RenderScopes();
            for (const int child : _categories[0].children) {
                RenderTree(child);
            }
        }
        ImGui::EndChild();

        if (_matchedRevision != _revision || _matchedScope != _scope || _matchedCategory != _category || _matchedDev != _showDev || _matchedSearch != _search) {
            RefreshMatches();
        }

        const char *where = _scope == Scope::Favorites ? _favoriteFolders.Find(_favoriteFolders.ActiveId())->name.c_str() : _scope == Scope::Recent ? "recent" : _scope == Scope::Category ? _categories[static_cast<std::size_t>(_category)].name.c_str() : "the catalog";
        ImGui::TextDisabled("%zu %s in %s", _matches.size(), _catalog.plural, where);

        RenderList(std::max(height - treeHeight - ImGui::GetFrameHeightWithSpacing() * 6.0F, 120.0F));
    }

    bool AssetLibrary::TakePicked(int &index) {
        if (!_picked) {
            return false;
        }
        _picked = false;
        index   = _selected;
        return _selected != kNone;
    }

    void AssetLibrary::NoteUsed(int index) {
        EnsureBuilt();
        if (!IsValid(index)) {
            return;
        }

        const std::string &path = _items[static_cast<std::size_t>(index)].path;
        if (!_recent.empty() && _recent.front() == path) {
            return;
        }
        _recent.erase(std::remove(_recent.begin(), _recent.end(), path), _recent.end());
        _recent.push_front(path);
        while (_recent.size() > kMaxRecent) {
            _recent.pop_back();
        }
        ++_revision;
        SavePrefs();
    }

    void AssetLibrary::NoteSize(int index, const SDK::Vec3 &size) {
        if (index >= 0) {
            _sizes[index] = size;
        }
    }

    bool AssetLibrary::SizeOf(int index, SDK::Vec3 &size) const {
        const auto found = _sizes.find(index);
        if (found == _sizes.end()) {
            return false;
        }
        size = found->second;
        return true;
    }

    bool AssetLibrary::IsFavorite(int index) const {
        return IsValid(index) && _favoriteFolders.Contains(_items[static_cast<std::size_t>(index)].path, _favoriteFolders.ActiveId());
    }

    void AssetLibrary::ToggleFavorite(int index) {
        if (!IsValid(index)) {
            return;
        }
        const std::string &path = _items[static_cast<std::size_t>(index)].path;
        SetProjectFavorite(path, !IsFavorite(index), _favoriteFolders.ActiveId(), _folderError);
    }

    void AssetLibrary::LoadPrefs() {
        const std::string directory = MapFiles::Directory();
        if (directory.empty()) {
            return;
        }
        std::ifstream file(std::filesystem::path(directory) / _catalog.prefsName, std::ios::binary);
        if (!file) {
            return;
        }

        std::ostringstream buffer;
        buffer << file.rdbuf();
        const nlohmann::json root = nlohmann::json::parse(buffer.str(), nullptr, false);
        if (root.is_discarded() || !root.is_object()) {
            return;
        }

        if (const auto favorites = root.find("favorites"); favorites != root.end() && favorites->is_array()) {
            for (const auto &entry : *favorites) {
                if (entry.is_string()) {
                    _favorites.insert(entry.get<std::string>());
                }
            }
        }
        if (const auto recent = root.find("recent"); recent != root.end() && recent->is_array()) {
            for (const auto &entry : *recent) {
                if (entry.is_string() && _recent.size() < kMaxRecent) {
                    _recent.push_back(entry.get<std::string>());
                }
            }
        }
        if (const auto showDev = root.find("showDev"); showDev != root.end() && showDev->is_boolean()) {
            _showDev = showDev->get<bool>();
        }
    }

    void AssetLibrary::SavePrefs() const {
        const std::string directory = MapFiles::Directory();
        if (directory.empty()) {
            return;
        }

        nlohmann::json favorites = nlohmann::json::array();
        for (const std::string &entry : _favorites) {
            favorites.push_back(entry);
        }
        const nlohmann::json root {
            {"favorites", std::move(favorites)},
            {"recent", nlohmann::json(std::vector<std::string>(_recent.begin(), _recent.end()))},
            {"showDev", _showDev},
        };

        std::ofstream file(std::filesystem::path(directory) / _catalog.prefsName, std::ios::binary | std::ios::trunc);
        if (file) {
            file << root.dump(2);
        }
    }
} // namespace KCDC::Features::Prop
