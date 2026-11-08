#include "core/application.h"
#include "prop_editor.h"
#include "prop_editor_layer_render.h"
#include <charconv>
#include <cstdio>
#include <imgui.h>

namespace KCDC::Features::Prop {
    std::string MapEditor::LayerKey(const SelectionRef &ref) {
        return std::string(ref.kind == SelectionKind::Placed ? "prop:" : ref.kind == SelectionKind::Light ? "light:" : ref.kind == SelectionKind::Effect ? "effect:" : "world:") + std::to_string(ref.id);
    }

    bool MapEditor::CanEdit(const SelectionRef &ref) const {
        return ref.kind != SelectionKind::None && _layers.Editable(LayerKey(ref));
    }

    bool MapEditor::ApplyLayerAction(const nlohmann::json &action, std::string &error) {
        try {
            EditorLayers next    = _layers;
            const auto operation = action.at("action").get<std::string>();
            const auto id        = action.value("layerId", next.Active());
            bool accepted        = false;
            if (operation == "create") {
                const auto created = next.Create(action.at("name").get<std::string>(), error);
                accepted           = !created.empty() && next.SetActive(created, error);
            }
            else if (operation == "rename") {
                accepted = next.Rename(id, action.at("name").get<std::string>(), error);
            }
            else if (operation == "delete") {
                accepted = next.Delete(id, action.value("confirm", false), error);
            }
            else if (operation == "visibility") {
                accepted = next.SetVisible(id, action.at("visible").get<bool>(), error);
            }
            else if (operation == "lock") {
                accepted = next.SetLocked(id, action.at("locked").get<bool>(), error);
            }
            else if (operation == "activate") {
                accepted = next.SetActive(id, error);
            }
            else if (operation == "assign") {
                const auto *target = next.Find(id);
                if (!target || !target->visible || target->locked) {
                    error = "Choose a visible, unlocked target layer.";
                    return false;
                }
                std::vector<std::string> keys;
                if (action.contains("keys")) {
                    keys = action.at("keys").get<std::vector<std::string>>();
                }
                else {
                    for (const auto &ref : SelectionRefs()) {
                        keys.push_back(LayerKey(ref));
                    }
                }
                if (keys.empty()) {
                    error = "Select objects to assign first.";
                    return false;
                }
                for (const auto &key : keys) {
                    const auto colon = key.find(':');
                    if (colon == std::string::npos) {
                        error = "Invalid object identity.";
                        return false;
                    }
                    const auto kind = key.substr(0, colon);
                    SelectionRef ref {kind == "prop" ? SelectionKind::Placed : kind == "light" ? SelectionKind::Light : kind == "effect" ? SelectionKind::Effect : kind == "world" ? SelectionKind::World : SelectionKind::None, 0};
                    const auto parsed = std::from_chars(key.data() + colon + 1, key.data() + key.size(), ref.id);
                    if (parsed.ec != std::errc {} || parsed.ptr != key.data() + key.size() || !DescribeRef(ref) || !CanEdit(ref)) {
                        error = "A selected object is unavailable, hidden, or locked.";
                        return false;
                    }
                    if (ref.kind == SelectionKind::World) {
                        if (!_world.SupportsLayerVisibility(ref.id)) {
                            error = "Layer assignment supports loaded world brushes, movable brushes, vegetation, and entity-owned objects. This bare object type is unsupported or currently streamed out.";
                            return false;
                        }
                        Shared::Game::LevelEditDefinition identity;
                        if (!WorldObjects::ToDefinition(*_world.Find(ref.id), identity, true)) {
                            error = "This level object has no persistent identity.";
                            return false;
                        }
                    }
                }
                accepted = next.Assign(keys, id, error);
            }
            else {
                error = "Unknown layer action.";
            }
            if (!accepted) {
                return false;
            }
            if (next.Revision() == _layers.Revision()) {
                return true;
            }
            HistoryAction change;
            change.kind         = HistoryAction::Kind::Layers;
            change.layersBefore = std::make_shared<const EditorLayers>(_layers);
            change.layersAfter  = std::make_shared<const EditorLayers>(next);
            HistoryStep step;
            step.label = "Edit layers";
            step.actions.push_back(std::move(change));
            _layers = std::move(next);
            _history.Record(std::move(step));
            if (_selectedKind != SelectionKind::None && !CanEdit({_selectedKind, _selected})) {
                ClearSelection();
            }
            _extra.EraseIf([this](const SelectionRef &ref) {
                return !CanEdit(ref);
            });
            SetStatus("Layers updated.");
            return true;
        }
        catch (const std::exception &exception) {
            error = exception.what();
            return false;
        }
    }

