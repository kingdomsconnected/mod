#include "prop_editor_areas.h"
#include "prop_editor.h"
#include "prop_editor_gizmo.h"
#include "prop_editor_support.h"
#include "shared/features/area/area_wireframe.h"
#include "shared/features/world_builder/world_document.h"

// imgui.h first: ImGuizmo.h uses its types without including it.
#include <imgui.h>

#include <ImGuizmo.h>

#include "common/module_support.h"
#include "shared/game/area_catalog.h"
#include "shared/pose_math.h"
#include "shared/scripting/level_guid.h"

#include <utils/string_utils.h>

#include <nlohmann/json.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string_view>
#include <unordered_set>

namespace KCDC::Features::Prop {
    namespace {
        constexpr ImU32 kTriggerAreaColour = IM_COL32(90, 200, 255, 150);
        constexpr ImU32 kUnionColour       = IM_COL32(190, 130, 255, 170);
        constexpr ImU32 kSmartAreaColour   = IM_COL32(255, 190, 80, 150);
        constexpr ImU32 kAuthoredColour    = IM_COL32(110, 230, 120, 200);
        constexpr ImU32 kSelectedColour    = IM_COL32(255, 255, 255, 240);
        constexpr ImU32 kDoorColour        = IM_COL32(255, 120, 90, 230);
        const ImVec4 kErrorText {1.0F, 0.45F, 0.35F, 1.0F};
        const ImVec4 kWarnText {0.9F, 0.8F, 0.4F, 1.0F};

        constexpr float kMarkerRadius = 4.0F;
        constexpr float kPickRadius   = 10.0F;
        constexpr float kMaxRadius    = 250.0F;

        // The nearby list is re-sorted once the camera has moved this far.
        constexpr float kNearbyRefreshDistance = 2.0F;
        constexpr std::size_t kMaxListed       = 400;

        ImU32 ColourOf(Shared::Game::LevelAreaClass areaClass) {
            switch (areaClass) {
            case Shared::Game::LevelAreaClass::TriggerArea: return kTriggerAreaColour;
            case Shared::Game::LevelAreaClass::AreaUnion: return kUnionColour;
            case Shared::Game::LevelAreaClass::SmartAreaShape: return kSmartAreaColour;
            }
            return kTriggerAreaColour;
        }

        const Shared::Game::LevelArea &LevelAreaAt(int index) {
            return Shared::Game::g_LevelAreas[static_cast<std::size_t>(index)];
        }

        /** A world point on screen, and whether it is in front of the camera at all. */
        struct ScreenPoint {
            ImVec2 at;
            bool visible = false;
        };

        ScreenPoint Project(const Camera::CameraPose &camera, const glm::vec3 &point) {
            float ndcX = 0.0F, ndcY = 0.0F;
            if (!Camera::ProjectToNdc(camera, KCDC::Common::ToNative(point), ndcX, ndcY)) {
                return {};
            }
            return ScreenPoint {NdcToScreen(*ImGui::GetMainViewport(), ndcX, ndcY), true};
        }

        /**
         * A polygon's prism: a ring at `bottom` and `top` with the corners
         * joined, or, `open`, a single ring at the corners' own heights for a
         * drawn shape with no height.
         */
        template <typename Point>
        void Prism(Gizmos::Drawing &drawing, const Point *points, std::size_t count, bool open, float bottom, float top) {
            for (std::size_t index = 0; index < count; ++index) {
                const std::size_t next = (index + 1) % count;
                const glm::vec3 low(points[index].x, points[index].y, open ? points[index].z : bottom);
                const glm::vec3 lowNext(points[next].x, points[next].y, open ? points[next].z : bottom);
                drawing.segments.push_back({low, lowNext});
                if (!open) {
                    const glm::vec3 high(points[index].x, points[index].y, top);
                    drawing.segments.push_back({high, glm::vec3(points[next].x, points[next].y, top)});
                    drawing.segments.push_back({low, high, 0.6F});
                }
            }
        }

        /** Where a level area's marker sits: its middle, on its ceiling. */
        glm::vec3 LevelAnchor(const Shared::Game::LevelArea &area) {
            glm::vec3 anchor = Shared::Game::LevelAreaCentre(area);
            anchor.z         = area.boundsMax.z;
            return anchor;
        }

        /** `a, b ,c` -> {a, b, c}. */
        std::vector<std::string> SplitList(std::string_view text) {
            std::vector<std::string> items;
            while (!text.empty()) {
                const std::size_t comma = text.find(',');
                std::string item        = Framework::Utils::StringUtils::Trim(text.substr(0, comma));
                if (!item.empty()) {
                    items.push_back(std::move(item));
                }
                text.remove_prefix(comma == std::string_view::npos ? text.size() : comma + 1);
            }
            return items;
        }

        std::string JoinList(const std::vector<std::string> &items) {
            std::string joined;
            for (const std::string &item : items) {
                joined += (joined.empty() ? "" : ", ") + item;
            }
            return joined;
        }
    } // namespace

    void EditorAreas::BeginFrame() {
        _tabVisible = false;
        _markers.clear();
        _handleMarkers.clear();
        _edgeMarkers.clear();
        _clickHandled = false;
    }

    void EditorAreas::RefreshLevel(const std::string &level) {
        if (level == _level) {
            return;
        }
        _level = level;
        _levelRows.clear();
        _levelByGuid.clear();
        _nearby.clear();
        _nearbyRadius  = -1.0F;
        _selectedLevel = -1;
        _selectedDoors.clear();
        for (std::size_t index = 0; index < Shared::Game::g_LevelAreas.size(); ++index) {
            if (level == Shared::Game::g_LevelAreas[index].level) {
                _levelRows.push_back(static_cast<std::uint32_t>(index));
                _levelByGuid.emplace(Shared::Game::g_LevelAreas[index].guid, static_cast<std::uint32_t>(index));
            }
        }
    }

    void EditorAreas::RefreshNearby(const Camera::CameraPose &camera) {
        const glm::vec3 from = KCDC::Common::ToGlm(camera.position);
        if (_nearbyRadius == _radius && glm::length(from - KCDC::Common::ToGlm(_nearbyFrom)) < kNearbyRefreshDistance) {
            return;
        }
        _nearbyFrom   = camera.position;
        _nearbyRadius = _radius;

        std::vector<std::pair<float, std::uint32_t>> scored;
        for (const std::uint32_t index : _levelRows) {
            const Shared::Game::LevelArea &area = Shared::Game::g_LevelAreas[index];
            const float distance                = Shared::Game::DistanceToBox(from, Shared::Game::ToVec3(area.boundsMin), Shared::Game::ToVec3(area.boundsMax));
            if (distance <= _radius) {
                scored.emplace_back(distance, index);
            }
        }
        std::sort(scored.begin(), scored.end());
        _nearby.clear();
        _nearby.reserve(scored.size());
        for (const auto &entry : scored) {
            _nearby.push_back(entry.second);
        }
    }

    bool EditorAreas::IsShown(const Shared::Game::LevelArea &area) const {
        const bool classShown         = area.areaClass == Shared::Game::LevelAreaClass::TriggerArea ? _showTriggerAreas : area.areaClass == Shared::Game::LevelAreaClass::AreaUnion ? _showUnions : _showSmartAreas;
        const std::string_view filter = _filter;
        return classShown && (filter.empty() || Framework::Utils::StringUtils::ContainsIgnoreCase(area.name, filter) || Framework::Utils::StringUtils::ContainsIgnoreCase(area.labels, filter) || Framework::Utils::StringUtils::ContainsIgnoreCase(area.location, filter));
    }

    void EditorAreas::SelectLevel(int levelIndex) {
        ClearHandle();
        if (!_selectedAreas.empty()) {
            ++_selectionRevision;
        }
        _selectedAreas.clear();
        _selectedLevel = levelIndex;

        // The door catalog is a linear search, so the links are resolved here rather than every frame.
        _selectedDoors.clear();
        const Shared::Game::LevelArea &area = LevelAreaAt(levelIndex);
        for (std::uint32_t index = 0; index < area.doorCount; ++index) {
            const Shared::Game::LevelAreaDoor &link = Shared::Game::g_LevelAreaDoors[area.firstDoor + index];
            _selectedDoors.push_back(LinkedDoor {Shared::Game::FindDoorPlacement(area.level, link.doorGuid), link.link, KCDC::Scripting::FormatLevelGuid(link.doorGuid)});
        }
    }

