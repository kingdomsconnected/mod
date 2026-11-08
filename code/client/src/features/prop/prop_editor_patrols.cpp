#include "prop_editor_patrols.h"
#include "common/module_support.h"
#include "prop_editor.h"
#include "prop_editor_gizmo.h"
#include "prop_editor_support.h"
#include "shared/features/world_builder/world_document.h"
#include "shared/game/npc_roles.h"
#include <cstdio>
#include <unordered_set>

namespace KCDC::Features::Prop {
    Shared::Game::PatrolRouteDefinition *EditorPatrols::Selected() {
        return _selected >= 0 && _selected < static_cast<int>(_routes.size()) ? &_routes[_selected] : nullptr;
    }
    std::string EditorPatrols::UniqueId(const std::string &stem) const {
        std::string candidate = stem.substr(0, 110);
        for (int suffix = 2; std::any_of(_routes.begin(), _routes.end(), [&](const auto &route) { return route.id == candidate; }); ++suffix) {
            candidate = stem.substr(0, 110) + "-" + std::to_string(suffix);
        }
        return candidate;
    }
    void EditorPatrols::Changed() {
        _previewRequest = true;
        _preview.reset();
    }
    void EditorPatrols::ContentChanged() {
        ++_contentRevision;
        Changed();
    }
    void EditorPatrols::Select(int index) {
        FinishReleasedGizmoDrag(true);
        if (_selected != index) {
            ++_selectionRevision;
        }
        _selected = index;
        _point    = -1;
        _placing  = false;
        if (auto *route = Selected()) {
            std::snprintf(_id, sizeof(_id), "%s", route->id.c_str());
            std::snprintf(_name, sizeof(_name), "%s", route->name.c_str());
        }
    }
    void EditorPatrols::ClearPoint() {
        _point   = -1;
        _placing = false;
        FinishReleasedGizmoDrag(true);
    }
    const Shared::Game::PatrolRouteDefinition *EditorPatrols::Find(const std::string &id) const {
        const auto found = std::find_if(_routes.begin(), _routes.end(), [&](const auto &route) { return route.id == id; });
        return found == _routes.end() ? nullptr : &*found;
    }

    std::vector<std::string> EditorPatrols::SelectedIds() const {
        return _selected >= 0 && _selected < static_cast<int>(_routes.size()) ? std::vector<std::string> {_routes[_selected].id} : std::vector<std::string> {};
    }

    bool EditorPatrols::Select(const std::string &id, std::string &error) {
        const auto *route = Find(id);
        if (!id.empty() && !route) {
            error = "Route not found: " + id;
            return false;
        }
        Select(route ? static_cast<int>(route - _routes.data()) : -1);
        error.clear();
        return true;
    }

    bool EditorPatrols::Create(const Shared::Game::PatrolRouteDefinition &route, std::string &error) {
        if (!route.IsSane()) {
            error = "Invalid patrol route.";
            return false;
        }
        if (Find(route.id)) {
            error = "Route ID already exists: " + route.id;
            return false;
        }
        _routes.push_back(route);
        ContentChanged();
        error.clear();
        return true;
    }

    bool EditorPatrols::Update(const std::string &id, const Shared::Game::PatrolRouteDefinition &route, std::string &error) {
        const auto *found = Find(id);
        if (!found) {
            error = "Route not found: " + id;
            return false;
        }
        if (!route.IsSane()) {
            error = "Invalid patrol route.";
            return false;
        }
        if (route.id != id && Find(route.id)) {
            error = "Route ID already exists: " + route.id;
            return false;
        }
        const int index = static_cast<int>(found - _routes.data());
        _routes[index] = route;
        if (_selected == index) {
            Select(index);
        }
        ContentChanged();
        error.clear();
        return true;
    }