    void MapEditor::UpdateLayerRendering() {
        const bool hidden = std::any_of(_layers.Layers().begin(), _layers.Layers().end(), [](const EditorLayer &layer) {
            return !layer.visible;
        });
        if (!IsOpen() || !hidden) {
            EditorLayerRender::Get().Clear();
            _effects.ApplyLayerVisibility(nullptr);
            _lights.ApplyLayerVisibility(nullptr);
            return;
        }
        auto policy             = std::make_shared<EditorLayerRenderPolicy>();
        policy->defaultVisible  = _layers.Find("default")->visible;
        policy->hasHiddenLayers = hidden;
        for (const auto &object : _scene.Objects()) {
            if (object.renderNode) {
                policy->visibility[object.renderNode] = _layers.Visible(LayerKey({SelectionKind::Placed, object.id}));
            }
        }
        // Entity identities came from this tick's O(1) Refresh lookup. Streamed geometry
        // uses authored spatial keys, so rendering does not bypass the bounded brush refresh.
        for (const auto &world : _world.Edits()) {
            const auto key = LayerKey({SelectionKind::World, world.id});
            if (_layers.LayerOf(key) == "default") {
                continue;
            }
            const bool visible = _layers.Visible(key);
            if (world.kind == WorldPickKind::Entity) {
                if (world.entityRenderNode) {
                    policy->visibility[world.entityRenderNode] = visible;
                }
            }
            else {
                policy->AddWorldGeometry(world.path, world.position, visible);
                if (world.moved) {
                    policy->AddWorldGeometry(world.path, world.originalPosition, visible);
                }
            }
        }
        _effects.ApplyLayerVisibility(&_layers);
        _lights.ApplyLayerVisibility(&_layers);
        if (Core::gApplication) {
            for (const auto *node : Core::gApplication->GetPropGhostService().Nodes()) {
                policy->visibility[node] = true;
            }
            auto &vfx = Core::gApplication->GetVfxService();
            for (const auto &effect : _effects.Effects()) {
                if (effect.editorVisible) {
                    for (const auto *node : vfx.RenderNodes(effect.handle)) {
                        if (node) {
                            policy->visibility[node] = true;
                        }
                    }
                }
            }
            for (const auto handle : {_armedEmitter, _previewEmitter}) {
                if (handle) {
                    for (const auto *node : vfx.RenderNodes(handle)) {
                        if (node) {
                            policy->visibility[node] = true;
                        }
                    }
                }
            }
        }
        EditorLayerRender::Get().Publish(std::move(policy));
    }

    void MapEditor::RenderLayersTab(const EditorSnapshot &snapshot) {
        const auto &layers = *snapshot.layers;
        const auto queue   = [this](nlohmann::json action) {
            Command command;
            command.kind        = CommandKind::Layer;
            command.layerAction = std::move(action);
            Queue(std::move(command));
        };
        ImGui::TextWrapped("Layers control editor visibility and editing. Exports include every object. Default holds all unassigned objects.");
        ImGui::BeginDisabled(!IsSettled(snapshot));
        if (ImGui::BeginTable("layers", 3, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Layer");
            ImGui::TableSetupColumn("Visible", ImGuiTableColumnFlags_WidthFixed, 48);
            ImGui::TableSetupColumn("Locked", ImGuiTableColumnFlags_WidthFixed, 48);
            ImGui::TableHeadersRow();
            for (const auto &layer : layers.Layers()) {
                ImGui::PushID(layer.id.c_str());
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                if (ImGui::Selectable(layer.name.c_str(), layers.Active() == layer.id)) {
                    queue({{"action", "activate"}, {"layerId", layer.id}});
                    std::snprintf(_layerName, sizeof(_layerName), "%s", layer.name.c_str());
                }
                ImGui::TableNextColumn();
                bool visible = layer.visible;
                if (ImGui::Checkbox("##visible", &visible)) {
                    queue({{"action", "visibility"}, {"layerId", layer.id}, {"visible", visible}});
                }
                ImGui::TableNextColumn();
                bool locked = layer.locked;
                if (ImGui::Checkbox("##locked", &locked)) {
                    queue({{"action", "lock"}, {"layerId", layer.id}, {"locked", locked}});
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::InputText("Layer name", _layerName, sizeof(_layerName));
        if (ImGui::Button("Add layer")) {
            queue({{"action", "create"}, {"name", _layerName}});
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(layers.Active() == "default");
        if (ImGui::Button("Rename")) {
            queue({{"action", "rename"}, {"layerId", layers.Active()}, {"name", _layerName}});
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete layer...")) {
            _deleteLayer = layers.Active();
            ImGui::OpenPopup("Delete layer?");
        }
        ImGui::EndDisabled();
        if (ImGui::BeginPopupModal("Delete layer?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextWrapped("Delete this layer? Its objects will return to Default. The objects themselves will remain in the project.");
            if (ImGui::Button("Delete")) {
                queue({{"action", "delete"}, {"layerId", _deleteLayer}, {"confirm", true}});
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        if (ImGui::Button("Assign selection to active layer")) {
            queue({{"action", "assign"}, {"layerId", layers.Active()}});
        }
        ImGui::TextDisabled("Shortcut: Ctrl+L");
        ImGui::EndDisabled();
        ImGui::Separator();
        if (layers.Active() == "default") {
            ImGui::TextWrapped("Default contains all unassigned world objects. Its members are not listed.");
            return;
        }
        ImGui::TextUnformatted("Layer objects");
        const auto member = [&](SelectionKind kind, std::uint64_t id, const std::string &name) {
            const SelectionRef ref {kind, id};
            const auto key = LayerKey(ref);
            if (layers.LayerOf(key) != layers.Active()) {
                return;
            }
            ImGui::PushID(key.c_str());
            ImGui::BeginDisabled(!layers.Editable(key));
            if (ImGui::Selectable(name.c_str(), snapshot.selection.kind == kind && snapshot.selection.id == id)) {
                Command command;
                command.kind   = CommandKind::Select;
                command.target = kind;
                command.id     = id;
                command.enable = ImGui::GetIO().KeyCtrl;
                Queue(std::move(command));
            }
            ImGui::EndDisabled();
            ImGui::PopID();
        };
        if (ImGui::BeginChild("members", ImVec2(0, 0))) {
            for (const auto &row : snapshot.rows) {
                member(SelectionKind::Placed, row.id, row.name);
            }
            for (const auto &row : snapshot.lightRows) {
                member(SelectionKind::Light, row.id, "Point light");
            }
            for (const auto &row : snapshot.effectRows) {
                member(SelectionKind::Effect, row.id, row.label);
            }
            for (const auto &row : snapshot.worldRows) {
                member(SelectionKind::World, row.id, row.label);
            }
        }
        ImGui::EndChild();
    }
} // namespace KCDC::Features::Prop