    bool EditorAreas::IsSelected(int index) const {
        return std::find(_selectedAreas.begin(), _selectedAreas.end(), index) != _selectedAreas.end();
    }

    void EditorAreas::SelectAuthored(int authored, bool toggle) {
        ClearHandle();
        _groundPlacementRequested = false;
        const auto previous = _selectedAreas;
        if (!toggle) {
            _selectedAreas.clear();
        }
        const auto found = std::find(_selectedAreas.begin(), _selectedAreas.end(), authored);
        if (found != _selectedAreas.end()) {
            _selectedAreas.erase(found);
        }
        else if (authored >= 0 && static_cast<std::size_t>(authored) < _authored.size()) {
            _selectedAreas.push_back(authored);
        }
        if (_selectedAreas != previous) {
            ++_selectionRevision;
        }
        _selectedLevel = -1;
        _selectedDoors.clear();
        LoadDrafts();
    }

    void EditorAreas::ClearHandle() {
        SelectHandle({});
    }

    void EditorAreas::SelectHandle(AreaHandle handle) {
        FinishReleasedGizmoDrag(true);
        _handle     = handle;
        _gizmoScale = glm::vec3(1.0F);
        _handleMarkers.clear();
    }

    void EditorAreas::LoadDrafts() {
        if (_selectedAreas.empty()) {
            return;
        }
        const auto &area = *Selected();
        std::snprintf(_idDraft, sizeof(_idDraft), "%s", area.id.c_str());
        std::snprintf(_nameDraft, sizeof(_nameDraft), "%s", area.name.c_str());
        std::snprintf(_labelsDraft, sizeof(_labelsDraft), "%s", JoinList(area.labels).c_str());
        const auto group = _groups.find(area.id);
        std::snprintf(_groupDraft, sizeof(_groupDraft), "%s", group != _groups.end() ? group->second.c_str() : "");
        for (const int index : _selectedAreas) {
            const auto other = _groups.find(_authored[index].id);
            if (other == _groups.end() || other->second != _groupDraft) {
                _groupDraft[0] = '\0';
                break;
            }
        }
        std::snprintf(_metadataDraft, sizeof(_metadataDraft), "%s", nlohmann::json::parse(area.metadataJson, nullptr, false).dump(2).c_str());
        _metadataInvalid = false;
    }

    std::string EditorAreas::UniqueId(const char *stem) const {
        for (int suffix = 1;; ++suffix) {
            std::string id = std::string(stem) + "_" + std::to_string(suffix);
            if (std::none_of(_authored.begin(), _authored.end(), [&id](const Shared::Game::AreaDefinition &area) {
                    return area.id == id;
                })) {
                return id;
            }
        }
    }

    bool EditorAreas::ValidateDefinition(const Shared::Game::AreaDefinition &area, std::string &error) {
        if (area.id.empty() || area.id.find('\0') != std::string::npos || KCDC::Scripting::ParseLevelGuid(area.id) != 0) {
            error = "Area ID must be non-empty and must not be a level GUID.";
            return false;
        }
        if (!area.shape.IsValid() || !Shared::Game::IsUsableRotation(area.shape.rotation)) {
            error = "Invalid area shape or rotation.";
            return false;
        }
        if (!area.collisionOptions.IsValid()) {
            error = Shared::Game::AreaCollisionOptions::ThicknessRangeError();
            return false;
        }
        if (area.collision) {
            Shared::Game::AreaCollisionMesh mesh;
            if (!Shared::Game::BuildAreaCollisionMesh(area.shape, area.collisionOptions, mesh, error)) {
                return false;
            }
        }
        if (!nlohmann::json::parse(area.metadataJson, nullptr, false).is_object()) {
            error = "Area metadata must be a JSON object.";
            return false;
        }
        return true;
    }

    const Shared::Game::AreaDefinition *EditorAreas::Find(const std::string &id) const {
        const auto found = std::find_if(_authored.begin(), _authored.end(), [&](const auto &area) {
            return area.id == id;
        });
        return found == _authored.end() ? nullptr : &*found;
    }

    std::vector<std::string> EditorAreas::SelectedIds() const {
        std::vector<std::string> ids;
        for (const auto index : _selectedAreas) {
            ids.push_back(_authored[index].id);
        }
        return ids;
    }

    bool EditorAreas::Select(const std::vector<std::string> &ids, std::string &error) {
        std::vector<int> indices;
        for (const auto &id : ids) {
            const auto *area = Find(id);
            if (!area) {
                error = "Area not found: " + id;
                return false;
            }
            const int index = static_cast<int>(area - _authored.data());
            if (std::find(indices.begin(), indices.end(), index) == indices.end()) {
                indices.push_back(index);
            }
        }
        SelectAuthored(-1);
        _selectedAreas = std::move(indices);
        if (!_selectedAreas.empty()) {
            ++_selectionRevision;
        }
        LoadDrafts();
        error.clear();
        return true;
    }

    bool EditorAreas::Create(const Shared::Game::AreaDefinition &area, std::string &error) {
        if (!ValidateDefinition(area, error)) {
            return false;
        }
        if (Find(area.id)) {
            error = "Area ID already exists: " + area.id;
            return false;
        }
        _authored.push_back(area);
        ContentChanged();
        error.clear();
        return true;
    }

    bool EditorAreas::Update(const std::string &id, const Shared::Game::AreaDefinition &area, std::string &error) {
        const auto *found = Find(id);
        if (!found) {
            error = "Area not found: " + id;
            return false;
        }
        if (!ValidateDefinition(area, error)) {
            return false;
        }
        if (area.id != id && Find(area.id)) {
            error = "Area ID already exists: " + area.id;
            return false;
        }
        const auto index = found - _authored.data();
        // Copy first: callers may pass a reference to another authored area.
        auto replacement = area;
        if (replacement.id != id) {
            if (auto group = _groups.extract(id); !group.empty()) {
                group.key() = replacement.id;
                _groups.insert(std::move(group));
            }
        }
        _authored[index] = std::move(replacement);
        ContentChanged();
        ClearHandle();
        LoadDrafts();
        error.clear();
        return true;
    }

    bool EditorAreas::Remove(const std::string &id, std::string &error) {
        const auto *found = Find(id);
        if (!found) {
            error = "Area not found: " + id;
            return false;
        }
        auto selected = SelectedIds();
        selected.erase(std::remove(selected.begin(), selected.end(), id), selected.end());
        _groups.erase(id);
        _authored.erase(_authored.begin() + (found - _authored.data()));
        ContentChanged();
        _markers.clear();
        _edgeMarkers.clear();
        return Select(selected, error);
    }

    bool EditorAreas::Duplicate(const std::string &id, std::string &createdId, std::string &error) {
        const auto *found = Find(id);
        if (!found) {
            error = "Area not found: " + id;
            return false;
        }
        auto copy = *found;
        copy.id = UniqueId(id.c_str());
        const auto group = _groups.find(id);
        const std::string groupName = group == _groups.end() ? "" : group->second;
        if (!Create(copy, error)) {
            return false;
        }
        if (!groupName.empty()) {
            _groups[copy.id] = groupName;
        }
        createdId = copy.id;
        return true;
    }

    bool EditorAreas::SetGroup(const std::vector<std::string> &ids, const std::string &group, std::string &error) {
        for (const auto &id : ids) {
            if (!Find(id)) {
                error = "Area not found: " + id;
                return false;
            }
        }
        for (const auto &id : ids) {
            if (group.empty()) {
                _groups.erase(id);
            }
            else {
                _groups[id] = group;
            }
        }
        if (!ids.empty()) {
            ContentChanged();
        }
        _groupFilter.clear();
        LoadDrafts();
        error.clear();
        return true;
    }

    bool EditorAreas::ReplaceState(const std::vector<Shared::Game::AreaDefinition> &areas, const std::unordered_map<std::string, std::string> &groups, const std::vector<std::string> &selectedIds, std::string &error) {
        std::unordered_set<std::string> ids;
        for (const auto &area : areas) {
            if (!ValidateDefinition(area, error)) {
                return false;
            }
            if (!ids.insert(area.id).second) {
                error = "Duplicate area ID: " + area.id;
                return false;
            }
        }
        for (const auto &[id, group] : groups) {
            if (!ids.contains(id) || group.empty()) {
                error = "Area groups must reference existing areas and have non-empty names.";
                return false;
            }
        }
        for (const auto &id : selectedIds) {
            if (!ids.contains(id)) {
                error = "Selected area not found: " + id;
                return false;
            }
        }
        _authored = areas;
        _groups = groups;
        ContentChanged();
        _groupFilter.clear();
        _markers.clear();
        _edgeMarkers.clear();
        return Select(selectedIds, error);
    }