    bool EditorPatrols::Remove(const std::string &id, std::string &error) {
        const auto *found = Find(id);
        if (!found) {
            error = "Route not found: " + id;
            return false;
        }
        const int index = static_cast<int>(found - _routes.data());
        _routes.erase(_routes.begin() + index);
        if (_selected == index) {
            Select(-1);
        }
        else if (_selected > index) {
            --_selected;
        }
        _markers.clear();
        _edges.clear();
        ContentChanged();
        error.clear();
        return true;
    }

    bool EditorPatrols::Duplicate(const std::string &id, std::string &createdId, std::string &error) {
        const auto *found = Find(id);
        if (!found) {
            error = "Route not found: " + id;
            return false;
        }
        auto copy = *found;
        copy.id = UniqueId(id);
        copy.name = copy.name.substr(0, 251) + " copy";
        if (!Create(copy, error)) {
            return false;
        }
        createdId = copy.id;
        return true;
    }

    bool EditorPatrols::ReplaceState(const std::vector<Shared::Game::PatrolRouteDefinition> &routes, const std::vector<std::string> &selectedIds, std::string &error) {
        std::unordered_set<std::string> ids;
        for (const auto &route : routes) {
            if (!route.IsSane()) {
                error = "Invalid patrol route.";
                return false;
            }
            if (!ids.insert(route.id).second) {
                error = "Duplicate route ID: " + route.id;
                return false;
            }
        }
        if (selectedIds.size() > 1 || (!selectedIds.empty() && !ids.contains(selectedIds.front()))) {
            error = "Patrol selection must reference at most one existing route.";
            return false;
        }
        _routes = routes;
        _markers.clear();
        _edges.clear();
        Select(-1);
        if (!selectedIds.empty()) {
            Select(selectedIds.front(), error);
        }
        ContentChanged();
        error.clear();
        return true;
    }

    bool EditorPatrols::RequestPreview(const std::string &id, const std::string &role, std::string &error) {
        const auto *route = Find(id);
        if (!route) {
            error = "Route not found: " + id;
            return false;
        }
        if (!route->IsSane()) {
            error = "Invalid patrol route.";
            return false;
        }
        if (std::none_of(Shared::Game::g_NpcRoles.begin(), Shared::Game::g_NpcRoles.end(), [&](const auto &entry) { return entry.name == role; })) {
            error = "Unknown preview NPC role: " + role;
            return false;
        }
        _role = role;
        _preview = std::make_shared<const Shared::Game::PatrolRouteDefinition>(*route);
        _previewRequest = true;
        error.clear();
        return true;
    }

    void EditorPatrols::StopPreview() {
        Changed();
    }

    void EditorPatrols::Import(const std::vector<Shared::Game::PatrolRouteDefinition> &routes, bool replace) {
        const bool clearedRoutes = replace && !_routes.empty();
        bool importedRoutes      = false;
        Changed();
        Select(-1);
        if (replace) {
            _routes.clear();
        }
        for (auto route : routes) {
            if (!route.IsSane()) {
                continue;
            }
            route.id = UniqueId(route.id);
            _routes.push_back(std::move(route));
            importedRoutes = true;
        }
        if (clearedRoutes || importedRoutes) {
            ++_contentRevision;
        }
    }
    MapDocument EditorPatrols::SelectedDocument() const {
        MapDocument document;
        if (_selected >= 0 && _selected < static_cast<int>(_routes.size())) {
            document.patrolRoutes.push_back(_routes[_selected]);
        }
        return document;
    }
    void EditorPatrols::DeleteSelected() {
        auto *route = Selected();
        if (!route) {
            return;
        }
        FinishReleasedGizmoDrag(true);
        if (_point >= 0 && route->points.size() > 1) {
            route->points.erase(route->points.begin() + _point);
            _point = -1;
        }
        else {
            _routes.erase(_routes.begin() + _selected);
            Select(-1);
        }
        ContentChanged();
    }
    void EditorPatrols::DuplicateSelected() {
        auto *route = Selected();
        if (!route) {
            return;
        }
        auto copy = *route;
        copy.id   = UniqueId(copy.id);
        copy.name = copy.name.substr(0, 251) + " copy";
        _routes.push_back(std::move(copy));
        Select(static_cast<int>(_routes.size()) - 1);
        ContentChanged();
    }
    void EditorPatrols::AddPoint(const glm::vec3 &position) {
        auto *route = Selected();
        if (!route || route->points.size() >= Shared::Game::PatrolRouteDefinition::kMaxPoints) {
            return;
        }
        route->points.push_back({position});
        _point = static_cast<int>(route->points.size()) - 1;
        ContentChanged();
    }
    bool EditorPatrols::TakePreviewRequest(std::shared_ptr<const Shared::Game::PatrolRouteDefinition> &route, std::string &role) {
        if (!_previewRequest) {
            return false;
        }
        _previewRequest = false;
        route           = std::move(_preview);
        role            = _role;
        return true;
    }