    void EditorAreas::Import(const std::vector<Shared::Game::AreaDefinition> &areas, bool replace, const std::unordered_map<std::string, std::string> &groups) {
        const bool changesContent = (replace && (!_authored.empty() || !_groups.empty())) || !areas.empty();
        if (replace) {
            SelectAuthored(-1);
            _authored.clear();
            _groups.clear();
            _groupFilter.clear();
        }
        for (Shared::Game::AreaDefinition area : areas) {
            const auto group = groups.find(area.id);
            // Two maps can both hold a `market`; the second one gets a free id rather than shadowing the first.
            if (std::any_of(_authored.begin(), _authored.end(), [&area](const Shared::Game::AreaDefinition &own) {
                    return own.id == area.id;
                })) {
                area.id = UniqueId(area.id.c_str());
            }
            if (group != groups.end() && !group->second.empty()) {
                _groups[area.id] = group->second;
            }
            _authored.push_back(std::move(area));
        }
        if (changesContent) {
            ContentChanged();
        }
        LoadDrafts();
    }

    void EditorAreas::RenderTab(const EditorSnapshot &snapshot, std::string &notice, const EditorViewport &viewport) {
        _tabVisible = true;
        RefreshLevel(snapshot.level);
        if (snapshot.cameraHeld) {
            RefreshNearby(snapshot.camera);
        }

        if (ImGui::CollapsingHeader("Game areas", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Show", &_showLevel);
            ImGui::SameLine();
            ImGui::Checkbox("Trigger", &_showTriggerAreas);
            ImGui::SameLine();
            ImGui::Checkbox("Union", &_showUnions);
            ImGui::SameLine();
            ImGui::Checkbox("Smart", &_showSmartAreas);
            ImGui::SetNextItemWidth(-1.0F);
            ImGui::SliderFloat("##kcdc_area_radius", &_radius, 5.0F, kMaxRadius, "within %.0f m of the camera");
            ImGui::SetNextItemWidth(-1.0F);
            ImGui::InputTextWithHint("##kcdc_area_filter", "filter by name, label or location", _filter, sizeof(_filter));
            RenderLevelList();
            RenderLevelInspector(notice);
        }

        if (ImGui::CollapsingHeader("Drawn areas", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::Checkbox("Show drawn areas", &_showAuthored) && !_showAuthored) {
                ClearHandle();
            }
            ImGui::SameLine();
            ImGui::Checkbox("Show collision", &_showCollision);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Shades the walls, and any floor or ceiling, that the selected areas' collision builds.");
            }
            RenderAuthoredList(snapshot, viewport);
            RenderSelection();
            RenderAuthoredInspector();
            RenderExport(notice);
        }
    }

    void EditorAreas::RenderLevelList() {
        std::size_t listed = 0;
        if (ImGui::BeginChild("##kcdc_area_level_list", ImVec2(0.0F, 170.0F), ImGuiChildFlags_Borders)) {
            if (_levelRows.empty()) {
                ImGui::TextDisabled("This level has no areas in the catalog.");
            }
            for (const std::uint32_t index : _nearby) {
                const Shared::Game::LevelArea &area = Shared::Game::g_LevelAreas[index];
                if (!IsShown(area)) {
                    continue;
                }
                if (++listed > kMaxListed) {
                    ImGui::TextDisabled("... narrow the radius or the filter to see more.");
                    break;
                }
                ImGui::PushID(static_cast<int>(index));
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(ColourOf(area.areaClass) | IM_COL32_A_MASK));
                const std::string row = std::string(area.location[0] != '\0' ? area.location : area.name) + (area.labels[0] != '\0' ? "  [" + std::string(area.labels) + "]" : "");
                if (ImGui::Selectable(row.c_str(), _selectedLevel == static_cast<int>(index))) {
                    SelectLevel(static_cast<int>(index));
                }
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
                    ImGui::SetTooltip("%s\n%s", Shared::Game::LevelAreaClassName(area.areaClass), area.name);
                }
                ImGui::PopID();
            }
        }
        ImGui::EndChild();
    }

    void EditorAreas::RenderLevelInspector(std::string &notice) {
        if (_selectedLevel < 0) {
            ImGui::TextDisabled("Select an area, in the list or by its marker in the world.");
            return;
        }
        const Shared::Game::LevelArea &area = LevelAreaAt(_selectedLevel);
        const std::string guid              = KCDC::Scripting::FormatLevelGuid(area.guid);

        ImGui::SeparatorText(Shared::Game::LevelAreaClassName(area.areaClass));
        ImGui::TextWrapped("%s", area.name);
        ImGui::Text("GUID %s", guid.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Copy GUID")) {
            ImGui::SetClipboardText(guid.c_str());
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
            ImGui::SetTooltip("Area.getById(\"%s\") finds it in a script.", guid.c_str());
        }
        ImGui::TextWrapped("Labels: %s", area.labels[0] != '\0' ? area.labels : "none");
        if (area.location[0] != '\0') {
            ImGui::Text("Location: %s", area.location);
        }
        if (area.areaClass != Shared::Game::LevelAreaClass::AreaUnion) {
            ImGui::Text("%u corners, %s", area.pointCount, area.height > 0.0F ? (std::to_string(static_cast<int>(std::lround(area.height))) + " m high").c_str() : "no ceiling");
        }

        const bool copyDefinition = ImGui::Button("Copy game area definitions");
        const bool createCopy     = ImGui::Button("Create editable copy");
        if (copyDefinition || createCopy) {
            std::vector<Shared::Game::AreaDefinition> copies;
            std::size_t skipped = 0;
            CopyLevelArea(_selectedLevel, copies, skipped);
            if (copyDefinition) {
                ImGui::SetClipboardText(Shared::Game::SerializeAreaDefinitions(copies).c_str());
            }
            else if (!copies.empty()) {
                Import(copies, false);
                _groupFilter.clear();
                SelectAuthored(static_cast<int>(_authored.size() - copies.size()));
            }
            notice = std::string(copyDefinition ? "Copied " : "Created ") + std::to_string(copies.size()) + " custom area definition(s).";
            if (skipped != 0) {
                notice += " Skipped " + std::to_string(skipped) + " polygons outside custom area limits.";
            }
            if (createCopy) {
                return;
            }
        }

        if (area.memberCount != 0 && ImGui::TreeNode("##kcdc_area_members", "%u member area%s", area.memberCount, area.memberCount == 1 ? "" : "s")) {
            for (std::uint32_t index = 0; index < area.memberCount; ++index) {
                const std::uint64_t memberGuid = Shared::Game::g_LevelAreaMembers[area.firstMember + index];
                const auto member              = _levelByGuid.find(memberGuid);
                if (member != _levelByGuid.end() && ImGui::Selectable(KCDC::Scripting::FormatLevelGuid(memberGuid).c_str())) {
                    SelectLevel(static_cast<int>(member->second));
                }
            }
            ImGui::TreePop();
        }

        if (!_selectedDoors.empty() && ImGui::TreeNodeEx("##kcdc_area_doors", ImGuiTreeNodeFlags_DefaultOpen, "%zu linked door%s", _selectedDoors.size(), _selectedDoors.size() == 1 ? "" : "s")) {
            for (std::size_t index = 0; index < _selectedDoors.size(); ++index) {
                const LinkedDoor &door = _selectedDoors[index];
                ImGui::PushID(static_cast<int>(index));
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kDoorColour), "%s", Shared::Game::AreaDoorLinkName(door.link));
                ImGui::SameLine();
                ImGui::TextUnformatted(door.guid.c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton("Copy")) {
                    ImGui::SetClipboardText(door.guid.c_str());
                }
                if (door.placement != nullptr && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
                    ImGui::SetTooltip("%s", door.placement->name);
                }
                ImGui::PopID();
            }
            ImGui::TreePop();
        }
    }

    void EditorAreas::CopyLevelArea(int index, std::vector<Shared::Game::AreaDefinition> &copies, std::size_t &skipped) const {
        std::vector<int> pending {index};
        std::unordered_set<int> visited;
        while (!pending.empty()) {
            const int current = pending.back();
            pending.pop_back();
            if (!visited.insert(current).second) {
                continue;
            }
            const auto &source = LevelAreaAt(current);
            if (source.areaClass == Shared::Game::LevelAreaClass::AreaUnion) {
                for (std::uint32_t member = 0; member < source.memberCount; ++member) {
                    const auto found = _levelByGuid.find(Shared::Game::g_LevelAreaMembers[source.firstMember + member]);
                    if (found != _levelByGuid.end()) {
                        pending.push_back(static_cast<int>(found->second));
                    }
                    else {
                        ++skipped;
                    }
                }
                continue;
            }
            Shared::Game::AreaDefinition area;
            const auto id = UniqueId(("area_" + KCDC::Scripting::FormatLevelGuid(source.guid)).c_str());
            if (EditableLevelArea(source, std::span(Shared::Game::g_LevelAreaPoints).subspan(source.firstPoint, source.pointCount), id, area)) {
                copies.push_back(std::move(area));
            }
            else {
                ++skipped;
            }
        }
    }

    void EditorAreas::RenderAuthoredList(const EditorSnapshot &snapshot, const EditorViewport &viewport) {
        // New areas start where the cursor last rested on the world, or a few metres ahead of the camera before it has.
        const SDK::Vec3 ahead = Camera::PoseForward(snapshot.camera);
        const glm::vec3 where = _hasWorldPoint ? KCDC::Common::ToGlm(_lastWorldPoint) : KCDC::Common::ToGlm(snapshot.camera.position) + KCDC::Common::ToGlm(ahead) * 8.0F;
        const auto create     = [&](Shared::Game::AreaShapeType type, const char *stem) {
            Shared::Game::AreaDefinition area;
            area.id             = UniqueId(stem);
            area.name           = area.id;
            area.shape.type     = type;
            area.shape.position = where;
            area.shape.boxMin   = glm::vec3(-4.0F, -4.0F, 0.0F);
            area.shape.boxMax   = glm::vec3(4.0F, 4.0F, 4.0F);
            area.shape.points   = {{-4.0F, -4.0F, 0.0F}, {4.0F, -4.0F, 0.0F}, {4.0F, 4.0F, 0.0F}, {-4.0F, 4.0F, 0.0F}};
            area.shape.height   = 4.0F;
            area.shape.radius   = 4.0F;
            if (viewport.snapTranslate) {
                area.shape.position = glm::round(area.shape.position / viewport.snapTranslateStep) * viewport.snapTranslateStep;
            }
            if (!_groupFilter.empty()) {
                _groups[area.id] = _groupFilter;
            }
            _authored.push_back(std::move(area));
            ContentChanged();
            SelectAuthored(static_cast<int>(_authored.size()) - 1);
            _groundPlacementRequested = ImGui::GetIO().KeyShift;
        };

        const float third = (ImGui::GetContentRegionAvail().x - 8.0F) / 3.0F;
        if (ImGui::Button("+ Box", ImVec2(third, 0.0F))) {
            create(Shared::Game::AreaShapeType::Box, "box");
        }
        ImGui::SameLine();
        if (ImGui::Button("+ Shape", ImVec2(third, 0.0F))) {
            create(Shared::Game::AreaShapeType::Shape, "shape");
        }
        ImGui::SameLine();
        if (ImGui::Button("+ Sphere", ImVec2(-1.0F, 0.0F))) {
            create(Shared::Game::AreaShapeType::Sphere, "sphere");
        }

        std::vector<std::string> groups;
        for (const auto &[id, group] : _groups) {
            if (std::find(groups.begin(), groups.end(), group) == groups.end()) {
                groups.push_back(group);
            }
        }
        std::sort(groups.begin(), groups.end());
        if (!_groupFilter.empty() && std::find(groups.begin(), groups.end(), _groupFilter) == groups.end()) {
            _groupFilter.clear();
        }
        if (ImGui::BeginCombo("Area group", _groupFilter.empty() ? "All areas" : _groupFilter.c_str())) {
            if (ImGui::Selectable("All areas", _groupFilter.empty())) {
                _groupFilter.clear();
            }
            for (const auto &group : groups) {
                if (ImGui::Selectable(group.c_str(), _groupFilter == group)) {
                    _groupFilter = group;
                    SelectAuthored(-1);
                }
            }
            ImGui::EndCombo();
        }

        if (ImGui::BeginChild("##kcdc_area_drawn_list", ImVec2(0.0F, 96.0F), ImGuiChildFlags_Borders)) {
            if (_authored.empty()) {
                ImGui::TextDisabled("Nothing drawn yet.");
            }
            for (std::size_t index = 0; index < _authored.size(); ++index) {
                const auto group = _groups.find(_authored[index].id);
                if (!_groupFilter.empty() && (group == _groups.end() || group->second != _groupFilter)) {
                    continue;
                }
                ImGui::PushID(static_cast<int>(index));
                const std::string row = _authored[index].id + "  (" + Shared::Game::AreaShapeTypeName(_authored[index].shape.type) + ")" + (group != _groups.end() ? "  [" + group->second + "]" : "");
                if (ImGui::Selectable(row.c_str(), IsSelected(static_cast<int>(index)))) {
                    SelectAuthored(static_cast<int>(index), ImGui::GetIO().KeyShift);
                }
                ImGui::PopID();
            }
        }
        ImGui::EndChild();
    }

    void EditorAreas::RenderSelection() {
        ImGui::TextWrapped("Shift-click areas in the list or their outlines to add or remove them from the selection.");
        if (ImGui::Button(_groupFilter.empty() ? "Select all areas" : "Select group")) {
            SelectAuthored(-1);
            for (std::size_t index = 0; index < _authored.size(); ++index) {
                const auto group = _groups.find(_authored[index].id);
                if (_groupFilter.empty() || (group != _groups.end() && group->second == _groupFilter)) {
                    _selectedAreas.push_back(static_cast<int>(index));
                }
            }
            if (!_selectedAreas.empty()) {
                ++_selectionRevision;
            }
            LoadDrafts();
        }
        if (_selectedAreas.empty()) {
            return;
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear selection")) {
            SelectAuthored(-1);
            return;
        }
        ImGui::Text("%zu area(s) selected", _selectedAreas.size());
        ImGui::InputTextWithHint("Group name", "name for the selected areas", _groupDraft, sizeof(_groupDraft));
        const auto group = Framework::Utils::StringUtils::Trim(_groupDraft);
        ImGui::BeginDisabled(group.empty());
        if (ImGui::Button("Group selected")) {
            for (const int index : _selectedAreas) {
                _groups[_authored[index].id] = group;
            }
            ContentChanged();
            _groupFilter.clear();
            LoadDrafts();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Ungroup selected")) {
            for (const int index : _selectedAreas) {
                _groups.erase(_authored[index].id);
            }
            ContentChanged();
            _groupFilter.clear();
            LoadDrafts();
        }
        if (ImGui::Button("Duplicate selected")) {
            DuplicateSelected();
        }
        ImGui::SameLine();
        const char *deleteLabel = _handle.kind == AreaHandle::Kind::Point ? "Delete point" : _handle.kind == AreaHandle::Kind::Edge ? "Delete edge" : "Delete selected areas";
        if (ImGui::Button(deleteLabel)) {
            DeleteSelected();
        }
        if (_selectedAreas.size() > 1) {
            ImGui::TextWrapped("The gizmo moves and rotates the selected areas together. Scaling a selection keeps its proportions. Select one area to edit its points or size handles.");
        }
    }

    void EditorAreas::DuplicateSelected() {
        const auto selected = _selectedAreas;
        SelectAuthored(-1);
        for (const int index : selected) {
            auto copy        = _authored[index];
            const auto group = _groups.find(copy.id);
            copy.id          = UniqueId(copy.id.c_str());
            copy.shape.position += glm::vec3(2, 2, 0);
            if (group != _groups.end()) {
                _groups[copy.id] = group->second;
            }
            _selectedAreas.push_back(static_cast<int>(_authored.size()));
            _authored.push_back(std::move(copy));
        }
        if (!selected.empty()) {
            ContentChanged();
            ++_selectionRevision;
        }
        LoadDrafts();
    }

    void EditorAreas::DeleteSelected() {
        _handleMarkers.clear();
        _edgeMarkers.clear();
        _markers.clear();
        if (_selectedAreas.size() == 1 && (_handle.kind == AreaHandle::Kind::Point || _handle.kind == AreaHandle::Kind::Edge)) {
            const auto result = DeleteAreaHandlePoints(_authored[_selectedAreas.back()].shape, _handle);
            ClearHandle();
            if (result != AreaPointDeletion::RemoveArea) {
                if (result == AreaPointDeletion::PointsRemoved) {
                    ContentChanged();
                }
                return;
            }
        }
        auto selected = _selectedAreas;
        std::sort(selected.begin(), selected.end(), std::greater<int>());
        for (const int index : selected) {
            _groups.erase(_authored[index].id);
            _authored.erase(_authored.begin() + index);
        }
        if (!selected.empty()) {
            ContentChanged();
        }
        SelectAuthored(-1);
    }

    MapDocument EditorAreas::SelectedDocument() const {
        MapDocument document;
        document.areas.reserve(_selectedAreas.size());
        for (const int index : _selectedAreas) {
            const auto &area = _authored[index];
            document.areas.push_back(area);
            if (const auto group = _groups.find(area.id); group != _groups.end()) {
                document.areaGroups.emplace(*group);
            }
        }
        return document;
    }

    void EditorAreas::RenderAuthoredInspector() {
        if (_selectedAreas.size() != 1) {
            return;
        }
        Shared::Game::AreaDefinition &area = _authored[_selectedAreas.back()];
        Shared::Game::AreaShape &shape     = area.shape;

        ImGui::SeparatorText("Drawn area");
        ImGui::InputText("Id", _idDraft, sizeof(_idDraft));
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            const std::string id = Framework::Utils::StringUtils::Trim(_idDraft);
            const bool taken     = std::any_of(_authored.begin(), _authored.end(), [&](const Shared::Game::AreaDefinition &other) {
                return &other != &area && other.id == id;
            });
            // The server refuses an id that reads as a level GUID, so the editor does too.
            if (!id.empty() && !taken && KCDC::Scripting::ParseLevelGuid(id) == 0) {
                if (auto group = _groups.extract(area.id); !group.empty()) {
                    group.key() = id;
                    _groups.insert(std::move(group));
                }
                area.id = id;
                ContentChanged();
            }
            std::snprintf(_idDraft, sizeof(_idDraft), "%s", area.id.c_str());
        }
        ImGui::InputText("Name", _nameDraft, sizeof(_nameDraft));
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            area.name = _nameDraft[0] != '\0' ? _nameDraft : area.id;
            ContentChanged();
        }
        ImGui::InputTextWithHint("Labels", "comma separated", _labelsDraft, sizeof(_labelsDraft));
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            area.labels = SplitList(_labelsDraft);
            ContentChanged();
        }

        ImGui::TextWrapped("Click a corner, edge or size handle, then drag its arrows. Hold Shift while moving an area or its polygon points to snap to the ground. Shift-click + Box, + Shape or + Sphere to place its bottom on the ground. The toolbar's translation snap also applies to new areas. Double-click an edge to insert a point there. Escape returns to moving the whole area.");
        if (IsEditingHandle() && ImGui::Button("Move whole area")) {
            ClearHandle();
        }

        if (ImGui::DragFloat3("Position", &shape.position.x, 0.05F)) {
            ContentChanged();
        }
        float yaw = glm::degrees(glm::roll(shape.rotation));
        if (ImGui::DragFloat("Yaw", &yaw, 0.5F, -180.0F, 180.0F, "%.1f deg")) {
            shape.rotation = glm::angleAxis(glm::radians(yaw), glm::vec3(0.0F, 0.0F, 1.0F));
            ContentChanged();
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
            ImGui::SetTooltip("Resets any tilt; the gizmo's rotate tool turns it freely.");
        }

        switch (shape.type) {
        case Shared::Game::AreaShapeType::Box: {
            if (ImGui::DragFloat3("Min", &shape.boxMin.x, 0.05F)) {
                ContentChanged();
            }
            if (ImGui::DragFloat3("Max", &shape.boxMax.x, 0.05F)) {
                ContentChanged();
            }
            const auto clampedMax = glm::max(shape.boxMax, shape.boxMin + glm::vec3(0.01F));
            if (glm::any(glm::notEqual(clampedMax, shape.boxMax))) {
                shape.boxMax = clampedMax;
                ContentChanged();
            }
            break;
        }
        case Shared::Game::AreaShapeType::Shape: {
            if (ImGui::DragFloat("Height", &shape.height, 0.05F, 0.0F, Shared::Game::AreaShape::kMaxExtent, shape.height > 0.0F ? "%.2f m" : "no ceiling")) {
                ContentChanged();
            }
            int removed = -1;
            for (std::size_t index = 0; index < shape.points.size(); ++index) {
                ImGui::PushID(static_cast<int>(index));
                if (ImGui::Selectable(("Point " + std::to_string(index + 1)).c_str(), _handle == AreaHandle {AreaHandle::Kind::Point, static_cast<int>(index)}, 0, ImVec2(64.0F, 0.0F))) {
                    SelectHandle({AreaHandle::Kind::Point, static_cast<int>(index)});
                }
                ImGui::SameLine();
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 28.0F);
                if (ImGui::DragFloat3("##point", &shape.points[index].x, 0.05F)) {
                    ContentChanged();
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("x")) {
                    removed = static_cast<int>(index);
                }
                ImGui::PopID();
            }
            if (removed >= 0) {
                SelectHandle({AreaHandle::Kind::Point, removed});
                DeleteSelected();
                return;
            }
            if (shape.points.size() < Shared::Game::AreaShape::kMaxPoints && ImGui::SmallButton("Add a corner")) {
                if (SplitAreaEdge(shape, static_cast<int>(shape.points.size()) - 1)) {
                    ContentChanged();
                    SelectHandle({AreaHandle::Kind::Point, static_cast<int>(shape.points.size()) - 1});
                }
            }
            if (_handle.kind == AreaHandle::Kind::Edge && shape.points.size() < Shared::Game::AreaShape::kMaxPoints && ImGui::SmallButton("Split selected edge")) {
                if (SplitAreaEdge(shape, _handle.index)) {
                    ContentChanged();
                    SelectHandle({AreaHandle::Kind::Point, _handle.index + 1});
                }
            }
            break;
        }
        case Shared::Game::AreaShapeType::Sphere:
            if (ImGui::DragFloat("Radius", &shape.radius, 0.05F, 0.1F, Shared::Game::AreaShape::kMaxExtent, "%.2f m")) {
                ContentChanged();
            }
            break;
        }

        bool bound = area.virtualWorld.has_value();
        if (ImGui::Checkbox("One virtual world", &bound)) {
            area.virtualWorld = bound ? std::optional<std::uint32_t>(0U) : std::nullopt;
            ContentChanged();
        }
        if (area.virtualWorld.has_value()) {
            ImGui::SameLine();
            int world = static_cast<int>(*area.virtualWorld);
            ImGui::SetNextItemWidth(-1.0F);
            if (ImGui::InputInt("##kcdc_area_world", &world)) {
                area.virtualWorld = static_cast<std::uint32_t>(std::max(world, 0));
                ContentChanged();
            }
        }
        if (ImGui::Checkbox("Enabled", &area.enabled)) {
            ContentChanged();
        }

        if (ImGui::Checkbox("Collision", &area.collision)) {
            ContentChanged();
        }
        if (area.collision) {
            ImGui::TextWrapped("Invisible walls block crossing from either side. The interior stays empty.");
            if (shape.type != Shared::Game::AreaShapeType::Sphere) {
                if (ImGui::Checkbox("Floor", &area.collisionOptions.floor)) {
                    ContentChanged();
                }
                ImGui::SameLine();
                if (ImGui::Checkbox("Ceiling", &area.collisionOptions.ceiling)) {
                    ContentChanged();
                }
                ImGui::SameLine();
                ImGui::TextDisabled("(?)");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("A floor or ceiling is a plate across the whole inside. Turn one on only when players must not get under or over the walls.");
                }
                if (area.collisionOptions.ceiling) {
                    // The game's attack check marks anything solid up to about 1.97 m above a player's feet.
                    ImGui::TextColored(kWarnText, "Keep the ceiling well above head height everywhere inside: where it comes within about 2 m of the ground, overhead attacks are blocked.");
                }
            }
            if (ImGui::DragFloat("Wall thickness", &area.collisionOptions.thickness, 0.01F, Shared::Game::AreaCollisionOptions::kMinThickness, Shared::Game::AreaCollisionOptions::kMaxThickness, "%.2f m", ImGuiSliderFlags_AlwaysClamp)) {
                ContentChanged();
            }
            if (_collisionValidationRevision != _contentRevision || _collisionValidationId != area.id) {
                _collisionValidationRevision = _contentRevision;
                _collisionValidationId       = area.id;
                _collisionError.clear();
                Shared::Game::AreaCollisionMesh mesh;
                Shared::Game::BuildAreaCollisionMesh(area.shape, area.collisionOptions, mesh, _collisionError);
            }
            if (!_collisionError.empty()) {
                ImGui::TextColored(kErrorText, "%s", _collisionError.c_str());
            }
        }

        ImGui::TextUnformatted("Metadata (JSON)");
        ImGui::InputTextMultiline("##kcdc_area_metadata", _metadataDraft, sizeof(_metadataDraft), ImVec2(-1.0F, ImGui::GetTextLineHeight() * 5.0F));
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            const nlohmann::json parsed = nlohmann::json::parse(_metadataDraft, nullptr, false);
            _metadataInvalid            = !parsed.is_object();
            if (!_metadataInvalid) {
                area.metadataJson = parsed.dump();
                ContentChanged();
            }
        }
        if (_metadataInvalid) {
            ImGui::TextColored(kErrorText, "Not a JSON object; the last good metadata is kept.");
        }
        if (!shape.IsValid()) {
            ImGui::TextColored(kErrorText, "The server would refuse this shape.");
        }
    }

    void EditorAreas::RenderExport(std::string &notice) {
        if (!_selectedAreas.empty()) {
            if (ImGui::Button("Copy selected definitions")) {
                ImGui::SetClipboardText(MapDocumentToJson(SelectedDocument())["areas"].dump(2).c_str());
                notice = "Copied the selected area definitions.";
            }
            if (ImGui::Button("Copy selected areas as API calls")) {
                ImGui::SetClipboardText(Shared::WorldBuilder::ApiCalls(MapDocumentToJson(SelectedDocument())).c_str());
                notice = "Copied the selected areas as server API calls.";
            }
        }
        ImGui::Spacing();
        ImGui::TextWrapped("Drawn areas and groups save with the project. Copy all definitions, or choose an area group to copy just that group.");
        const auto copy = [&](bool groupOnly, bool api) {
            MapDocument document;
            document.areaGroups = _groups;
            for (const auto &area : _authored) {
                const auto group = _groups.find(area.id);
                if (!groupOnly || (group != _groups.end() && group->second == _groupFilter)) {
                    document.areas.push_back(area);
                }
            }
            const auto content = MapDocumentToJson(document);
            ImGui::SetClipboardText((api ? Shared::WorldBuilder::ApiCalls(content) : content["areas"].dump(2)).c_str());
            notice = "Copied " + std::to_string(document.areas.size()) + " area definition(s).";
        };
        if (!_groupFilter.empty()) {
            if (ImGui::Button("Copy group definitions")) {
                copy(true, false);
            }
            if (ImGui::Button("Copy group as API calls")) {
                copy(true, true);
            }
        }
        const float half = ImGui::GetContentRegionAvail().x * 0.5F - 2.0F;
        if (ImGui::Button("Copy all definitions", ImVec2(half, 0.0F))) {
            copy(false, false);
        }
        ImGui::SameLine();
        if (ImGui::Button("Paste definitions", ImVec2(-1.0F, 0.0F))) {
            const char *clipboard = ImGui::GetClipboardText();
            MapDocument document;
            std::size_t skipped = 0;
            std::string error;
            auto definitions = nlohmann::json::parse(clipboard != nullptr ? clipboard : "", nullptr, false);
            if (definitions.is_object()) {
                definitions = nlohmann::json::array({definitions});
            }
            if (!definitions.is_array() || !MapDocumentFromJson({{"objects", nlohmann::json::array()}, {"areas", definitions}}, document, skipped, error)) {
                notice = "Could not paste: " + (error.empty() ? std::string("expected an area definition or array of definitions") : error);
                return;
            }
            const auto first = _authored.size();
            Import(document.areas, false, document.areaGroups);
            if (!document.areas.empty()) {
                _groupFilter.clear();
                SelectAuthored(static_cast<int>(first));
            }
            notice = "Pasted " + std::to_string(document.areas.size()) + " area(s)" + (skipped != 0 ? ", skipped " + std::to_string(skipped) + " the server would refuse." : ".");
        }
    }

    void EditorAreas::DrawLevelArea(std::vector<Gizmos::Drawing> &lines, int levelIndex, ImU32 colour, float thickness) {
        const Shared::Game::LevelArea &area = LevelAreaAt(levelIndex);
        if (area.areaClass == Shared::Game::LevelAreaClass::AreaUnion) {
            for (std::uint32_t index = 0; index < area.memberCount; ++index) {
                const auto member = _levelByGuid.find(Shared::Game::g_LevelAreaMembers[area.firstMember + index]);
                if (member != _levelByGuid.end()) {
                    DrawLevelArea(lines, static_cast<int>(member->second), colour, thickness);
                }
            }
            return;
        }
        Gizmos::Drawing &drawing = lines.emplace_back(EditorOutline(colour, thickness));
        Prism(drawing, Shared::Game::g_LevelAreaPoints.data() + area.firstPoint, area.pointCount, false, area.boundsMin.z, area.boundsMax.z);
    }

    void EditorAreas::DrawAuthoredArea(std::vector<Gizmos::Drawing> &lines, const Camera::CameraPose &camera, int authored, ImU32 colour, float thickness) {
        Gizmos::Drawing &drawing = lines.emplace_back(EditorOutline(colour, thickness));
        const auto &shape        = _authored[authored].shape;
        int segment              = 0;
        const auto eye           = KCDC::Common::ToGlm(camera.position);
        const auto forward       = KCDC::Common::ToGlm(Camera::PoseForward(camera));
        Shared::Game::AreaWireframe(shape, [&](const glm::vec3 &from, const glm::vec3 &to, float weight) {
            drawing.segments.push_back({from, to, weight});
            const int edge = shape.type == Shared::Game::AreaShapeType::Shape && weight == 1.0F ? (shape.height > 0.0F ? segment / 3 : segment) : -1;
            ++segment;
            if (_tabVisible) {
                const auto a = Project(camera, from);
                const auto b = Project(camera, to);
                if (a.visible && b.visible) {
                    _edgeMarkers.push_back({a.at, b.at, authored, edge, glm::dot(from - eye, forward), glm::dot(to - eye, forward)});
                }
            }
        });
    }

    void EditorAreas::RenderOverlay(const EditorSnapshot &snapshot, std::vector<Gizmos::Drawing> &lines) {
        if (!snapshot.cameraHeld) {
            return;
        }
        RefreshLevel(snapshot.level);
        ImDrawList *draw = ImGui::GetBackgroundDrawList();
        if (snapshot.hover.valid) {
            _lastWorldPoint = snapshot.hover.point;
            _hasWorldPoint  = true;
        }

        // Area anchors select the whole area; the selected area's handles are drawn below.
        const auto marker = [&](const glm::vec3 &anchor, ImU32 colour, int levelIndex, int authored, const char *label) {
            const ScreenPoint point = Project(snapshot.camera, anchor);
            if (!point.visible) {
                return;
            }
            draw->AddCircleFilled(point.at, kMarkerRadius, colour | IM_COL32_A_MASK);
            if (label != nullptr) {
                draw->AddText(ImVec2(point.at.x + 7.0F, point.at.y - 7.0F), IM_COL32(255, 255, 255, 230), label);
            }
            if (_tabVisible && (levelIndex >= 0 || authored >= 0)) {
                _markers.push_back(Marker {point.at, levelIndex, authored});
            }
        };

        // The tab refreshed the nearby list this frame before the overlay is drawn.
        if (_showLevel && _tabVisible) {
            for (const std::uint32_t index : _nearby) {
                const Shared::Game::LevelArea &area = Shared::Game::g_LevelAreas[index];
                if (!IsShown(area)) {
                    continue;
                }
                const bool selected = _selectedLevel == static_cast<int>(index);
                if (!selected) {
                    DrawLevelArea(lines, static_cast<int>(index), ColourOf(area.areaClass), 1.0F);
                }
                marker(LevelAnchor(area), ColourOf(area.areaClass), static_cast<int>(index), -1, nullptr);
            }
        }

        // The selection is drawn last and brightest, with its doors, whatever the filters say.
        if (_selectedLevel >= 0) {
            const Shared::Game::LevelArea &area = LevelAreaAt(_selectedLevel);
            DrawLevelArea(lines, _selectedLevel, kSelectedColour, 2.0F);
            marker(LevelAnchor(area), kSelectedColour, _selectedLevel, -1, area.location[0] != '\0' ? area.location : area.labels);
            for (const LinkedDoor &door : _selectedDoors) {
                if (door.placement != nullptr) {
                    marker(glm::vec3(door.placement->positionX, door.placement->positionY, door.placement->positionZ + 1.0F), kDoorColour, -1, -1, Shared::Game::AreaDoorLinkName(door.link));
                }
            }
        }

        // The selected one last, so another drawn on the same spot cannot cover its outline or its label.
        if (_showAuthored) {
            const auto drawOne = [&](std::size_t index, bool selected) {
                const auto group = _groups.find(_authored[index].id);
                if (!selected && !_groupFilter.empty() && (group == _groups.end() || group->second != _groupFilter)) {
                    return;
                }
                DrawAuthoredArea(lines, snapshot.camera, static_cast<int>(index), selected ? kSelectedColour : kAuthoredColour, selected ? 2.0F : 1.5F);
                marker(_authored[index].shape.position, selected ? kSelectedColour : kAuthoredColour, -1, static_cast<int>(index), _authored[index].id.c_str());
            };
            for (std::size_t index = 0; index < _authored.size(); ++index) {
                if (!IsSelected(static_cast<int>(index))) {
                    drawOne(index, false);
                }
            }
            for (const int index : _selectedAreas) {
                drawOne(static_cast<std::size_t>(index), true);
            }
            if (_tabVisible && _selectedAreas.size() == 1) {
                DrawHandles(snapshot.camera);
            }
            if (_showCollision) {
                DrawCollisionPreview(lines);
            }
        }
    }

    void EditorAreas::DrawCollisionPreview(std::vector<Gizmos::Drawing> &lines) {
        if (_collisionPreviewContent != _contentRevision || _collisionPreviewSelection != _selectionRevision) {
            _collisionPreviewContent   = _contentRevision;
            _collisionPreviewSelection = _selectionRevision;
            _collisionPreview.clear();
            for (const int index : _selectedAreas) {
                const auto &area = _authored[static_cast<std::size_t>(index)];
                Shared::Game::AreaCollisionMesh mesh;
                std::string error;
                if (!area.collision || !Shared::Game::BuildAreaCollisionMesh(area.shape, area.collisionOptions, mesh, error) || mesh.indices.size() / 3 > kCollisionPreviewTriangles) {
                    continue;
                }
                Gizmos::Drawing &drawing = _collisionPreview.emplace_back();
                drawing.depthTest        = true;
                drawing.color            = 0;
                drawing.fillColor        = kCollisionFill;
                drawing.position         = area.shape.position;
                drawing.triangles.reserve(mesh.indices.size() / 3);
                for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
                    drawing.triangles.push_back({mesh.vertices[mesh.indices[i]], mesh.vertices[mesh.indices[i + 1]], mesh.vertices[mesh.indices[i + 2]]});
                }
            }
        }
        lines.insert(lines.end(), _collisionPreview.begin(), _collisionPreview.end());
    }

    bool EditorAreas::RenderGizmo(const float view[16], const float projection[16], int operation, bool localSpace, const float *snap, GroundSnapFunction ground, bool groundPending) {
        if (!_tabVisible || !_showAuthored) {
            return false;
        }
        // A second click can be on the edge gizmo created by the first.
        // Resolve insertion before ImGuizmo can consume that click.
        const auto &io = ImGui::GetIO();
        if (!io.WantCaptureMouse && !io.KeyAlt && !io.KeyShift && !ImGui::IsMouseDown(ImGuiMouseButton_Right) && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && InsertPointAt(io.MousePos)) {
            _clickHandled = true;
            return false;
        }
        if (_selectedAreas.empty()) {
            return false;
        }
        Shared::Game::AreaShape &shape = _authored[_selectedAreas.back()].shape;

        if (IsEditingHandle() && !HasAreaHandle(shape, _handle)) {
            ClearHandle();
        }
        if (IsEditingHandle()) {
            float matrix[16];
            const bool worldAxis = _handle.kind == AreaHandle::Kind::Radius || _handle.kind == AreaHandle::Kind::Height;
            const bool shapeAxis = _handle.kind == AreaHandle::Kind::BoxFace || (!worldAxis && localSpace);
            ComposeMatrix(KCDC::Common::ToNative(AreaHandlePosition(shape, _handle)), shapeAxis ? KCDC::Common::ToNative(shape.rotation) : SDK::Quat {0, 0, 0, 1}, SDK::Vec3 {1, 1, 1}, matrix);
            ImGuizmo::OPERATION tool = ImGuizmo::TRANSLATE;
            if (_handle.kind == AreaHandle::Kind::Radius || _handle.kind == AreaHandle::Kind::BoxFace) {
                tool = _handle.index / 2 == 0 ? ImGuizmo::TRANSLATE_X : _handle.index / 2 == 1 ? ImGuizmo::TRANSLATE_Y : ImGuizmo::TRANSLATE_Z;
            }
            else if (_handle.kind == AreaHandle::Kind::Height) {
                tool = ImGuizmo::TRANSLATE_Z;
            }
            ImGuizmo::SetID(-1 - _selectedAreas.back());
            const bool moved = ImGuizmo::Manipulate(view, projection, tool, ImGuizmo::LOCAL, matrix, nullptr, snap);
            glm::vec3 point(matrix[12], matrix[13], matrix[14]);
            const bool grounded =
                (_handle.kind == AreaHandle::Kind::Point || _handle.kind == AreaHandle::Kind::Edge) && ground && ground("area:" + _authored[_selectedAreas.back()].id + ":" + std::to_string(static_cast<int>(_handle.kind)) + ":" + std::to_string(_handle.index), point, moved, 0, false);
            if (moved || grounded) {
                if (MoveAreaHandle(shape, _handle, point)) {
                    ContentChanged();
                }
            }
            return !io.KeyShift || ImGuizmo::IsOver() || ImGuizmo::IsUsingAny();
        }

        static const ImGuizmo::OPERATION kOperations[] {ImGuizmo::TRANSLATE, ImGuizmo::ROTATE, ImGuizmo::SCALE};
        const ImGuizmo::OPERATION tool = _groundPlacementRequested || groundPending ? ImGuizmo::TRANSLATE : kOperations[std::clamp(operation, 0, 2)];
        const ImGuizmo::MODE mode      = localSpace || tool == ImGuizmo::SCALE ? ImGuizmo::LOCAL : ImGuizmo::WORLD;

        // ImGuizmo reports scale relative to the beginning of the drag. Keep
        // the last applied scale so each update changes the dimensions once.
        if (!ImGuizmo::IsUsingAny()) {
            _gizmoScale = glm::vec3(1.0F);
        }
        glm::vec3 pivot(0.0F);
        float floor = std::numeric_limits<float>::max();
        for (const int index : _selectedAreas) {
            pivot += _authored[index].shape.position;
            floor = std::min(floor, AreaGroundFloor(_authored[index].shape));
        }
        pivot /= static_cast<float>(_selectedAreas.size());
        const auto orientation = shape.rotation;
        float matrix[16];
        ComposeMatrix(KCDC::Common::ToNative(pivot), KCDC::Common::ToNative(orientation), SDK::Vec3 {1.0F, 1.0F, 1.0F}, matrix);
        ImGuizmo::SetID(-1 - _selectedAreas.back());
        const bool moved = ImGuizmo::Manipulate(view, projection, tool, mode, matrix, nullptr, snap);
        glm::vec3 translated(matrix[12], matrix[13], matrix[14]);
        std::string groundKey = "areas";
        for (const int index : _selectedAreas) {
            groundKey += ":" + _authored[index].id;
        }
        const bool grounded = tool == ImGuizmo::TRANSLATE && ground && ground(groundKey, translated, moved, pivot.z - floor, _groundPlacementRequested);
        if (tool == ImGuizmo::TRANSLATE && ground) {
            _groundPlacementRequested = false;
        }
        if (!moved && !grounded) {
            return !io.KeyShift || ImGuizmo::IsOver() || ImGuizmo::IsUsingAny();
        }
        if (tool == ImGuizmo::TRANSLATE) {
            matrix[12] = translated.x;
            matrix[13] = translated.y;
            matrix[14] = translated.z;
        }

        SDK::Vec3 position {};
        SDK::Quat rotation {0.0F, 0.0F, 0.0F, 1.0F};
        SDK::Vec3 scale {1.0F, 1.0F, 1.0F};
        DecomposeMatrix(matrix, position, rotation, scale);
        const auto turn   = glm::normalize(KCDC::Common::ToGlm(rotation) * glm::inverse(orientation));
        auto currentScale = KCDC::Common::ToGlm(scale);
        if (_selectedAreas.size() > 1) {
            currentScale = glm::vec3(UniformAreaScale(currentScale));
        }
        const auto ratio = currentScale / _gizmoScale;
        std::vector<Shared::Game::AreaShape> edited;
        edited.reserve(_selectedAreas.size());
        for (const int index : _selectedAreas) {
            auto changed = _authored[index].shape;
            if (tool == ImGuizmo::TRANSLATE) {
                changed.position += KCDC::Common::ToGlm(position) - pivot;
            }
            else if (tool == ImGuizmo::ROTATE) {
                changed.position = pivot + turn * (changed.position - pivot);
                changed.rotation = glm::normalize(turn * changed.rotation);
            }
            else {
                changed.position = pivot + (changed.position - pivot) * ratio;
                ScaleAreaShape(changed, ratio);
            }
            if (!changed.IsValid()) {
                return !io.KeyShift || ImGuizmo::IsOver() || ImGuizmo::IsUsingAny();
            }
            edited.push_back(std::move(changed));
        }
        for (std::size_t index = 0; index < edited.size(); ++index) {
            _authored[_selectedAreas[index]].shape = std::move(edited[index]);
        }
        ContentChanged();
        _gizmoScale = currentScale;
        return !io.KeyShift || ImGuizmo::IsOver() || ImGuizmo::IsUsingAny();
    }

    void EditorAreas::DrawHandles(const Camera::CameraPose &camera) {
        const auto &shape = Selected()->shape;
        auto *draw        = ImGui::GetBackgroundDrawList();
        const auto marker = [&](AreaHandle handle, const std::string &label) {
            const auto point = Project(camera, AreaHandlePosition(shape, handle));
            if (!point.visible) {
                return;
            }
            const bool selected = handle == _handle;
            const ImU32 colour  = selected ? IM_COL32(255, 210, 90, 255) : IM_COL32(110, 230, 255, 255);
            draw->AddCircleFilled(point.at, selected ? 6.0F : 4.5F, colour);
            draw->AddCircle(point.at, selected ? 7.5F : 6.0F, IM_COL32(0, 0, 0, 255));
            if (selected || (std::fabs(ImGui::GetIO().MousePos.x - point.at.x) < kPickRadius && std::fabs(ImGui::GetIO().MousePos.y - point.at.y) < kPickRadius)) {
                draw->AddText(ImVec2(point.at.x + 10, point.at.y - 10), colour, label.c_str());
            }
            _handleMarkers.push_back({point.at, handle});
        };
        if (shape.type == Shared::Game::AreaShapeType::Shape) {
            for (std::size_t index = 0; index < shape.points.size(); ++index) {
                marker({AreaHandle::Kind::Point, static_cast<int>(index)}, "Point " + std::to_string(index + 1));
                marker({AreaHandle::Kind::Edge, static_cast<int>(index)}, "Edge " + std::to_string(index + 1));
            }
            if (_handle.kind == AreaHandle::Kind::Edge) {
                for (const auto &edge : _edgeMarkers) {
                    if (edge.authored == _selectedAreas.back() && edge.index == _handle.index) {
                        draw->AddLine(edge.from, edge.to, IM_COL32(255, 210, 90, 255), 3.0F);
                    }
                }
            }
            marker({AreaHandle::Kind::Height}, "Height");
        }
        else {
            for (int index = 0; index < 6; ++index) {
                marker({shape.type == Shared::Game::AreaShapeType::Sphere ? AreaHandle::Kind::Radius : AreaHandle::Kind::BoxFace, index}, shape.type == Shared::Game::AreaShapeType::Sphere ? "Radius" : "Box face");
            }
        }
    }

    bool EditorAreas::PickHandle(const ImVec2 &mouse) {
        float distance = kPickRadius * kPickRadius;
        AreaHandle picked;
        for (const auto &marker : _handleMarkers) {
            const float dx = marker.at.x - mouse.x;
            const float dy = marker.at.y - mouse.y;
            if (dx * dx + dy * dy <= distance) {
                distance = dx * dx + dy * dy;
                picked   = marker.handle;
            }
        }
        if (picked.kind == AreaHandle::Kind::None) {
            return false;
        }
        SelectHandle(picked);
        return true;
    }

    const EditorAreas::EdgeMarker *EditorAreas::PickEdge(const ImVec2 &mouse, bool polygonOnly, float &fraction) const {
        const EdgeMarker *picked = nullptr;
        float distance           = 6.0F * 6.0F;
        for (const auto &edge : _edgeMarkers) {
            if (polygonOnly && edge.index < 0) {
                continue;
            }
            const glm::vec2 from(edge.from.x, edge.from.y), to(edge.to.x, edge.to.y), at(mouse.x, mouse.y);
            const auto delta   = to - from;
            const float length = glm::dot(delta, delta);
            if (length < 1.0F) {
                continue;
            }
            const float along = std::clamp(glm::dot(at - from, delta) / length, 0.0F, 1.0F);
            const auto offset = at - (from + delta * along);
            if (glm::dot(offset, offset) <= distance) {
                distance = glm::dot(offset, offset);
                picked   = &edge;
                // Screen interpolation is perspective distorted. Recover the
                // fraction along the world edge using each endpoint's depth.
                fraction = along * edge.fromDepth / ((1.0F - along) * edge.toDepth + along * edge.fromDepth);
            }
        }
        return picked;
    }

    bool EditorAreas::InsertPointAt(const ImVec2 &mouse) {
        float fraction   = 0;
        const auto *edge = PickEdge(mouse, true, fraction);
        if (edge == nullptr || !SplitAreaEdge(_authored[edge->authored].shape, edge->index, fraction)) {
            return false;
        }
        ContentChanged();
        SelectAuthored(edge->authored);
        SelectHandle({AreaHandle::Kind::Point, edge->index + 1});
        return true;
    }

    bool EditorAreas::Pick(const ImVec2 &mouse) {
        if (!_tabVisible) {
            return false;
        }
        if (_clickHandled) {
            return true;
        }
        const bool toggle = ImGui::GetIO().KeyShift;
        if (!toggle && PickHandle(mouse)) {
            return true;
        }
        const Marker *best = nullptr;
        float bestDistance = kPickRadius * kPickRadius;
        for (const Marker &candidate : _markers) {
            const float dx       = candidate.at.x - mouse.x;
            const float dy       = candidate.at.y - mouse.y;
            const float distance = dx * dx + dy * dy;
            if (distance <= bestDistance) {
                best         = &candidate;
                bestDistance = distance;
            }
        }
        if (best == nullptr) {
            float fraction   = 0;
            const auto *edge = PickEdge(mouse, false, fraction);
            if (edge == nullptr) {
                if (!toggle) {
                    SelectAuthored(-1);
                }
                return true;
            }
            const bool singleSelected = _selectedAreas.size() == 1 && IsSelected(edge->authored);
            if (toggle || !singleSelected) {
                SelectAuthored(edge->authored, toggle);
            }
            if (!toggle && singleSelected && edge->index >= 0) {
                SelectHandle({AreaHandle::Kind::Edge, edge->index});
            }
            return true;
        }
        if (best->authored >= 0) {
            SelectAuthored(best->authored, toggle);
        }
        else {
            SelectLevel(best->levelIndex);
        }
        return true;
    }
} // namespace KCDC::Features::Prop