    void EditorPatrols::RenderTab(const EditorSnapshot &snapshot, std::string &notice) {
        _visible = true;
        ImGui::BeginDisabled(!snapshot.cameraGround.valid);
        if (ImGui::Button("New route")) {
            Shared::Game::PatrolRouteDefinition route;
            route.id                 = UniqueId("patrol");
            route.name               = "New patrol";
            route.mode               = Shared::Game::PatrolMode::PingPong;
            const glm::vec3 position = KCDC::Common::ToGlm(snapshot.cameraGround.point);
            route.points.push_back({position});
            _routes.push_back(std::move(route));
            Select(static_cast<int>(_routes.size()) - 1);
            _point = 0;
            ContentChanged();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!Selected());
        if (ImGui::Button("Duplicate")) {
            DuplicateSelected();
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete route")) {
            _point = -1;
            DeleteSelected();
        }
        ImGui::EndDisabled();
        if (!snapshot.cameraGround.valid) {
            ImGui::TextDisabled("Aim the camera at ground to create a route.");
        }
        if (ImGui::BeginChild("Routes", ImVec2(0, 110), ImGuiChildFlags_Borders)) {
            for (int i = 0; i < static_cast<int>(_routes.size()); ++i) {
                ImGui::PushID(i);
                if (ImGui::Selectable((_routes[i].name + " (" + _routes[i].id + ")").c_str(), _selected == i)) {
                    Select(i);
                }
                ImGui::PopID();
            }
        }
        ImGui::EndChild();
        auto *route = Selected();
        if (!route) {
            ImGui::TextWrapped("Create a route, then add and move its waypoints in the world.");
            return;
        }
        if (ImGui::Button("Copy API call")) {
            ImGui::SetClipboardText(Shared::WorldBuilder::ApiCalls(MapDocumentToJson(SelectedDocument())).c_str());
            notice = "Copied the selected patrol route as a server API call.";
        }
        ImGui::InputText("ID", _id, sizeof(_id));
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            if (_id[0] && std::none_of(_routes.begin(), _routes.end(), [&](const auto &other) { return &other != route && other.id == _id; })) {
                route->id = _id;
                ContentChanged();
            }
            else {
                notice = "Route IDs must be non-empty and unique.";
                std::snprintf(_id, sizeof(_id), "%s", route->id.c_str());
            }
        }
        if (ImGui::InputText("Name", _name, sizeof(_name))) {
            route->name = _name;
            ContentChanged();
        }
        int mode = static_cast<int>(route->mode);
        if (ImGui::Combo("Mode", &mode, "Once\0Loop\0Ping pong\0")) {
            route->mode = static_cast<Shared::Game::PatrolMode>(mode);
            ContentChanged();
        }
        int speed = static_cast<int>(route->speed);
        if (ImGui::Combo("Speed", &speed, "Walk\0Jog\0Run\0")) {
            route->speed = static_cast<Shared::Game::NpcSpeed>(speed);
            ContentChanged();
        }
        if (ImGui::DragFloat("Wait (seconds)", &route->waitSeconds, 0.1F, 0, 3600, "%.1f", ImGuiSliderFlags_AlwaysClamp)) {
            ContentChanged();
        }
        if (ImGui::DragFloat("Arrival radius", &route->radius, 0.05F, 0.1F, 100, "%.2f m", ImGuiSliderFlags_AlwaysClamp)) {
            ContentChanged();
        }
        ImGui::Separator();
        ImGui::BeginDisabled(!_placing && route->points.size() >= Shared::Game::PatrolRouteDefinition::kMaxPoints);
        if (ImGui::Button(_placing ? "Cancel adding points" : "Add points in world")) {
            _placing = !_placing;
        }
        ImGui::EndDisabled();
        ImGui::TextWrapped(_placing ? "Click the ground to append a waypoint. Escape finishes." : "Select a numbered waypoint to move it. Double-click a line to insert a waypoint.");
        if (ImGui::BeginChild("Waypoints", ImVec2(0, 120), ImGuiChildFlags_Borders)) {
            for (int i = 0; i < static_cast<int>(route->points.size()); ++i) {
                ImGui::PushID(i);
                if (ImGui::Selectable(("Waypoint " + std::to_string(i + 1)).c_str(), _point == i)) {
                    FinishReleasedGizmoDrag(true);
                    _point = i;
                }
                ImGui::PopID();
            }
        }
        ImGui::EndChild();
        if (_point >= 0 && _point < static_cast<int>(route->points.size())) {
            auto &point = route->points[_point];
            if (ImGui::DragFloat3("Position", &point.position.x, 0.1F, -Shared::Game::NpcIntent::kWorldLimit, Shared::Game::NpcIntent::kWorldLimit, "%.2f", ImGuiSliderFlags_AlwaysClamp)) {
                ContentChanged();
            }
            int pointSpeed = point.speed ? static_cast<int>(*point.speed) + 1 : 0;
            if (ImGui::Combo("Waypoint speed", &pointSpeed, "Route default\0Walk\0Jog\0Run\0")) {
                point.speed = pointSpeed ? std::optional(static_cast<Shared::Game::NpcSpeed>(pointSpeed - 1)) : std::nullopt;
                ContentChanged();
            }
            auto editOverride = [&](const char *label, const char *field, std::optional<float> &value, float fallback, float minimum, float maximum) {
                bool enabled = value.has_value();
                if (ImGui::Checkbox(label, &enabled)) {
                    value = enabled ? std::optional(fallback) : std::nullopt;
                    ContentChanged();
                }
                if (value && ImGui::DragFloat(field, &*value, 0.1F, minimum, maximum, "%.2f", ImGuiSliderFlags_AlwaysClamp)) {
                    ContentChanged();
                }
            };
            editOverride("Override wait", "Waypoint wait", point.waitSeconds, route->waitSeconds, 0, 3600);
            editOverride("Override radius", "Waypoint radius", point.radius, route->radius, 0.1F, 100);
            ImGui::BeginDisabled(_point == 0);
            if (ImGui::Button("Move earlier")) {
                std::swap(route->points[_point], route->points[_point - 1]);
                --_point;
                ContentChanged();
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(_point + 1 == static_cast<int>(route->points.size()));
            if (ImGui::Button("Move later")) {
                std::swap(route->points[_point], route->points[_point + 1]);
                ++_point;
                ContentChanged();
            }
            ImGui::EndDisabled();
            if (ImGui::Button("Delete waypoint")) {
                DeleteSelected();
                return;
            }
        }
        ImGui::Separator();
        if (ImGui::BeginCombo("Preview NPC", _role.c_str())) {
            for (const auto &entry : Shared::Game::g_NpcRoles) {
                if (ImGui::Selectable(entry.name.data(), _role == entry.name)) {
                    _role = entry.name;
                    Changed();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::BeginDisabled(!snapshot.cameraHeld || !route->IsSane());
        if (ImGui::Button(snapshot.patrolPreviewActive ? "Restart preview" : "Preview")) {
            RequestPreview(route->id, _role, notice);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!snapshot.patrolPreviewActive);
        if (ImGui::Button("Stop preview")) {
            Changed();
        }
        ImGui::EndDisabled();
        ImGui::TextWrapped("%s", snapshot.patrolPreviewStatus.c_str());
        if (snapshot.patrolProgress.is_object()) {
            ImGui::Text("Waypoint %d, lap %d: %s", snapshot.patrolProgress.value("waypointIndex", 0) + 1, snapshot.patrolProgress.value("lap", 0) + 1, snapshot.patrolProgress.value("phase", std::string()).c_str());
        }
    }

    void EditorPatrols::RenderOverlay(const EditorSnapshot &snapshot, std::vector<Gizmos::Drawing> &lines) {
        _markers.clear();
        _edges.clear();
        if (!_visible) {
            return;
        }
        auto project = [&](const glm::vec3 &point, ImVec2 &screen, float &depth) {
            float x, y;
            const auto forward = KCDC::Common::ToGlm(Camera::PoseForward(snapshot.camera));
            depth              = glm::dot(point - KCDC::Common::ToGlm(snapshot.camera.position), forward);
            if (!Camera::ProjectToNdc(snapshot.camera, KCDC::Common::ToNative(point), x, y)) {
                return false;
            }
            screen = NdcToScreen(*ImGui::GetMainViewport(), x, y);
            return true;
        };
        auto *draw = ImGui::GetForegroundDrawList();
        for (int r = 0; r < static_cast<int>(_routes.size()); ++r) {
            const auto &route  = _routes[r];
            const ImU32 colour = r == _selected ? IM_COL32(255, 195, 70, 255) : IM_COL32(115, 190, 240, 190);
            auto drawing       = EditorOutline(colour, 2);
            for (int i = 0; i < static_cast<int>(route.points.size()); ++i) {
                const auto &point = route.points[i].position;
                ImVec2 at;
                float depth;
                if (project(point, at, depth)) {
                    _markers.push_back({at, r, i});
                    const bool target = snapshot.patrolProgress.is_object() && snapshot.patrolProgress.value("routeId", std::string()) == route.id && snapshot.patrolProgress.value("waypointIndex", -1) == i;
                    draw->AddCircleFilled(at, r == _selected && i == _point ? 8.0F : 5.0F, target ? IM_COL32(100, 255, 130, 255) : colour);
                    draw->AddText(ImVec2(at.x + 9, at.y - 8), colour, std::to_string(i + 1).c_str());
                }
                const int next = i + 1 < static_cast<int>(route.points.size()) ? i + 1 : route.mode == Shared::Game::PatrolMode::Loop && route.points.size() > 1 ? 0 : -1;
                if (next < 0) {
                    continue;
                }
                const auto &end = route.points[next].position;
                drawing.segments.push_back({point, end});
                const auto direction = end - point;
                if (glm::length(direction) > 0.1F) {
                    const auto axis = glm::normalize(direction);
                    auto side       = glm::cross(axis, glm::vec3(0, 0, 1));
                    if (glm::length(side) > 0.01F) {
                        side             = glm::normalize(side) * 0.2F;
                        const auto arrow = glm::mix(point, end, 0.6F);
                        drawing.segments.push_back({arrow, arrow - axis * 0.4F + side});
                        drawing.segments.push_back({arrow, arrow - axis * 0.4F - side});
                        if (route.mode == Shared::Game::PatrolMode::PingPong) {
                            const auto back = glm::mix(point, end, 0.4F);
                            drawing.segments.push_back({back, back + axis * 0.4F + side});
                            drawing.segments.push_back({back, back + axis * 0.4F - side});
                        }
                    }
                }
                ImVec2 from, to;
                float fromDepth, toDepth;
                if (project(point, from, fromDepth) && project(end, to, toDepth)) {
                    _edges.push_back({from, to, r, i, next, fromDepth, toDepth});
                }
            }
            drawing.ComputeBounds();
            lines.push_back(std::move(drawing));
        }
        if (snapshot.patrolPath.size() > 1) {
            auto drawing = EditorOutline(IM_COL32(100, 255, 130, 255), 3);
            for (std::size_t i = 1; i < snapshot.patrolPath.size(); ++i) {
                drawing.segments.push_back({snapshot.patrolPath[i - 1], snapshot.patrolPath[i]});
            }
            drawing.ComputeBounds();
            lines.push_back(std::move(drawing));
        }
    }
    bool EditorPatrols::Pick(const ImVec2 &mouse, const EditorSnapshot &snapshot) {
        if (!_visible) {
            return false;
        }
        if (_placing) {
            if (snapshot.hover.valid) {
                AddPoint(KCDC::Common::ToGlm(snapshot.hover.point));
            }
            return true;
        }
        const Marker *nearest = nullptr;
        float distance        = 100;
        for (const auto &marker : _markers) {
            const float dx = mouse.x - marker.at.x, dy = mouse.y - marker.at.y;
            if (dx * dx + dy * dy < distance) {
                distance = dx * dx + dy * dy;
                nearest  = &marker;
            }
        }
        if (nearest) {
            const int point = nearest->point;
            Select(nearest->route);
            _point = point;
            return true;
        }
        const Edge *nearestEdge = nullptr;
        float fraction          = 0;
        distance                = 64;
        for (const auto &edge : _edges) {
            const glm::vec2 delta(edge.to.x - edge.from.x, edge.to.y - edge.from.y), offset(mouse.x - edge.from.x, mouse.y - edge.from.y);
            const float length = glm::dot(delta, delta);
            if (length < 1) {
                continue;
            }
            const float t         = std::clamp(glm::dot(offset, delta) / length, 0.0F, 1.0F);
            const auto difference = offset - delta * t;
            if (glm::dot(difference, difference) < distance) {
                distance    = glm::dot(difference, difference);
                fraction    = t;
                nearestEdge = &edge;
            }
        }
        if (nearestEdge) {
            const Edge edge = *nearestEdge;
            Select(edge.route);
            auto *route = Selected();
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && route->points.size() < Shared::Game::PatrolRouteDefinition::kMaxPoints) {
                const float t = fraction * edge.fromDepth / ((1 - fraction) * edge.toDepth + fraction * edge.fromDepth);
                Shared::Game::PatrolPoint point;
                point.position = glm::mix(route->points[edge.point].position, route->points[edge.next].position, std::clamp(t, 0.001F, 0.999F));
                route->points.insert(route->points.begin() + edge.point + 1, point);
                _point = edge.point + 1;
                ContentChanged();
            }
        }
        return true;
    }
    bool EditorPatrols::RenderGizmo(const float view[16], const float projection[16], const float *snap, GroundSnapFunction ground) {
        auto *route = Selected();
        if (!_visible || _placing || !route || _point < 0 || _point >= static_cast<int>(route->points.size())) {
            return false;
        }
        auto &point = route->points[_point].position;
        float matrix[16];
        ComposeMatrix(KCDC::Common::ToNative(point), {0, 0, 0, 1}, {1, 1, 1}, matrix);
        ImGuizmo::SetID(-200000 - _selected);
        const bool moved = ImGuizmo::Manipulate(view, projection, ImGuizmo::TRANSLATE, ImGuizmo::WORLD, matrix, nullptr, snap);
        if (moved) {
            point = {matrix[12], matrix[13], matrix[14]};
        }
        const bool grounded = ground && ground("patrol:" + route->id + ":" + std::to_string(_point), point, moved, 0, false);
        if (moved || grounded) {
            ContentChanged();
        }
        return true;
    }
} // namespace KCDC::Features::Prop
