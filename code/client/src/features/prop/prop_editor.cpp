#include "prop_editor.h"
#include "prop_editor_capture.h"
#include "prop_editor_gizmo.h"
#include "prop_editor_layer_render.h"
#include "prop_editor_support.h"

// imgui.h first: ImGuizmo.h uses its types without including it.
#include <imgui.h>

#include <ImGuizmo.h>

#include "core/obfuscation.h"

#include <logging/logger.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "core/application.h"
#include "common/module_support.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "sdk/cryengine/entitysystem/i_entity.h"
#include "sdk/cryengine/physics/c_physical_world.h"

#include "features/camera/fly_keys.h"
#include "features/gizmos/gizmos_service.h"
#include "features/player/player_teleport.h"
#include "features/prop/prop_editor_world_objects.h"
#include "features/prop/prop_ghost.h"
#include "shared/game/smart_object_blueprints.h"

#include "shared/game/prop_models.h"
#include "shared/game/vfx_catalog.h"

namespace KCDC::Features::Prop {
    namespace {
        constexpr float kPi           = 3.14159265358979323846F;
        constexpr float kDegreesToRad = kPi / 180.0F;
        constexpr float kRadToDegrees = 180.0F / kPi;

        constexpr float kSidebarWidth = 400.0F;

        constexpr float kGizmoNear = 0.05F;
        constexpr float kGizmoFar  = 8000.0F;

        constexpr float kAimRange  = 400.0F;
        constexpr float kPickRange = 2000.0F;

        // "Visible only": a ray aimed this far above an object's pivot may stop
        // this much short of it and still count as reaching the object itself.
        constexpr float kInViewLift      = 0.5F;
        constexpr float kInViewTolerance = 1.5F;

        // Degrees per pixel of mouse travel.
        constexpr float kLookSensitivity  = 0.12F;
        constexpr float kOrbitSensitivity = 0.3F;

        constexpr float kHoverIntervalSeconds = 0.12F;

        constexpr float kMinBrushScale = 0.05F;
        constexpr float kMaxBrushScale = 20.0F;

        // Probe from slightly above, so a point just inside a floor still resolves.
        constexpr float kGroundProbeUp   = 3.0F;
        constexpr float kGroundProbeDown = 200.0F;

        // Short of the poles, where yaw means nothing.
        constexpr float kOrbitPitchLimit = 85.0F;

        constexpr float kMaxBrowseRadius     = 250.0F;
        constexpr std::size_t kMaxBrowseRows = 800;

        // Turntable speed, hover debounce, and distance per metre of mesh.
        constexpr float kPreviewDegreesPerSecond = 40.0F;
        constexpr std::chrono::milliseconds kPreviewSettle {140};
        constexpr float kPreviewStandOff = 1.4F;

        // Brush tint: blue on a surface, red in the air.
        constexpr GhostTint kBrushOnGround = PackGhostTint(90, 200, 255, 110);
        constexpr GhostTint kBrushInAir    = PackGhostTint(255, 110, 70, 110);

        KCDC::Common::Spawn::PropPhysics PhysicsFromIndex(int index) {
            switch (index) {
            case 0: return KCDC::Common::Spawn::PropPhysics::Rigid;
            case 2: return KCDC::Common::Spawn::PropPhysics::None;
            default: return KCDC::Common::Spawn::PropPhysics::Static;
            }
        }

        int IndexFromPhysics(KCDC::Common::Spawn::PropPhysics physics) {
            switch (physics) {
            case KCDC::Common::Spawn::PropPhysics::Rigid: return 0;
            case KCDC::Common::Spawn::PropPhysics::Static: return 1;
            case KCDC::Common::Spawn::PropPhysics::None: return 2;
            }
            return 1;
        }

        float Length(const SDK::Vec3 &value) {
            return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
        }

        /** A rotation standing an object up on `up`, facing along `hint`. */
        SDK::Quat RotationFromUp(const SDK::Vec3 &up, const SDK::Vec3 &hint) {
            const float upLength = Length(up);
            if (upLength < 1e-4F) {
                return SDK::Quat {0.0F, 0.0F, 0.0F, 1.0F};
            }
            const SDK::Vec3 unitUp {up.x / upLength, up.y / upLength, up.z / upLength};

            // right = hint x up; parallel vectors fall back to world X.
            SDK::Vec3 right {hint.y * unitUp.z - hint.z * unitUp.y, hint.z * unitUp.x - hint.x * unitUp.z, hint.x * unitUp.y - hint.y * unitUp.x};
            float rightLength = Length(right);
            if (rightLength < 1e-4F) {
                right       = SDK::Vec3 {1.0F, 0.0F, 0.0F};
                rightLength = 1.0F;
            }
            right = SDK::Vec3 {right.x / rightLength, right.y / rightLength, right.z / rightLength};

            const SDK::Vec3 forward {unitUp.y * right.z - unitUp.z * right.y, unitUp.z * right.x - unitUp.x * right.z, unitUp.x * right.y - unitUp.y * right.x};

            // The engine's basis columns are (right, forward, up).
            SDK::Matrix34 basis {};
            basis.m[0][0] = right.x;
            basis.m[1][0] = right.y;
            basis.m[2][0] = right.z;
            basis.m[0][1] = forward.x;
            basis.m[1][1] = forward.y;
            basis.m[2][1] = forward.z;
            basis.m[0][2] = unitUp.x;
            basis.m[1][2] = unitUp.y;
            basis.m[2][2] = unitUp.z;

            SDK::Vec3 position {};
            SDK::Quat rotation {0.0F, 0.0F, 0.0F, 1.0F};
            SDK::Vec3 scale {1.0F, 1.0F, 1.0F};
            SDK::Matrix34ToTransform(basis, position, rotation, scale);
            return rotation;
        }

        /** A yaw about world Z in the engine's convention: zero faces +Y. */
        SDK::Quat RotationFromYaw(float yawDegrees) {
            return SDK::QuatFromAngles(SDK::Ang3 {0.0F, 0.0F, yawDegrees * kDegreesToRad});
        }

        /** The ground-plane direction a yaw faces, in the same convention. */
        SDK::Vec3 HeadingFromYaw(float yawDegrees) {
            const float radians = yawDegrees * kDegreesToRad;
            return SDK::Vec3 {-std::sin(radians), std::cos(radians), 0.0F};
        }

        HistoryTransform TransformOf(const SDK::Vec3 &position, const SDK::Quat &rotation, const SDK::Vec3 &scale) {
            return HistoryTransform {position, rotation, scale};
        }

        // --- Toolbar chrome -------------------------------------------------

        const ImVec4 kAccent {0.24F, 0.52F, 0.88F, 1.00F};
        const ImVec4 kAccentHover {0.32F, 0.62F, 0.96F, 1.00F};

        const ImVec4 kPlacedColour {0.6F, 0.9F, 0.6F, 1.0F};
        const ImVec4 kWorldColour {0.5F, 0.75F, 1.0F, 1.0F};
        const ImVec4 kWarnColour {0.9F, 0.8F, 0.4F, 1.0F};

        /** A toolbar button that stays lit while it is the one in effect. */
        bool ToolbarToggle(const char *label, bool active, const char *tooltip, float width) {
            if (active) {
                ImGui::PushStyleColor(ImGuiCol_Button, kAccent);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kAccentHover);
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, kAccent);
            }

            const bool pressed = ImGui::Button(label, ImVec2(width, 0.0F));

            if (active) {
                ImGui::PopStyleColor(3);
            }
            if (tooltip != nullptr && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip("%s", tooltip);
            }
            return pressed;
        }

        /** A hairline between groups of toolbar controls. */
        void ToolbarSeparator() {
            ImGui::SameLine(0.0F, 12.0F);

            const ImVec2 cursor = ImGui::GetCursorScreenPos();
            const float height  = ImGui::GetFrameHeight();
            ImGui::GetWindowDrawList()->AddLine(ImVec2(cursor.x, cursor.y + 3.0F), ImVec2(cursor.x, cursor.y + height - 3.0F), ImGui::GetColorU32(ImGuiCol_Separator));

            ImGui::Dummy(ImVec2(1.0F, height));
            ImGui::SameLine(0.0F, 12.0F);
        }

        /** A button in the danger colour. */
        bool DangerButton(const char *label, const ImVec2 &size) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55F, 0.12F, 0.12F, 1.0F));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.72F, 0.16F, 0.16F, 1.0F));
            const bool pressed = ImGui::Button(label, size);
            ImGui::PopStyleColor(2);
            return pressed;
        }

        void HelpMarker(const char *text) {
            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                ImGui::SetTooltip("%s", text);
            }
        }

        /** The armed row's name, from whichever catalog it belongs to. */
        std::string_view ArmedName(const EditorSnapshot &snapshot) {
            if (snapshot.armedIsEffect) {
                const Shared::Game::VfxEffectEntry *entry = Shared::Game::VfxEffectAt(static_cast<std::size_t>(snapshot.armedModel));
                return entry != nullptr ? entry->name : std::string_view {"?"};
            }
            if (const WorldObjectRow *worldObject = WorldObjectAtLibraryRow(snapshot.armedModel)) {
                return worldObject->name;
            }
            const Shared::Game::PropModelEntry *model = Shared::Game::PropModelAt(static_cast<std::size_t>(snapshot.armedModel));
            return model != nullptr ? model->name : std::string_view {"?"};
        }

        bool IsStaticMesh(std::string_view path) {
            return path.size() > 4 && Framework::Utils::StringUtils::EqualsIgnoreCase(path.substr(path.size() - 4), ".cgf");
        }

        /**
         * What the brush and the library preview draw for a door, smart object
         * or item: its static meshes, or a box where it has none a ghost can
         * draw -- a door's leaf is a character, and so is a worn piece.
         */
        std::vector<GhostPart> WorldObjectGhost(const WorldObjectRow &row) {
            std::vector<GhostPart> parts;
            const auto add = [&parts](std::string_view mesh, float x, float y, float z, float rx, float ry, float rz, float rw, float sx, float sy, float sz) {
                if (parts.size() < PropGhostService::kMaxPartsPerGhost && IsStaticMesh(mesh)) {
                    parts.push_back(GhostPart {std::string(mesh), SDK::Vec3 {x, y, z}, SDK::Quat {rx, ry, rz, rw}, 1.0F, SDK::Vec3 {sx, sy, sz}});
                }
            };
            if (row.kind == PlacedKind::SmartObject) {
                if (const Shared::Game::SmartObjectBlueprint *blueprint = Shared::Game::FindSmartObjectBlueprint(row.model)) {
                    for (std::size_t index = 0; index < blueprint->brushCount; ++index) {
                        const Shared::Game::SmartObjectBrush &brush = blueprint->brushes[index];
                        add(brush.mesh, brush.posX, brush.posY, brush.posZ, brush.rotX, brush.rotY, brush.rotZ, brush.rotW, brush.scaleX, brush.scaleY, brush.scaleZ);
                    }
                    for (std::size_t index = 0; index < blueprint->partCount; ++index) {
                        const Shared::Game::SmartObjectPart &part = blueprint->parts[index];
                        for (std::size_t property = 0; property < part.propertyCount; ++property) {
                            if (part.properties[property].key == "object_Model") {
                                add(part.properties[property].value, part.posX, part.posY, part.posZ, part.rotX, part.rotY, part.rotZ, part.rotW, part.scaleX, part.scaleY, part.scaleZ);
                            }
                        }
                    }
                }
            }
            else if (row.kind == PlacedKind::Item) {
                add(ItemPreviewMesh(row.model), 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 1.0F, 1.0F, 1.0F);
            }
            if (parts.empty()) {
                GhostPart box;
                box.model   = kWorldObjectStandInMesh;
                box.offset  = row.kind == PlacedKind::Door ? SDK::Vec3 {0.0F, 0.0F, 1.05F} : SDK::Vec3 {0.0F, 0.0F, 0.15F};
                box.stretch = row.kind == PlacedKind::Door ? SDK::Vec3 {1.0F, 0.1F, 2.1F} : SDK::Vec3 {0.3F, 0.3F, 0.3F};
                parts.push_back(std::move(box));
            }
            return parts;
        }

        /** How the Object tab and the outliner name a kind other than a mesh. */
        const char *PlacedKindLabel(PlacedKind kind) {
            switch (kind) {
            case PlacedKind::Door: return "door";
            case PlacedKind::SmartObject: return "interactive";
            case PlacedKind::Item: return "item";
            case PlacedKind::Prop: break;
            }
            return "mesh";
        }

        const ImVec4 kEffectColour {1.0F, 0.62F, 0.3F, 1.0F};

        /** The mouse in normalized device coordinates. */
        void CursorNdc(float &ndcX, float &ndcY) {
            const glm::vec2 ndc = ScreenToNdc(*ImGui::GetMainViewport(), ImGui::GetIO().MousePos);
            ndcX                = ndc.x;
            ndcY                = ndc.y;
        }
    } // namespace

    // --- Lifecycle ----------------------------------------------------------

    void MapEditor::SetOpen(bool open) {
        KCDC_JUNK;
        if (_open.exchange(open, std::memory_order_acq_rel) == open || !KCDC_OPAQUE_TRUE) {
            return;
        }
        _closeRequested.store(false, std::memory_order_release);

        _lastTick = {};
    }

    void MapEditor::Shutdown() {
        _mapTravel.Shutdown();
        _areaCollision.Forget();
        ++_collisionContext;
        EditorLayerRender::Get().Clear();
        EditorLayerRender::Get().Update();
        _layers          = EditorLayers();
        _layersPublished = std::make_shared<const EditorLayers>(_layers);
        _mcp.Shutdown();
        // Only a request: the sky is handed back from the render thread,
        // which is the one that knows what this panel changed.
        _environment.RequestRelease();
        EditorCapture::Get().Shutdown();
        _patrolPreview.Stop();
        KCDC_JUNK;
        _open.store(false, std::memory_order_release);
        _looking.store(false, std::memory_order_release);
        _rightMouseHeld = false;
        _lookRotated    = false;
        _camera.Release();
        _scene.Clear();
        ClearUnresolved();
        _history.Clear();

        // Forget rather than restore: the level may already be gone.
        _world.Forget();
        _effects.Forget();
        _lights.Forget();
        _armedGhost     = 0;
        _armedEmitter   = 0;
        _armedModel     = AssetLibrary::kNone;
        _previewGhost   = 0;
        _previewEmitter = 0;
        _previewModel   = AssetLibrary::kNone;

        ClearSelection();

        if (_controlsHeld) {
            _controlsHeld = false;
            if (auto *application = Core::gApplication.get()) {
                application->LockControls(false);
                application->GetMultiplayerFrontend().SetGameHudSuppressed(false);
            }
        }

        std::scoped_lock lock(_snapshotMutex);
        _snapshot = std::make_shared<const EditorSnapshot>();
    }

    void MapEditor::ClearAndRelease() {
        _mapTravel.Shutdown();
        _areaCollision.Reset();
        ++_collisionContext;
        EditorLayerRender::Get().Clear();
        EditorLayerRender::Get().Update();
        _layers          = EditorLayers();
        _layersPublished = std::make_shared<const EditorLayers>(_layers);
        _mcp.Availability(false);
        _mcp.ContextChanged();
        _environment.RequestRelease();
        {
            std::scoped_lock lock(_commandMutex);
            _commands.clear();
            _completedCommand = _nextCommand.load(std::memory_order_acquire);
        }
        _patrolPreview.Stop();
        KCDC_JUNK;
        Disarm();
        SetPreview(AssetLibrary::kNone, false);
        _camera.Release();
        _scene.Clear();
        ClearUnresolved();
        _effects.Clear();
        _lights.Clear();
        _history.Clear();

        _world.RestoreAll();

        ClearSelection();
        SetStatus("Scene cleared");
    }

    void MapEditor::ClearSelection() {
        _selectedKind = SelectionKind::None;
        _selected     = 0;
        _extra.Clear();
    }

    void MapEditor::SelectOnly(const std::vector<SelectionRef> &refs) {
        ClearSelection();
        if (!refs.empty()) {
            _selectedKind = refs.front().kind;
            _selected     = refs.front().id;
            _extra.Assign(refs.begin() + 1, refs.end());
            _extra.Erase(refs.front());
        }
    }

    void MapEditor::SelectTouched(const std::vector<SelectionRef> &touched) {
        std::vector<SelectionRef> present;
        std::vector<SelectionRef> takenOut;
        for (const SelectionRef &ref : touched) {
            if (!DescribeRef(ref).has_value() || !CanEdit(ref)) {
                continue;
            }
            const WorldEdit *edit = ref.kind == SelectionKind::World ? _world.Find(ref.id) : nullptr;
            (edit != nullptr && edit->hidden ? takenOut : present).push_back(ref);
        }
        if (present.empty() && takenOut.empty()) {
            return;
        }
        // Redoing a conversion touches the hidden originals too; select them only when nothing visible was changed.
        SelectOnly(present.empty() ? takenOut : present);
    }

    bool MapEditor::AddToSelection(const SelectionRef &ref) {
        if (_selectedKind == SelectionKind::None) {
            _selectedKind = ref.kind;
            _selected     = ref.id;
            return true;
        }
        if (_selectedKind == ref.kind && _selected == ref.id) {
            return false;
        }
        return _extra.Insert(ref);
    }

    bool MapEditor::RemoveFromSelection(const SelectionRef &ref) {
        if (_selectedKind == ref.kind && _selected == ref.id) {
            _selectedKind = SelectionKind::None;
            _selected     = 0;
            if (!_extra.Empty()) {
                const SelectionRef promoted = _extra.PopFront();
                _selectedKind               = promoted.kind;
                _selected                   = promoted.id;
            }
            return true;
        }
        return _extra.Erase(ref);
    }

    bool MapEditor::SimilarFilter(const SelectionRef &ref, SelectionFilter &filter, glm::vec3 &position) const {
        filter = SelectionFilter {};
        if (ref.kind == SelectionKind::Placed) {
            if (const PlacedObject *object = _scene.Find(ref.id)) {
                filter.kinds = SelectableKinds::Prop;
                filter.mesh  = object->model;
                position     = KCDC::Common::ToGlm(object->position);
                return true;
            }
        }
        else if (ref.kind == SelectionKind::Effect) {
            if (const PlacedEffect *effect = _effects.Find(ref.id)) {
                filter.kinds     = SelectableKinds::Effect;
                filter.className = effect->name;
                position         = KCDC::Common::ToGlm(effect->position);
                return true;
            }
        }
        else if (ref.kind == SelectionKind::Light) {
            if (const PlacedLight *light = _lights.Find(ref.id)) {
                filter.kinds = SelectableKinds::Light;
                position     = KCDC::Common::ToGlm(light->position);
                return true;
            }
        }
        else if (ref.kind == SelectionKind::World) {
            if (const WorldEdit *edit = _world.Find(ref.id)) {
                position = KCDC::Common::ToGlm(edit->position);
                if (edit->kind == WorldPickKind::RenderNode) {
                    // A tree is vegetation and a wall a brush; both are matched by mesh.
                    filter.kinds = SelectableKinds::Brush | SelectableKinds::Vegetation;
                    filter.mesh  = edit->path;
                    return !filter.mesh.empty();
                }
                filter.kinds = SelectableKinds::Entity;
                filter.mesh  = WorldObjects::MeshPath(*edit);
                if (filter.mesh.empty()) {
                    filter.className = edit->className;
                }
                return !filter.mesh.empty() || !filter.className.empty();
            }
        }
        return false;
    }

    MapEditor::ScenePick MapEditor::PickScene(const SDK::Vec3 &heading, PickEffort effort) {
        const KCDC::Features::Camera::CameraPose &pose = _camera.GetPose();
        ScenePick result;

        // The level's bounds passes leave the editor's objects out: their boxes are weighed below, against what the ray struck.
        result.world = _world.Pick(pose.position, heading, kPickRange, effort, _viewport.pickPreferPhysics, &_scene.OwnedEntities());

        PickSurface surface;
        surface.hit      = result.world.kind != WorldPickKind::None && !result.world.boundsOnly;
        surface.distance = result.world.distance;
        surface.point    = result.world.hitPoint;

        // Physics can return the entity; the octree returns its render node.
        if (const std::uint64_t own = _scene.FindByPick(result.world)) {
            surface.placed = CanEdit({SelectionKind::Placed, own}) ? own : 0;
            result.world   = WorldPick {};
        }

        _scene.CollectPickBoxes(_pickBoxes, &_layers);
        result.placed = ChoosePlacedPick(_pickBoxes, pose.position, heading, kPickRange, surface);
        return result;
    }

    MapEditor::PickResult MapEditor::PickThrough(const SDK::Vec3 &heading, PickEffort effort) {
        const KCDC::Features::Camera::CameraPose &pose = _camera.GetPose();
        PickResult result;

        const ScenePick scene         = PickScene(heading, effort);
        const std::uint64_t placedHit = scene.placed.id;
        const float placedDistance    = placedHit != 0 ? scene.placed.distance : kPickRange;
        WorldPick worldHit            = scene.world;

        if (worldHit.kind != WorldPickKind::None) {
            const auto adopted       = _world.FindByPick(worldHit);
            const auto *defaultLayer = _layers.Find("default");
            if (adopted ? !CanEdit({SelectionKind::World, adopted}) : (!defaultLayer->visible || defaultLayer->locked)) {
                worldHit = WorldPick {};
            }
        }
        // Custom level members remain pickable through hidden Default geometry.
        for (const auto &world : _world.Edits()) {
            if (!world.loaded || !world.hasBounds || !CanEdit({SelectionKind::World, world.id}) || _layers.LayerOf(LayerKey({SelectionKind::World, world.id})) == "default") {
                continue;
            }
            const auto distance = SDK::RayBoxEntryDistance(pose.position, heading, world.worldBounds);
            if (distance < 0.0F || distance > kPickRange) {
                continue;
            }
            WorldPick candidate;
            candidate.kind       = world.kind;
            candidate.entityId   = world.entityId;
            candidate.distance   = distance;
            candidate.boundsOnly = true;
            candidate.hitPoint   = {pose.position.x + heading.x * distance, pose.position.y + heading.y * distance, pose.position.z + heading.z * distance};
            if (!PreferWorldPick(candidate, worldHit, _viewport.pickPreferPhysics)) {
                continue;
            }
            const auto *node = _world.ResolveRenderNode(world.id);
            if (!node) {
                continue;
            }
            candidate.node      = world.kind == WorldPickKind::RenderNode ? const_cast<SDK::IRenderNode *>(node) : nullptr;
            candidate.inspected = worldHit.inspected;
            worldHit            = candidate;
        }
        result.inspected = worldHit.inspected;

        // An effect marker floats in front of what it burns on, and wins ties.
        float effectDistance          = kPickRange;
        const std::uint64_t effectHit = _effects.PickNearest(pose.position, heading, kPickRange, effectDistance, &_layers);
        const float otherDistance     = std::min(placedHit != 0 ? placedDistance : kPickRange, worldHit.kind != WorldPickKind::None ? worldHit.distance : kPickRange);
        float lightDistance           = kPickRange;
        const std::uint64_t lightHit  = _lights.PickNearest(pose.position, heading, kPickRange, lightDistance, &_layers);
        float distance                = 0.0F;
        if (lightHit != 0 && lightDistance <= std::min(otherDistance, effectHit != 0 ? effectDistance : kPickRange) + 0.01F) {
            result.light = lightHit;
            distance     = lightDistance;
        }
        else if (effectHit != 0 && effectDistance <= otherDistance + 0.01F) {
            result.effect = effectHit;
            distance      = effectDistance;
        }
        // The editor's object was chosen against the solid surface already; only a nearer box of the level's still outranks it.
        else if (worldHit.kind != WorldPickKind::None && (placedHit == 0 || (worldHit.boundsOnly && worldHit.distance < placedDistance - 0.01F))) {
            result.world = worldHit;
            distance     = worldHit.distance;
        }
        else if (placedHit != 0) {
            result.placed       = placedHit;
            result.placedMethod = scene.placed.method;
            distance            = placedDistance;
        }
        result.distance = distance;
        result.point    = SDK::Vec3 {pose.position.x + heading.x * distance, pose.position.y + heading.y * distance, pose.position.z + heading.z * distance};
        return result;
    }

    bool MapEditor::IsInView(const glm::vec3 &point, const SDK::AABB *bounds) const {
        auto *physics = SDK::CPhysicalWorld::GetInstance();
        if (physics == nullptr) {
            return true;
        }
        // Aimed a little above the pivot, which for props and trees sits on the ground.
        const SDK::Vec3 origin = _camera.GetPose().position;
        const SDK::Vec3 span {point.x - origin.x, point.y - origin.y, point.z + kInViewLift - origin.z};
        const float distance = std::sqrt(span.x * span.x + span.y * span.y + span.z * span.z);
        SDK::SRayHit hit {};
        if (distance < 0.01F || physics->RayWorldIntersection(origin, span, SDK::CPhysicalWorld::kGroundObjectTypes, SDK::CPhysicalWorld::kGroundRayFlags, &hit, 1) <= 0) {
            return true;
        }
        if (hit.distance >= distance - kInViewTolerance) {
            return true;
        }
        // A house's wall is hit long before its pivot; that hit is the house.
        return bounds != nullptr && hit.point.x >= bounds->min.x - kInViewTolerance && hit.point.x <= bounds->max.x + kInViewTolerance && hit.point.y >= bounds->min.y - kInViewTolerance && hit.point.y <= bounds->max.y + kInViewTolerance
               && hit.point.z >= bounds->min.z - kInViewTolerance && hit.point.z <= bounds->max.z + kInViewTolerance;
    }

    SelectionOutcome MapEditor::RunSelection(const SelectionQuery &request) {
        SelectionOutcome outcome;
        SelectionQuery query = request;
        ReleaseUntouchedAdoptions();
        if (query.similarTo.has_value()) {
            // The object decides kind, mesh and class; the rest of the filter still applies.
            glm::vec3 position {0.0F};
            SelectionFilter identity;
            if (!SimilarFilter(*query.similarTo, identity, position)) {
                outcome.error = "Select similar needs an object with a mesh or a class to match";
                return outcome;
            }
            query.filter.kinds     = identity.kinds;
            query.filter.mesh      = identity.mesh;
            query.filter.className = identity.className;
            query.region.kind      = SelectionRegionKind::Sphere;
            query.region.centre    = position;
        }
        // The brush takes what a click there would pick, the first plant the
        // cursor passes over -- a tree by its crown, metres above its pivot --
        // and everything whose pivot is within the radius of the surface under
        // the cursor.
        PickResult aimed;
        std::vector<LevelCandidate> aimedLevel;
        if (query.aimNdc.has_value()) {
            const KCDC::Features::Camera::CameraPose &pose = _camera.GetPose();
            const SDK::Vec3 heading                        = KCDC::Features::Camera::RayThroughNdc(pose, query.aimNdc->x, query.aimNdc->y);
            aimed                                          = PickThrough(heading, PickEffort::Quick);
            SDK::Vec3 point {};
            SDK::Vec3 normal {};
            bool hit = false;
            ResolveAim(query.aimNdc->x, query.aimNdc->y, 0.0F, point, normal, hit);
            LevelCandidate candidate;
            if (_world.Describe(aimed.world, candidate)) {
                aimedLevel.push_back(std::move(candidate));
            }
            // In front of the surface, not behind it.
            const float surface   = hit ? glm::distance(KCDC::Common::ToGlm(pose.position), KCDC::Common::ToGlm(point)) : kPickRange;
            const WorldPick plant = (query.filter.kinds & SelectableKinds::Vegetation) != 0 ? _world.PickPlant(pose.position, heading, surface) : WorldPick {};
            if (!IsSameLevelObject(plant, aimed.world) && _world.Describe(plant, candidate)) {
                aimedLevel.push_back(std::move(candidate));
            }
            const bool picked = aimed.light != 0 || aimed.effect != 0 || aimed.placed != 0 || aimed.world.kind != WorldPickKind::None;
            if (!hit && !picked && aimedLevel.empty()) {
                return outcome; // painting over the sky selects nothing, quietly
            }
            query.region.kind   = SelectionRegionKind::Sphere;
            query.region.centre = KCDC::Common::ToGlm(hit ? point : picked ? aimed.point : plant.hitPoint);
        }
        const SelectionRef aimedRef = aimed.light != 0 ? SelectionRef {SelectionKind::Light, aimed.light} : aimed.effect != 0 ? SelectionRef {SelectionKind::Effect, aimed.effect} : SelectionRef {SelectionKind::Placed, aimed.placed};
        query.region.Prepare();
        if (!query.region.IsValid()) {
            outcome.error = "That selection region is empty or too large";
            return outcome;
        }

        const KCDC::Features::Camera::CameraPose &camera = _camera.GetPose();
        const std::vector<std::string> tokens            = query.filter.Tokens();
        std::vector<SelectionRef> held;
        std::vector<WorldPick> levelOnly;
        const auto weigh = [&](const SelectionRef &ref, const SelectionCandidate &candidate, const SDK::AABB *bounds) {
            // What the brush is over is in sight and under it, wherever its pivot stands.
            const bool underBrush = ref.id != 0 && ref == aimedRef;
            if (CanEdit(ref) && (underBrush || query.region.Contains(candidate.position, camera)) && query.filter.Matches(candidate, tokens) && (underBrush || !query.visibleOnly || IsInView(candidate.position, bounds))) {
                held.push_back(ref);
            }
        };

        if ((query.filter.kinds & SelectableKinds::Prop) != 0) {
            for (const PlacedObject &object : _scene.Objects()) {
                const SelectionRef ref {SelectionKind::Placed, object.id};
                weigh(ref, SelectionCandidate {SelectableKinds::Prop, object.name, object.model, {}, object.group, _layers.LayerOf(LayerKey(ref)), object.physics, KCDC::Common::ToGlm(object.position), false}, object.hasBounds ? &object.worldBounds : nullptr);
            }
        }
        if ((query.filter.kinds & SelectableKinds::Effect) != 0) {
            for (const PlacedEffect &effect : _effects.Effects()) {
                const SelectionRef ref {SelectionKind::Effect, effect.id};
                weigh(ref, SelectionCandidate {SelectableKinds::Effect, effect.label, {}, effect.name, effect.group, _layers.LayerOf(LayerKey(ref)), std::nullopt, KCDC::Common::ToGlm(effect.position), false}, nullptr);
            }
        }
        if ((query.filter.kinds & SelectableKinds::Light) != 0) {
            for (const PlacedLight &light : _lights.Lights()) {
                const SelectionRef ref {SelectionKind::Light, light.id};
                weigh(ref, SelectionCandidate {SelectableKinds::Light, "Light", {}, {}, light.group, _layers.LayerOf(LayerKey(ref)), std::nullopt, KCDC::Common::ToGlm(light.position), false}, nullptr);
            }
        }
        if ((query.filter.kinds & SelectableKinds::Level) != 0) {
            glm::vec3 min {0.0F};
            glm::vec3 max {0.0F};
            query.region.QueryBox(camera, min, max);
            // A huge area is cut down to what is around the camera rather than refused.
            const glm::vec3 eye  = KCDC::Common::ToGlm(camera.position);
            const glm::vec3 low  = glm::max(min, eye - glm::vec3(SelectionRegion::kMaxRadius));
            const glm::vec3 high = glm::min(max, eye + glm::vec3(SelectionRegion::kMaxRadius));
            outcome.truncated    = glm::any(glm::lessThan(min, low)) || glm::any(glm::greaterThan(max, high));
            std::vector<LevelCandidate> level;
            if (glm::all(glm::lessThanEqual(low, high))) {
                bool crowded = false;
                _world.Collect(SDK::AABB {KCDC::Common::ToNative(low), KCDC::Common::ToNative(high)}, level, crowded);
                outcome.truncated = outcome.truncated || crowded;
            }
            const auto isUnderBrush = [&aimedLevel](const LevelCandidate &found) {
                return std::any_of(aimedLevel.begin(), aimedLevel.end(), [&found](const LevelCandidate &aimedAt) {
                    return IsSameLevelObject(found.pick, aimedAt.pick);
                });
            };
            // What the brush is over may stand far outside the box around the surface.
            for (const LevelCandidate &aimedAt : aimedLevel) {
                if (std::none_of(level.begin(), level.end(), [&aimedAt](const LevelCandidate &found) {
                        return IsSameLevelObject(found.pick, aimedAt.pick);
                    })) {
                    level.push_back(aimedAt);
                }
            }
            // The editor's own props are entities too; they belong to the scene list, as in the level browser.
            std::unordered_set<SDK::EntityId> owned {_camera.GetAnchorEntityId()};
            for (const PlacedObject &object : _scene.Objects()) {
                owned.insert(object.entityId);
            }
            // An object the editor does not hold yet is on Default.
            const bool defaultEditable = _layers.Editable(LayerKey({SelectionKind::World, 0}));
            for (const LevelCandidate &found : level) {
                const glm::vec3 position = KCDC::Common::ToGlm(found.position);
                const bool underBrush    = isUnderBrush(found);
                if ((found.pick.kind == WorldPickKind::Entity && owned.contains(found.pick.entityId)) || !(underBrush || query.region.Contains(position, camera))) {
                    continue;
                }
                const std::uint64_t id = _world.FindByPick(found.pick);
                const SelectionRef ref {SelectionKind::World, id};
                const SelectionCandidate candidate {found.kind, found.name, found.mesh, found.className, {}, id != 0 ? _layers.LayerOf(LayerKey(ref)) : std::string {"default"}, std::nullopt, position, found.hidden};
                if (!(id != 0 ? CanEdit(ref) : defaultEditable) || !query.filter.Matches(candidate, tokens) || (!underBrush && query.visibleOnly && !IsInView(position, found.hasBounds ? &found.bounds : nullptr))) {
                    continue;
                }
                if (id != 0) {
                    held.push_back(ref);
                }
                else {
                    levelOnly.push_back(found.pick);
                }
            }
        }
        outcome.matched = held.size() + levelOnly.size();

        if (query.mode == SelectionMode::Replace) {
            ClearSelection();
        }
        if (query.mode == SelectionMode::Remove) {
            // What the editor does not hold yet cannot be selected, so only held objects can leave. One pass, not one scan per object.
            const std::unordered_set<SelectionRef, SelectionRefHash> leaving(held.begin(), held.end());
            outcome.removed = _extra.EraseIf([&leaving](const SelectionRef &ref) {
                return leaving.contains(ref);
            });
            if (leaving.contains(SelectionRef {_selectedKind, _selected})) {
                RemoveFromSelection(SelectionRef {_selectedKind, _selected});
                ++outcome.removed;
            }
        }
        else {
            std::size_t total = SelectionSize();
            for (const SelectionRef &ref : held) {
                if (total >= SelectionQuery::kMaxSelection) {
                    outcome.truncated = true;
                    break;
                }
                if (AddToSelection(ref)) {
                    ++outcome.added;
                    ++total;
                }
            }
            for (const WorldPick &pick : levelOnly) {
                if (total >= SelectionQuery::kMaxSelection) {
                    outcome.truncated = true;
                    break;
                }
                const std::uint64_t id = _world.Adopt(pick);
                if (id == 0) {
                    --outcome.matched; // the browser lists what the editor cannot hold, such as hidden entities
                    continue;
                }
                if (AddToSelection({SelectionKind::World, id})) {
                    _transientAdoptions.insert(id);
                    ++outcome.added;
                    ++total;
                }
            }
        }
        outcome.total = SelectionSize();

        char message[160];
        if (query.mode == SelectionMode::Remove) {
            std::snprintf(message, sizeof(message), "Took %zu out of the selection; %zu selected%s", outcome.removed, outcome.total, outcome.truncated ? " (the search was cut short)" : "");
        }
        else {
            std::snprintf(message, sizeof(message), "%zu matched, %zu selected%s", outcome.matched, outcome.total, outcome.truncated ? " -- the search was cut short; move closer or narrow it" : "");
        }
        SetStatus(message);
        return outcome;
    }

    void MapEditor::ReleaseUntouchedAdoptions() {
        if (_transientAdoptions.empty()) {
            return;
        }
        std::unordered_set<std::uint64_t> candidates;
        for (const std::uint64_t id : _transientAdoptions) {
            const SelectionRef ref {SelectionKind::World, id};
            if (!(_selectedKind == ref.kind && _selected == id) && !_extra.Contains(ref)) {
                candidates.insert(id);
            }
        }
        if (candidates.empty()) {
            return;
        }
        // Kept for good: anything edited, put on a layer, or named by undo history.
        std::unordered_set<std::uint64_t> kept;
        _history.CollectWorldIds(kept);
        std::erase_if(candidates, [this, &kept](std::uint64_t id) {
            const WorldEdit *edit = _world.Find(id);
            const bool keep       = edit != nullptr && (kept.contains(id) || edit->hidden || edit->moved || _layers.LayerOf(LayerKey({SelectionKind::World, id})) != "default");
            if (keep || edit == nullptr) {
                _transientAdoptions.erase(id);
            }
            return keep || edit == nullptr;
        });
        for (const std::uint64_t id : _world.ReleaseUntouched(candidates)) {
            _transientAdoptions.erase(id);
        }
    }

    std::vector<SelectionRef> MapEditor::SelectionRefs() const {
        std::vector<SelectionRef> refs;
        if (_selectedKind == SelectionKind::None) {
            return refs;
        }
        refs.reserve(_extra.Size() + 1);
        refs.push_back(SelectionRef {_selectedKind, _selected});
        refs.insert(refs.end(), _extra.begin(), _extra.end());
        return refs;
    }

    void MapEditor::SelectRef(const SelectionRef &ref, bool additive) {
        if (ref.kind != SelectionKind::None && !CanEdit(ref)) {
            return;
        }
        if (ref.kind == SelectionKind::None || ref.id == 0) {
            if (!additive) {
                ClearSelection();
            }
            return;
        }

        if (!additive) {
            ClearSelection();
            _selectedKind = ref.kind;
            _selected     = ref.id;
            // A group is picked whole: the rest of it joins the selection.
            std::string group;
            if (ref.kind == SelectionKind::Placed) {
                if (const PlacedObject *object = _scene.Find(ref.id)) {
                    group = object->group;
                }
            }
            else if (ref.kind == SelectionKind::Light) {
                if (const PlacedLight *light = _lights.Find(ref.id)) {
                    group = light->group;
                }
            }
            else if (ref.kind == SelectionKind::Effect) {
                if (const PlacedEffect *effect = _effects.Find(ref.id)) {
                    group = effect->group;
                }
            }
            if (group.empty()) {
                return;
            }
            for (const PlacedObject &object : _scene.Objects()) {
                if (CanEdit({SelectionKind::Placed, object.id}) && object.group == group && !(ref.kind == SelectionKind::Placed && ref.id == object.id)) {
                    _extra.Insert(SelectionRef {SelectionKind::Placed, object.id});
                }
            }
            for (const PlacedLight &light : _lights.Lights()) {
                if (CanEdit({SelectionKind::Light, light.id}) && light.group == group && !(ref.kind == SelectionKind::Light && ref.id == light.id)) {
                    _extra.Insert(SelectionRef {SelectionKind::Light, light.id});
                }
            }
            for (const PlacedEffect &effect : _effects.Effects()) {
                if (CanEdit({SelectionKind::Effect, effect.id}) && effect.group == group && !(ref.kind == SelectionKind::Effect && ref.id == effect.id)) {
                    _extra.Insert(SelectionRef {SelectionKind::Effect, effect.id});
                }
            }
            return;
        }

        // Removing the primary promotes the next one.
        if (_selectedKind == SelectionKind::None) {
            _selectedKind = ref.kind;
            _selected     = ref.id;
            return;
        }
        if (_selectedKind == ref.kind && _selected == ref.id) {
            if (_extra.Empty()) {
                ClearSelection();
                return;
            }
            const SelectionRef promoted = _extra.PopFront();
            _selectedKind               = promoted.kind;
            _selected                   = promoted.id;
            return;
        }
        if (_extra.Erase(ref)) {
            return;
        }
        _extra.Insert(ref);
    }

    std::optional<RefPose> MapEditor::DescribeRef(const SelectionRef &ref) const {
        if (ref.kind == SelectionKind::Placed) {
            if (const PlacedObject *object = _scene.Find(ref.id)) {
                return RefPose {object->position, object->rotation, object->scale, object->worldBounds, object->hasBounds};
            }
        }
        else if (ref.kind == SelectionKind::World) {
            if (const WorldEdit *edit = _world.Find(ref.id)) {
                return RefPose {edit->position, edit->rotation, edit->scale, edit->worldBounds, edit->hasBounds};
            }
        }
        else if (ref.kind == SelectionKind::Light) {
            if (const PlacedLight *light = _lights.Find(ref.id)) {
                return RefPose {light->position, SDK::Quat {0, 0, 0, 1}, SDK::Vec3 {1.0F, 1.0F, 1.0F}, EditorLights::BoundsOf(*light), true};
            }
        }
        else if (ref.kind == SelectionKind::Effect) {
            if (const PlacedEffect *effect = _effects.Find(ref.id)) {
                return RefPose {effect->position, effect->rotation, SDK::Vec3 {1.0F, 1.0F, 1.0F}, EditorEffects::BoundsOf(*effect), true};
            }
        }
        return std::nullopt;
    }

    void MapEditor::SetStatus(std::string status) {
        _status = std::move(status);
    }

    void MapEditor::Queue(Command command) {
        if (_mcpDispatchGeneration != 0 && command.kind != CommandKind::Mcp) {
            command.mcpGeneration = _mcpDispatchGeneration;
        }
        const bool restoresHistory = command.kind == CommandKind::Undo || command.kind == CommandKind::Redo || (command.kind == CommandKind::Mcp && command.mcp && (command.mcp->name == "wb_history_undo" || command.mcp->name == "wb_history_redo"));
        if (command.kind == CommandKind::ReplaceDocument) {
            ResetAuthoredHistoryBaseline();
        }
        else if (command.kind == CommandKind::Import && _authoredHistoryPendingRestore == 0) {
            const auto state = CaptureAuthoredHistory();
            if (state->IsValid() && _authoredHistoryBaseline && _authoredHistoryBaselineValid) {
                auto content = ProjectToJson(state->document);
                if (content != _authoredHistoryContent) {
                    // A pasted blueprint is one undo step, including its native
                    // instances and authored areas/routes/views.
                    command.authoredBefore   = _authoredHistoryBaseline;
                    command.authoredAfter    = state;
                    _authoredHistoryBaseline = state;
                    _authoredHistoryContent  = std::move(content);
                    _authoredHistoryRevision = AuthoredHistoryRevision();
                }
            }
        }
        else {
            // Only history-changing commands need an authored flush. Camera,
            // hover and preview traffic must not clone the document per command.
            switch (command.kind) {
            case CommandKind::Layer:
            case CommandKind::Mcp:
            case CommandKind::Undo:
            case CommandKind::Redo:
            case CommandKind::PlaceArmed:
            case CommandKind::DeleteSelection:
            case CommandKind::DuplicateSelection:
            case CommandKind::ConvertSelection:
            case CommandKind::Transform:
            case CommandKind::SetPhysics:
            case CommandKind::SetSettings:
            case CommandKind::Duplicate:
            case CommandKind::Remove:
            case CommandKind::Clear:
            case CommandKind::HideWorld:
            case CommandKind::ReplaceWorld:
            case CommandKind::RestoreWorld:
            case CommandKind::RestoreAllWorld:
            case CommandKind::DropToGround:
            case CommandKind::PlaceLight:
            case CommandKind::SetLightTuning:
            case CommandKind::SetEffectTuning:
            case CommandKind::GroupSelection:
                // Queue(RecordAuthored) does not enter this branch, and the
                // command mutex is still free while the flush queues its step.
                RecordAuthoredChanges({}, restoresHistory);
                break;
            default: break;
            }
        }
        std::scoped_lock lock(_commandMutex);
        command.serial = ++_nextCommand;
        if (restoresHistory || command.kind == CommandKind::ReplaceDocument) {
            _authoredHistoryPendingRestore = command.serial;
        }

        // A drag queues one per frame; only the latest matters.
        if (command.kind == CommandKind::Transform) {
            const auto pending = std::find_if(_commands.begin(), _commands.end(), [&](const Command &queued) {
                return queued.kind == CommandKind::Transform && queued.id == command.id && queued.target == command.target;
            });
            if (pending != _commands.end()) {
                *pending = std::move(command);
                return;
            }
        }

        _commands.push_back(std::move(command));
    }

    std::uint32_t MapEditor::NextGesture() {
        if (++_gestureCounter == 0) {
            ++_gestureCounter;
        }
        return _gestureCounter;
    }

    void MapEditor::QueueLook(const EditorSnapshot &snapshot, const SDK::Vec3 &direction) {
        Command command;
        command.kind      = CommandKind::CameraPose;
        command.position  = snapshot.camera.position;
        command.direction = direction;
        Queue(std::move(command));
    }

    void MapEditor::QueueTransform(const SelectionInfo &selection, std::uint32_t gesture) {
        Command command;
        command.kind     = CommandKind::Transform;
        command.target   = selection.kind;
        command.id       = selection.id;
        command.position = _editPosition;
        command.rotation = _editRotation;
        command.scale    = _editScale;
        command.gesture  = gesture;
        command.enable   = true; // the group follows
        Queue(std::move(command));
    }

    bool MapEditor::HasPendingTransform(std::uint64_t id) const {
        std::scoped_lock lock(_commandMutex);
        return std::any_of(_commands.begin(), _commands.end(), [id](const Command &queued) {
            return queued.kind == CommandKind::Transform && queued.id == id;
        });
    }

    std::shared_ptr<const EditorSnapshot> MapEditor::ReadSnapshot() const {
        std::scoped_lock lock(_snapshotMutex);
        return _snapshot;
    }

    // --- Game thread --------------------------------------------------------

    void MapEditor::NativeContextChanged() {
        _areaCollision.NativeContextChanged();
        ++_collisionContext;
        EditorLayerRender::Get().Clear();
        _mcp.ContextChanged();
        _levelChanged.store(true, std::memory_order_release);
    }

    void MapEditor::Update() {
        _mcp.Availability(Core::gApplication && Core::gApplication->IsOfflineBuilderReady());
        if (_closeRequested.exchange(false, std::memory_order_acq_rel)) {
            _open.store(false, std::memory_order_release);
        }

        if (_levelChanged.exchange(false, std::memory_order_acq_rel)) {
            _mapTravel.Reset();
            _areaCollision.Forget();
            _patrolPreview.Stop(true);
            // Forget rather than restore: the nodes are freed, and queued
            // browser rows name nodes by pointer.
            {
                std::scoped_lock lock(_commandMutex);
                _commands.clear();
                _completedCommand = _nextCommand.load();
            }
            _world.Forget();
            _scene.Forget();
            ClearUnresolved();
            _effects.Forget();
            _lights.Forget();
            _layers          = EditorLayers();
            _layersPublished = std::make_shared<const EditorLayers>(_layers);
            _history.Clear();
            _levelRows = std::make_shared<const std::vector<LevelRow>>();

            // The ghost and effect services drop their own on the same edge.
            _armedGhost     = 0;
            _armedEmitter   = 0;
            _armedModel     = AssetLibrary::kNone;
            _previewGhost   = 0;
            _previewEmitter = 0;
            _previewModel   = AssetLibrary::kNone;

            ClearSelection();
            SetStatus("The level changed; the editor let go of everything in it");
        }

        const bool open = IsOpen();
        if (!open) {
            _mapTravel.Cancel();
        }
        if (!open) {
            EditorLayerRender::Get().Clear();
            EditorLayerRender::Get().Update();
        }
        if (!open && _patrolPreview.Active()) {
            _patrolPreview.Stop();
        }

        const auto reconcileControls = [&] {
            const bool controls = open && !_mapTravel.NativeInput();
            if (controls != _controlsHeld) {
                _controlsHeld = controls;
                if (auto *application = Core::gApplication.get()) {
                    application->LockControls(controls);
                    application->GetMultiplayerFrontend().SetGameHudSuppressed(controls);
                }
            }
        };

        reconcileControls();
        const bool wasTravelling = _mapTravel.Active();
        _mapTravel.Update(_camera);
        // Opening needs input released first; closing must reacquire it in
        // this same tick before any editor or gameplay interaction resumes.
        reconcileControls();
        if (wasTravelling && !_mapTravel.Active()) {
            SetStatus(_mapTravel.State().value("message", "Map travel finished"));
        }
        const bool interacting = open && !_mapTravel.Active();
        if (_mapTravel.Active()) {
            _looking.store(false, std::memory_order_release);
            _rightMouseHeld = _lookRotated = false;
            HideBrush();
            SetPreview(AssetLibrary::kNone, false);
        }
        if (interacting && !_camera.IsActive()) {
            if (_camera.Take()) {
                SetStatus("Editor camera active -- hold the right mouse button to fly");
            }
            else {
                SetStatus("Waiting for a level before the camera can be taken");
            }
        }
        else if (!open && _camera.IsActive()) {
            _camera.Release();
            _looking.store(false, std::memory_order_release);
            _rightMouseHeld = false;
            _lookRotated    = false;
            Disarm();
            SetPreview(AssetLibrary::kNone, false);
        }

        bool idle = false;
        {
            std::scoped_lock lock(_commandMutex);
            idle = _commands.empty();
        }

        // Closed and idle is free; placed or adopted objects still need the
        // prune across a level change.
        if (!open && idle && _scene.Count() == 0 && _effects.Count() == 0 && _lights.Count() == 0 && _world.Edits().empty()) {
            if (_mcp.Available()) {
                PublishSnapshot();
            }
            return;
        }

        const auto now    = std::chrono::steady_clock::now();
        const float delta = _lastTick.time_since_epoch().count() == 0 ? 0.0F : std::chrono::duration<float>(now - _lastTick).count();
        _lastTick         = now;

        if (interacting) {
            DriveCamera(delta);
        }

        DrainCommands();
        _scene.Refresh();
        _effects.Refresh();
        _lights.Refresh();
        _world.Refresh();

        if (interacting && !_mapTravel.Active()) {
            UpdateBrush();
            UpdatePreview(delta);
            _patrolPreview.Update();
        }

        // The brush has its own readout.
        if (interacting && !_mapTravel.Active() && _armedModel == AssetLibrary::kNone) {
            RefreshHover();
        }
        else {
            _hover = HoverInfo {};
        }

        // Drop what has gone; a lost primary hands the gizmo on.
        const auto gone = [this](const SelectionRef &ref) {
            return !CanEdit(ref) || !DescribeRef(ref).has_value();
        };
        _extra.EraseIf(gone);
        const bool primaryGone = _selectedKind != SelectionKind::None && gone(SelectionRef {_selectedKind, _selected});
        if (primaryGone) {
            if (_extra.Empty()) {
                ClearSelection();
            }
            else {
                const SelectionRef promoted = _extra.PopFront();
                _selectedKind               = promoted.kind;
                _selected                   = promoted.id;
            }
        }

        UpdateLayerRendering();
        EditorLayerRender::Get().Update();

        // MCP operations (including closing the panel) still need their
        // acknowledgement while the offline editor UI is closed.
        if (open || _mcp.Available()) {
            PublishSnapshot();
        }
    }

    void MapEditor::ReadFlyInput(KCDC::Features::Camera::FlyInput &fly) const {
        const auto *application = Core::gApplication.get();
        const auto input        = application != nullptr ? application->GetInput() : nullptr;
        if (input == nullptr || _keyboardCaptured.load(std::memory_order_acquire)) {
            return;
        }

        KCDC::Features::Camera::FlyKeyBindings::Get().Read(*input, fly);

        // Ctrl is a chord (Ctrl+Z), never movement: on AZERTY Z is forward.
        if (input->IsKeyDown(FW_KEY_CONTROL)) {
            fly.forward = 0.0F;
            fly.strafe  = 0.0F;
            fly.lift    = 0.0F;
        }
    }

    bool MapEditor::ReadLookDelta(float &yawDelta, float &pitchDelta) const {
        yawDelta   = 0.0F;
        pitchDelta = 0.0F;

        const HWND window = Core::gGlobals.window;
        if (window == nullptr || GetForegroundWindow() != window) {
            return false;
        }

        RECT client {};
        if (GetClientRect(window, &client) == FALSE) {
            return false;
        }

        POINT centre {(client.right - client.left) / 2, (client.bottom - client.top) / 2};
        if (ClientToScreen(window, &centre) == FALSE) {
            return false;
        }

        POINT cursor {};
        if (GetCursorPos(&cursor) == FALSE) {
            return false;
        }

        // Negated: yaw is right-handed about +Z and screen Y runs down.
        // The first frame only recentres.
        if (_looking.load(std::memory_order_acquire)) {
            yawDelta   = -static_cast<float>(cursor.x - centre.x) * kLookSensitivity;
            pitchDelta = -static_cast<float>(cursor.y - centre.y) * kLookSensitivity;
        }

        SetCursorPos(centre.x, centre.y);
        return true;
    }

    void MapEditor::DriveCamera(float deltaSeconds) {
        if (!_camera.IsActive()) {
            _rightMouseHeld = false;
            _lookRotated    = false;
            _looking.store(false, std::memory_order_release);
            return;
        }

        const auto *application = Core::gApplication.get();
        const auto input        = application != nullptr ? application->GetInput() : nullptr;
        const bool holdingLook  = input != nullptr && input->IsMouseButtonDown(1);

        const bool focused = Core::gGlobals.window != nullptr && GetForegroundWindow() == Core::gGlobals.window;

        // Losing focus abandons the gesture; it is not a right-click release.
        if (!focused) {
            _rightMouseHeld = false;
            _lookRotated    = false;
            _looking.store(false, std::memory_order_release);
        }
        else if (holdingLook && !_rightMouseHeld) {
            _rightMouseHeld = true;
            _lookRotated    = false;
        }

        // Flight starts over the world and lasts until release, even though
        // the warped cursor may then sit over a panel.
        if (!_looking.load(std::memory_order_acquire) && holdingLook && focused && !_pointerOverUi.load(std::memory_order_acquire)) {
            float discardYaw   = 0.0F;
            float discardPitch = 0.0F;
            if (ReadLookDelta(discardYaw, discardPitch)) {
                _looking.store(true, std::memory_order_release);
            }
        }

        KCDC::Features::Camera::FlyInput fly {};
        if (_looking.load(std::memory_order_acquire)) {
            if (holdingLook) {
                ReadFlyInput(fly);
            }
            // Include the final mouse movement on the release frame.
            ReadLookDelta(fly.yawDelta, fly.pitchDelta);
        }

        const auto before = _camera.GetPose();
        _camera.Fly(fly, deltaSeconds);
        const auto &after = _camera.GetPose();
        if (_rightMouseHeld && (before.yawDegrees != after.yawDegrees || before.pitchDegrees != after.pitchDegrees)) {
            _lookRotated = true;
        }

        if (!holdingLook) {
            if (_rightMouseHeld && !_lookRotated && _armedModel != AssetLibrary::kNone) {
                Disarm();
                SetStatus("Stopped placing");
            }
            _rightMouseHeld = false;
            _lookRotated    = false;
            _looking.store(false, std::memory_order_release);
        }
    }

    bool MapEditor::ResolveAim(float ndcX, float ndcY, float fallbackDistance, SDK::Vec3 &position, SDK::Vec3 &normal, bool &hitSurface) const {
        const KCDC::Features::Camera::CameraPose &pose = _camera.GetPose();
        const SDK::Vec3 origin                         = pose.position;
        const SDK::Vec3 heading                        = KCDC::Features::Camera::RayThroughNdc(pose, ndcX, ndcY);

        hitSurface = false;
        normal     = SDK::Vec3 {0.0F, 0.0F, 1.0F};

        if (auto *physics = SDK::CPhysicalWorld::GetInstance()) {
            const SDK::Vec3 span {heading.x * kAimRange, heading.y * kAimRange, heading.z * kAimRange};

            SDK::SRayHit hit {};
            if (physics->RayWorldIntersection(origin, span, SDK::CPhysicalWorld::kGroundObjectTypes, SDK::CPhysicalWorld::kGroundRayFlags, &hit, 1) > 0) {
                position   = hit.point;
                normal     = hit.normal;
                hitSurface = true;
                return true;
            }
        }

        const float distance = std::clamp(fallbackDistance, 0.5F, kAimRange);
        position             = SDK::Vec3 {origin.x + heading.x * distance, origin.y + heading.y * distance, origin.z + heading.z * distance};
        return true;
    }

    void MapEditor::DrainCommands() {
        std::vector<Command> commands;
        {
            std::scoped_lock lock(_commandMutex);
            commands.swap(_commands);
        }

        for (const Command &command : commands) {
            Apply(command);
            _completedCommand = std::max(_completedCommand, command.serial);
        }
    }

    // --- The placement brush --------------------------------------------------

    Shared::Game::VfxTuning MapEditor::EffectTuningFor(int effectIndex, float scale) const {
        Shared::Game::VfxTuning tuning = _placement.effectTuning;
        tuning.scale                   = std::clamp(tuning.scale * scale, Shared::Game::VfxTuning::kMinScale, Shared::Game::VfxTuning::kMaxScale);

        // A one-shot would play once and vanish; repeat it so it can be seen.
        const Shared::Game::VfxEffectEntry *entry = Shared::Game::VfxEffectAt(static_cast<std::size_t>(effectIndex));
        if (entry != nullptr && !entry->continuous && tuning.pulsePeriod <= 0.0F) {
            tuning.pulsePeriod = std::clamp(_placement.oneShotRepeat, 0.0F, Shared::Game::VfxTuning::kMaxPulsePeriod);
        }
        return tuning;
    }

    void MapEditor::Arm(int index, bool effect) {
        Disarm();

        auto *application = Core::gApplication.get();
        if (application == nullptr || !_camera.IsActive()) {
            return;
        }

        if (effect) {
            const Shared::Game::VfxEffectEntry *entry = Shared::Game::VfxEffectAt(static_cast<std::size_t>(index));
            if (entry == nullptr) {
                return;
            }
            _armedModel    = index;
            _armedIsEffect = true;
            _armedShown    = false;
            _armedSized    = false;
            _armedScale    = 1.0F;
            RollBrush();
            SetStatus(std::string {"Placing "} + std::string {entry->name} + " -- click the world to put one down, Esc to stop");
            return;
        }

        const WorldObjectRow *worldObject         = WorldObjectAtLibraryRow(index);
        const Shared::Game::PropModelEntry *model = worldObject == nullptr ? Shared::Game::PropModelAt(static_cast<std::size_t>(index)) : nullptr;
        if (model == nullptr && worldObject == nullptr) {
            return;
        }
        const std::string_view name = worldObject != nullptr ? std::string_view {worldObject->name} : model->name;

        // Built at identity so the box is the mesh's own size.
        PropGhostService &ghosts  = application->GetPropGhostService();
        const SDK::Vec3 parking   = _camera.GetPose().position;
        const std::uint32_t ghost = ghosts.Create(worldObject != nullptr ? WorldObjectGhost(*worldObject) : std::vector<GhostPart> {GhostPart {std::string {model->path}}}, kBrushInAir, parking, SDK::Quat {0.0F, 0.0F, 0.0F, 1.0F}, 1.0F);
        if (ghost == PropGhostService::kNoHandle) {
            SetStatus("The game would not load a preview of that mesh; the log has the reason");
            return;
        }

        SDK::AABB bounds {};
        _armedSized = ghosts.Bounds(ghost, bounds);
        _armedSize  = _armedSized ? SDK::Vec3 {bounds.max.x - bounds.min.x, bounds.max.y - bounds.min.y, bounds.max.z - bounds.min.z} : SDK::Vec3 {};

        ghosts.SetHidden(ghost, true);
        _armedGhost      = ghost;
        _armedModel      = index;
        _armedIsEffect   = false;
        _armedFixedScale = worldObject != nullptr;
        _armedShown      = false;
        _armedScale      = 1.0F;
        RollBrush();

        SetStatus(std::string {"Placing "} + std::string {name} + " -- click the world to put one down, Esc to stop");
    }

    void MapEditor::SetPreview(int index, bool effect) {
        auto *application = Core::gApplication.get();
        if (application == nullptr) {
            return;
        }

        if (_previewGhost != 0) {
            application->GetPropGhostService().Destroy(_previewGhost);
            _previewGhost = 0;
        }
        if (_previewEmitter != 0) {
            application->GetVfxService().Stop(_previewEmitter, true);
            _previewEmitter = 0;
        }
        _previewModel = AssetLibrary::kNone;
        _previewSized = false;

        if (index < 0 || !_camera.IsActive()) {
            return;
        }

        if (effect) {
            // Primed, so a loop shows its full state at once.
            Shared::Game::VfxTuning tuning = EffectTuningFor(index, 1.0F);
            tuning.prime                   = true;
            _previewEmitter                = application->GetVfxService().Spawn(Shared::Game::VfxEffectRef::Catalog(static_cast<std::size_t>(index)), KCDC::Common::ToGlm(_camera.GetPose().position), glm::identity<glm::quat>(), tuning, 0);
            if (_previewEmitter == 0) {
                return;
            }
            _previewModel    = index;
            _previewIsEffect = true;
            _previewSize     = SDK::Vec3 {2.0F, 2.0F, 2.0F};
            _previewCentre   = SDK::Vec3 {};
            UpdatePreview(0.0F);
            return;
        }

        const WorldObjectRow *worldObject         = WorldObjectAtLibraryRow(index);
        const Shared::Game::PropModelEntry *model = worldObject == nullptr ? Shared::Game::PropModelAt(static_cast<std::size_t>(index)) : nullptr;
        if (model == nullptr && worldObject == nullptr) {
            return;
        }

        PropGhostService &ghosts  = application->GetPropGhostService();
        const std::uint32_t ghost = ghosts.Create(worldObject != nullptr ? WorldObjectGhost(*worldObject) : std::vector<GhostPart> {GhostPart {std::string {model->path}}}, kGhostUntinted, _camera.GetPose().position, SDK::Quat {0.0F, 0.0F, 0.0F, 1.0F}, 1.0F);
        if (ghost == PropGhostService::kNoHandle) {
            return;
        }

        SDK::AABB bounds {};
        _previewSized = ghosts.Bounds(ghost, bounds);
        if (_previewSized) {
            const SDK::Vec3 origin = _camera.GetPose().position;
            _previewSize           = SDK::Vec3 {bounds.max.x - bounds.min.x, bounds.max.y - bounds.min.y, bounds.max.z - bounds.min.z};
            _previewCentre         = SDK::Vec3 {(bounds.min.x + bounds.max.x) * 0.5F - origin.x, (bounds.min.y + bounds.max.y) * 0.5F - origin.y, (bounds.min.z + bounds.max.z) * 0.5F - origin.z};
        }
        else {
            _previewSize   = SDK::Vec3 {1.0F, 1.0F, 1.0F};
            _previewCentre = SDK::Vec3 {};
        }

        _previewGhost    = ghost;
        _previewModel    = index;
        _previewIsEffect = false;
        _previewYaw      = 30.0F;
        UpdatePreview(0.0F);
    }

    void MapEditor::UpdatePreview(float deltaSeconds) {
        if (_previewGhost == 0 && _previewEmitter == 0) {
            return;
        }
        auto *application = Core::gApplication.get();
        if (application == nullptr) {
            return;
        }

        _previewYaw = std::fmod(_previewYaw + kPreviewDegreesPerSecond * std::clamp(deltaSeconds, 0.0F, 0.1F), 360.0F);

        // Far enough to fit it, left of centre so the panel does not cover it.
        const KCDC::Features::Camera::CameraPose &pose = _camera.GetPose();
        const float extent                             = std::max({_previewSize.x, _previewSize.y, _previewSize.z, 0.5F});
        const float distance                           = extent * kPreviewStandOff / std::max(std::tan(pose.fovDegrees * 0.5F * kDegreesToRad), 0.2F) + 0.5F;
        const SDK::Vec3 forward                        = KCDC::Features::Camera::PoseForward(pose);
        const SDK::Vec3 right                          = KCDC::Features::Camera::PoseRight(pose);
        const float aside                              = -distance * 0.15F;
        const SDK::Vec3 anchor {pose.position.x + forward.x * distance + right.x * aside, pose.position.y + forward.y * distance + right.y * aside, pose.position.z + forward.z * distance + right.z * aside};

        if (_previewEmitter != 0) {
            if (!application->GetVfxService().Move(_previewEmitter, KCDC::Common::ToGlm(anchor), glm::identity<glm::quat>())) {
                _previewEmitter = 0;
                _previewModel   = AssetLibrary::kNone;
            }
            return;
        }

        PropGhostService &ghosts = application->GetPropGhostService();
        if (!ghosts.IsAlive(_previewGhost)) {
            _previewGhost = 0;
            _previewModel = AssetLibrary::kNone;
            return;
        }

        // Spun about its box centre, not its origin.
        const glm::quat spin   = glm::angleAxis(_previewYaw * kDegreesToRad, glm::vec3(0.0F, 0.0F, 1.0F));
        const glm::vec3 centre = spin * KCDC::Common::ToGlm(_previewCentre);
        const SDK::Vec3 position {anchor.x - centre.x, anchor.y - centre.y, anchor.z - centre.z};
        ghosts.SetPose(_previewGhost, position, KCDC::Common::ToNative(spin), 1.0F);
    }

    void MapEditor::Disarm() {
        if (auto *application = Core::gApplication.get()) {
            if (_armedGhost != 0) {
                application->GetPropGhostService().Destroy(_armedGhost);
            }
            if (_armedEmitter != 0) {
                application->GetVfxService().Stop(_armedEmitter, true);
            }
        }
        _armedGhost    = 0;
        _armedEmitter  = 0;
        _armedModel    = AssetLibrary::kNone;
        _armedIsEffect = false;
        _armedShown    = false;
    }

    void MapEditor::RollBrush() {
        if (_placement.randomYaw) {
            _armedYaw = std::uniform_real_distribution<float>(0.0F, 360.0F)(_random);
        }
        if (_placement.randomScale && !_armedFixedScale) {
            const float low  = std::clamp(std::min(_placement.scaleMin, _placement.scaleMax), kMinBrushScale, kMaxBrushScale);
            const float high = std::clamp(std::max(_placement.scaleMin, _placement.scaleMax), kMinBrushScale, kMaxBrushScale);
            _armedScale      = std::uniform_real_distribution<float>(low, high)(_random);
        }
    }

    void MapEditor::HideBrush() {
        if (!_armedShown) {
            return;
        }
        _armedShown = false;

        auto *application = Core::gApplication.get();
        if (application == nullptr) {
            return;
        }
        if (_armedIsEffect) {
            // An emitter cannot be hidden, so it stops and starts again on return.
            if (_armedEmitter != 0) {
                application->GetVfxService().Stop(_armedEmitter, true);
                _armedEmitter = 0;
            }
            return;
        }
        application->GetPropGhostService().SetHidden(_armedGhost, true);
    }

    void MapEditor::UpdateBrush() {
        if (_armedModel == AssetLibrary::kNone) {
            return;
        }

        auto *application = Core::gApplication.get();
        if (application == nullptr) {
            return;
        }
        PropGhostService &ghosts                 = application->GetPropGhostService();
        KCDC::Features::Vfx::VfxService &effects = application->GetVfxService();
        if (!_armedIsEffect && !ghosts.IsAlive(_armedGhost)) {
            _armedGhost = 0;
            _armedModel = AssetLibrary::kNone;
            return;
        }

        // Only over the world and not while flying.
        const bool show = _camera.IsActive() && !_looking.load(std::memory_order_acquire) && _cursorOverWorld.load(std::memory_order_acquire);
        if (!show) {
            HideBrush();
            return;
        }

        SDK::Vec3 position {};
        SDK::Vec3 normal {};
        bool onGround = false;
        ResolveAim(_cursorNdcX.load(std::memory_order_acquire), _cursorNdcY.load(std::memory_order_acquire), _placement.fallbackDistance, position, normal, onGround);

        // Snap on the ground plane, then re-find the height there.
        if (_placement.snapToGrid && _placement.gridStep > 0.01F) {
            position.x = std::round(position.x / _placement.gridStep) * _placement.gridStep;
            position.y = std::round(position.y / _placement.gridStep) * _placement.gridStep;
            if (onGround) {
                SDK::Vec3 ground {};
                if (auto *physics = SDK::CPhysicalWorld::GetInstance(); physics != nullptr && physics->ResolveGround(position, 2.0F, 20.0F, ground)) {
                    position.z = ground.z;
                }
            }
        }

        const bool align         = _placement.alignToSurface && onGround;
        const SDK::Quat rotation = align ? RotationFromUp(normal, HeadingFromYaw(_armedYaw)) : RotationFromYaw(_armedYaw);
        const SDK::Vec3 lift     = align ? normal : SDK::Vec3 {0.0F, 0.0F, 1.0F};
        position                 = SDK::Vec3 {position.x + lift.x * _placement.heightOffset, position.y + lift.y * _placement.heightOffset, position.z + lift.z * _placement.heightOffset};

        _armedPosition = position;
        _armedRotation = rotation;
        _armedOnGround = onGround;

        if (_armedIsEffect) {
            if (_armedEmitter == 0 || !effects.Move(_armedEmitter, KCDC::Common::ToGlm(position), KCDC::Common::ToGlm(rotation))) {
                _armedEmitter = effects.Spawn(Shared::Game::VfxEffectRef::Catalog(static_cast<std::size_t>(_armedModel)), KCDC::Common::ToGlm(position), KCDC::Common::ToGlm(rotation), EffectTuningFor(_armedModel, _armedScale), 0);
            }
            _armedShown = _armedEmitter != 0;
            return;
        }

        ghosts.SetPose(_armedGhost, position, rotation, _armedScale);
        ghosts.SetTint(_armedGhost, onGround ? kBrushOnGround : kBrushInAir);
        if (!_armedShown) {
            ghosts.SetHidden(_armedGhost, false);
            _armedShown = true;
        }
    }

    void MapEditor::PlaceArmed() {
        const auto *layer = _layers.Find("default");
        if (!layer->visible || layer->locked) {
            SetStatus("Show and unlock Default before placing new objects.");
            return;
        }
        if (_armedModel == AssetLibrary::kNone) {
            return;
        }
        if (!_armedShown) {
            SetStatus("Point at the world to put it down");
            return;
        }

        char message[192];
        if (_armedIsEffect) {
            const std::uint64_t placed = _effects.Place(static_cast<std::size_t>(_armedModel), _armedPosition, _armedRotation, EffectTuningFor(_armedModel, _armedScale));
            if (placed == 0) {
                SetStatus("That effect could not be started; the emitter budget may be spent");
                return;
            }
            HistoryStep step;
            step.label = "Place " + _effects.Find(placed)->label;
            AppendPlaced(step, SelectionKind::Effect, placed);
            _history.Record(std::move(step), &_layers);

            std::snprintf(message, sizeof(message), "Placed effect #%llu at %.1f, %.1f, %.1f", static_cast<unsigned long long>(placed), _armedPosition.x, _armedPosition.y, _armedPosition.z);
            SetStatus(message);
            RollBrush();
            return;
        }

        if (const WorldObjectRow *worldObject = WorldObjectAtLibraryRow(_armedModel)) {
            std::string error;
            const std::uint64_t placed = _scene.PlaceWorldObject(worldObject->kind, worldObject->model, nlohmann::json::object(), _armedPosition, _armedRotation, error);
            if (placed == 0) {
                SetStatus("That could not be placed: " + error);
                return;
            }
            HistoryStep step;
            step.label = "Place " + worldObject->name;
            AppendPlaced(step, SelectionKind::Placed, placed);
            _history.Record(std::move(step), &_layers);
            std::snprintf(message, sizeof(message), "Placed %s #%llu at %.1f, %.1f, %.1f -- its settings are in the Object tab", PlacedKindLabel(worldObject->kind), static_cast<unsigned long long>(placed), _armedPosition.x, _armedPosition.y, _armedPosition.z);
            SetStatus(message);
            RollBrush();
            return;
        }

        const Shared::Game::PropModelEntry *model = Shared::Game::PropModelAt(static_cast<std::size_t>(_armedModel));
        if (model == nullptr) {
            return;
        }
        const std::uint64_t placed = _scene.Place(std::string {model->path}, _armedPosition, _armedRotation, SDK::Vec3 {_armedScale, _armedScale, _armedScale}, _placement.physics);
        if (placed == 0) {
            SetStatus("That mesh could not be placed; the log has the reason");
            return;
        }

        HistoryStep step;
        step.label = "Place " + std::string {model->name};
        AppendPlaced(step, SelectionKind::Placed, placed);
        _history.Record(std::move(step), &_layers);
        std::snprintf(message, sizeof(message), "Placed #%llu at %.1f, %.1f, %.1f", static_cast<unsigned long long>(placed), _armedPosition.x, _armedPosition.y, _armedPosition.z);
        SetStatus(message);

        RollBrush();
    }

    void MapEditor::AppendPlaced(HistoryStep &step, SelectionKind kind, std::uint64_t id) const {
        HistoryAction action;
        action.id = id;
        if (kind == SelectionKind::Light) {
            const PlacedLight *light = _lights.Find(id);
            if (light == nullptr) {
                return;
            }
            action.kind  = HistoryAction::Kind::LightPlace;
            action.light = *light;
        }
        else if (kind == SelectionKind::Effect) {
            const PlacedEffect *effect = _effects.Find(id);
            if (effect == nullptr) {
                return;
            }
            action.kind   = HistoryAction::Kind::EffectPlace;
            action.effect = *effect;
        }
        else {
            const PlacedObject *object = _scene.Find(id);
            if (object == nullptr) {
                return;
            }
            action.kind   = HistoryAction::Kind::Place;
            action.object = *object;
        }
        step.actions.push_back(std::move(action));
    }

    void MapEditor::DuplicateTogether(const std::vector<SelectionRef> &selection, HistoryStep &step, std::vector<SelectionRef> &copies) {
        // The whole selection moves by its own width along world X, so the
        // copy keeps its layout and sits beside the original, not inside it.
        float minX = std::numeric_limits<float>::max();
        float maxX = std::numeric_limits<float>::lowest();
        for (const SelectionRef &ref : selection) {
            if (ref.kind == SelectionKind::Placed) {
                if (const PlacedObject *object = _scene.Find(ref.id)) {
                    minX = std::min(minX, object->hasBounds ? object->worldBounds.min.x : object->position.x);
                    maxX = std::max(maxX, object->hasBounds ? object->worldBounds.max.x : object->position.x);
                }
            }
            else if (ref.kind == SelectionKind::Light) {
                if (const PlacedLight *light = _lights.Find(ref.id)) {
                    minX = std::min(minX, light->position.x);
                    maxX = std::max(maxX, light->position.x);
                }
            }
            else if (ref.kind == SelectionKind::Effect) {
                if (const PlacedEffect *effect = _effects.Find(ref.id)) {
                    minX = std::min(minX, effect->position.x);
                    maxX = std::max(maxX, effect->position.x);
                }
            }
            else if (ref.kind == SelectionKind::World) {
                if (const WorldEdit *edit = _world.Find(ref.id)) {
                    minX = std::min(minX, edit->hasBounds ? edit->worldBounds.min.x : edit->position.x);
                    maxX = std::max(maxX, edit->hasBounds ? edit->worldBounds.max.x : edit->position.x);
                }
            }
        }
        const float shift = minX <= maxX ? std::max(maxX - minX, 0.25F) : 0.25F;

        // A copied group is a new group, the same one for every member copied from it.
        std::unordered_set<std::string> taken;
        for (const PlacedObject &object : _scene.Objects()) {
            taken.insert(object.group);
        }
        for (const PlacedEffect &effect : _effects.Effects()) {
            taken.insert(effect.group);
        }
        for (const auto &entry : _lights.Lights()) {
            taken.insert(entry.group);
        }
        std::unordered_map<std::string, std::string> renamed;
        const auto copyGroup = [&taken, &renamed](const std::string &group) {
            if (group.empty()) {
                return std::string {};
            }
            const auto [entry, inserted] = renamed.try_emplace(group);
            if (inserted) {
                entry->second = UniqueGroup(group, taken);
                taken.insert(entry->second);
            }
            return entry->second;
        };

        for (const SelectionRef &ref : selection) {
            if (ref.kind == SelectionKind::World) {
                // A level object has no group to carry; CopyLevelObject records it and keeps its layer.
                if (const std::uint64_t copy = CopyLevelObject(ref.id, SDK::Vec3 {shift, 0.0F, 0.0F}, step)) {
                    copies.push_back(SelectionRef {SelectionKind::Placed, copy});
                }
                continue;
            }
            std::uint64_t copy = 0;
            if (ref.kind == SelectionKind::Placed) {
                if (const PlacedObject *source = _scene.Find(ref.id)) {
                    const PlacedObject record = *source;
                    const SDK::Vec3 position {record.position.x + shift, record.position.y, record.position.z};
                    std::string ignored;
                    copy = record.kind != PlacedKind::Prop ? _scene.PlaceWorldObject(record.kind, record.model, record.settings, position, record.rotation, ignored) : _scene.Place(record.model, position, record.rotation, record.scale, record.physics);
                    _scene.SetGroup(copy, copyGroup(record.group));
                }
            }
            else if (ref.kind == SelectionKind::Light) {
                if (const PlacedLight *source = _lights.Find(ref.id)) {
                    const PlacedLight record = *source;
                    const SDK::Vec3 position {record.position.x + shift, record.position.y, record.position.z};
                    copy = _lights.Place(position, record.tuning);
                    _lights.SetGroup(copy, copyGroup(record.group));
                }
            }
            else if (ref.kind == SelectionKind::Effect) {
                if (const PlacedEffect *source = _effects.Find(ref.id)) {
                    const PlacedEffect record = *source;
                    const SDK::Vec3 position {record.position.x + shift, record.position.y, record.position.z};
                    copy = _effects.Place(record.effectIndex, position, record.rotation, record.tuning);
                    _effects.SetGroup(copy, copyGroup(record.group));
                }
            }
            if (copy != 0) {
                AppendPlaced(step, ref.kind, copy);
                std::string layerError;
                _layers.Assign({LayerKey({ref.kind, copy})}, _layers.LayerOf(LayerKey(ref)), layerError);
                copies.push_back(SelectionRef {ref.kind, copy});
            }
        }
    }

    std::string MapEditor::CatalogModelOf(const WorldEdit &edit) {
        return CatalogModelForLevelMesh(WorldObjects::MeshPath(edit));
    }

    std::uint64_t MapEditor::CopyLevelObject(std::uint64_t worldId, const SDK::Vec3 &offset, HistoryStep &step) {
        const WorldEdit *edit = _world.Find(worldId);
        if (edit == nullptr) {
            return 0;
        }
        const std::string model = CatalogModelOf(*edit);
        if (model.empty()) {
            return 0;
        }
        const SDK::Vec3 position {edit->position.x + offset.x, edit->position.y + offset.y, edit->position.z + offset.z};
        const SDK::Quat rotation = edit->rotation;
        const SDK::Vec3 scale    = edit->scale;
        const std::uint64_t copy = _scene.Place(model, position, rotation, scale, KCDC::Common::Spawn::PropPhysics::Static);
        if (copy == 0) {
            return 0;
        }
        AppendPlaced(step, SelectionKind::Placed, copy);
        std::string layerError;
        _layers.Assign({LayerKey({SelectionKind::Placed, copy})}, _layers.LayerOf(LayerKey({SelectionKind::World, worldId})), layerError);
        return copy;
    }

    void MapEditor::ClearUnresolved() {
        _unresolvedDocument = MapDocument {};
        _unresolvedPublished.reset();
    }

    void MapEditor::ImportDocument(const MapDocument &document, std::shared_ptr<const AuthoredHistoryState> authoredBefore, std::shared_ptr<const AuthoredHistoryState> authoredAfter) {
        if (authoredAfter) {
            _collisionError.clear();
            _areaCollision.ReplacePreview(authoredAfter->document.areas, _collisionError);
        }
        HistoryStep step;
        step.label = "Load a map";

        std::size_t placed  = 0;
        std::size_t unknown = 0;
        for (const DocumentObject &object : document.objects) {
            // Only catalog meshes are placed, and doors, smart objects and items the catalogs know.
            std::uint64_t id = 0;
            if (object.kind != PlacedKind::Prop) {
                std::string ignored;
                id = _scene.PlaceWorldObject(object.kind, object.model, object.settings, object.position, object.rotation, ignored);
            }
            else if (Shared::Game::FindPropModel(object.model) != nullptr) {
                id = _scene.Place(object.model, object.position, object.rotation, object.scale, object.physics);
            }
            if (id == 0) {
                ++unknown;
                _unresolvedDocument.objects.push_back(object);
                continue;
            }
            ++placed;
            _scene.SetGroup(id, object.group);
            std::string layerError;
            _layers.Assign({LayerKey({SelectionKind::Placed, id})}, object.layer, layerError);
            AppendPlaced(step, SelectionKind::Placed, id);
        }

        std::size_t effects = 0;
        for (const DocumentEffect &entry : document.effects) {
            const std::size_t index = Shared::Game::FindVfxEffectIndex(entry.effect);
            const std::uint64_t id  = index != Shared::Game::kNoVfxEffect ? _effects.Place(index, entry.position, entry.rotation, entry.tuning) : 0;
            if (id == 0) {
                ++unknown;
                _unresolvedDocument.effects.push_back(entry);
                continue;
            }
            ++effects;
            _effects.SetGroup(id, entry.group);
            std::string layerError;
            _layers.Assign({LayerKey({SelectionKind::Effect, id})}, entry.layer, layerError);
            AppendPlaced(step, SelectionKind::Effect, id);
        }

        std::size_t lights = 0;
        for (const DocumentLight &entry : document.lights) {
            const auto *layer      = _layers.Find(entry.layer);
            const std::uint64_t id = _lights.Import(entry.position, entry.tuning, !layer || layer->visible);
            if (id == 0) {
                ++unknown;
                _unresolvedDocument.lights.push_back(entry);
                continue;
            }
            ++lights;
            _lights.SetGroup(id, entry.group);
            std::string layerError;
            _layers.Assign({LayerKey({SelectionKind::Light, id})}, entry.layer, layerError);
            AppendPlaced(step, SelectionKind::Light, id);
        }

        std::size_t matched = 0;
        std::size_t missing = 0;
        std::size_t waiting = 0;
        for (const Shared::Game::LevelEditDefinition &entry : document.world) {
            const std::uint64_t id = _world.ApplyDocument(entry);
            const WorldEdit *edit  = id != 0 ? _world.Find(id) : nullptr;
            if (edit == nullptr) {
                ++missing;
                _unresolvedDocument.world.push_back(entry);
                continue;
            }
            ++matched;
            waiting += edit->loaded ? 0 : 1;
            if (entry.moved) {
                HistoryAction action;
                action.kind   = HistoryAction::Kind::WorldTransform;
                action.id     = id;
                action.before = TransformOf(edit->originalPosition, edit->originalRotation, edit->originalScale);
                action.after  = TransformOf(edit->position, edit->rotation, edit->scale);
                step.actions.push_back(action);
            }
            if (entry.hidden) {
                HistoryAction action;
                action.kind         = HistoryAction::Kind::WorldHide;
                action.id           = id;
                action.hiddenBefore = false;
                action.hiddenAfter  = true;
                step.actions.push_back(action);
            }
            if (!entry.model.empty()) {
                HistoryAction action;
                action.kind             = HistoryAction::Kind::WorldReplace;
                action.id               = id;
                action.replacementAfter = edit->replacement;
                step.actions.push_back(action);
            }
        }
        for (const auto &member : document.worldLayers) {
            const auto id = _world.ApplyDocument(member.object);
            std::string error;
            if (id) {
                _layers.Assign({LayerKey({SelectionKind::World, id})}, member.layer, error);
            }
        }
        if (authoredBefore && authoredAfter) {
            HistoryAction action;
            action.kind           = HistoryAction::Kind::Authored;
            action.authoredBefore = std::move(authoredBefore);
            action.authoredAfter  = std::move(authoredAfter);
            step.actions.push_back(std::move(action));
        }
        _history.Record(std::move(step), &_layers);
        if (unknown != 0 || missing != 0) {
            _unresolvedPublished = std::make_shared<const MapDocument>(_unresolvedDocument);
        }

        char waitingText[96] {};
        if (waiting != 0) {
            std::snprintf(waitingText, sizeof(waitingText), "; %zu apply when their object streams in", waiting);
        }
        char message[320];
        std::snprintf(message, sizeof(message), "Loaded %zu object%s, %zu effect%s, %zu light%s and %zu level edit%s%s%s%s", placed, placed == 1 ? "" : "s", effects, effects == 1 ? "" : "s", lights, lights == 1 ? "" : "s", matched, matched == 1 ? "" : "s",
            unknown != 0 ? "; some entries are not in the catalogs or could not start" : "", missing != 0 ? "; some level edits name nothing" : "", waitingText);
        SetStatus(message);
    }

    // --- Commands -------------------------------------------------------------

    void MapEditor::Apply(const Command &command) {
        if (command.kind == CommandKind::OpenNativeMap) {
            std::string error;
            if (!_mapTravel.Open(_camera, error)) {
                SetStatus(error);
            }
            return;
        }
        if (command.kind == CommandKind::CancelMapTravel) {
            _mapTravel.Cancel();
            return;
        }
        // Drop viewport actions queued before map input took ownership.
        if (_mapTravel.Active() && command.kind != CommandKind::Mcp && command.kind != CommandKind::SyncAreaCollision) {
            return;
        }
        if (command.mcpGeneration != 0 && (!_mcp.Available() || command.mcpGeneration != _mcp.Generation() || !Core::gApplication || !Core::gApplication->IsOfflineBuilderReady())) {
            return;
        }
        // Check permissions again on the owner thread: a layer may have been
        // hidden or locked since the UI queued the edit.
        switch (command.kind) {
        case CommandKind::Transform:
        case CommandKind::SetPhysics:
        case CommandKind::SetSettings:
        case CommandKind::Rebuild:
        case CommandKind::Duplicate:
        case CommandKind::Remove:
            if (!CanEdit({command.target, command.id})) {
                SetStatus("Object layer is hidden or locked.");
                return;
            }
            if (command.kind == CommandKind::Transform && command.enable) {
                for (const auto &ref : SelectionRefs()) {
                    if (!CanEdit(ref)) {
                        SetStatus("Selection includes a hidden or locked layer.");
                        return;
                    }
                }
            }
            break;
        case CommandKind::HideWorld:
        case CommandKind::ReplaceWorld:
        case CommandKind::RestoreWorld:
            if (!CanEdit({SelectionKind::World, command.id})) {
                SetStatus("Object layer is hidden or locked.");
                return;
            }
            break;
        case CommandKind::SetLightTuning:
            if (!CanEdit({SelectionKind::Light, command.id})) {
                SetStatus("Light layer is hidden or locked.");
                return;
            }
            break;
        case CommandKind::SetEffectTuning:
            if (!CanEdit({SelectionKind::Effect, command.id})) {
                SetStatus("Effect layer is hidden or locked.");
                return;
            }
            break;
        case CommandKind::RestoreAllWorld:
            for (const auto &object : _world.Edits()) {
                if (!CanEdit({SelectionKind::World, object.id})) {
                    SetStatus("A world layer is hidden or locked.");
                    return;
                }
            }
            break;
        case CommandKind::Clear:
            for (const auto &object : _scene.Objects()) {
                if (!CanEdit({SelectionKind::Placed, object.id})) {
                    SetStatus("A prop layer is hidden or locked.");
                    return;
                }
            }
            for (const auto &object : _effects.Effects()) {
                if (!CanEdit({SelectionKind::Effect, object.id})) {
                    SetStatus("An effect layer is hidden or locked.");
                    return;
                }
            }
            for (const auto &light : _lights.Lights()) {
                if (!CanEdit({SelectionKind::Light, light.id})) {
                    SetStatus("A light layer is hidden or locked.");
                    return;
                }
            }
            break;
        case CommandKind::DeleteSelection:
        case CommandKind::DuplicateSelection:
        case CommandKind::ConvertSelection:
        case CommandKind::GroupSelection:
        case CommandKind::DropToGround:
            for (const auto &ref : SelectionRefs()) {
                if (!CanEdit(ref)) {
                    SetStatus("Selection includes a hidden or locked layer.");
                    return;
                }
            }
            break;
        default: break;
        }
        char message[256];

        switch (command.kind) {
        case CommandKind::SyncAreaCollision: {
            if (command.id == _collisionContext.load() && command.document && Core::gApplication && Core::gApplication->IsOfflineBuilderReady()) {
                _collisionError.clear();
                if (!_areaCollision.ReplacePreview(command.document->areas, _collisionError)) {
                    SetStatus(_collisionError);
                }
                _collisionResults[command.serial] = _collisionError;
                while (_collisionResults.size() > 128) {
                    _collisionResults.erase(_collisionResults.begin());
                }
            }
            break;
        }
        case CommandKind::RecordAuthored: {
            HistoryAction action;
            action.kind           = HistoryAction::Kind::Authored;
            action.authoredBefore = command.authoredBefore;
            action.authoredAfter  = command.authoredAfter;
            HistoryStep step;
            step.label = command.group;
            step.actions.push_back(std::move(action));
            _history.Record(std::move(step), &_layers);
            break;
        }
        case CommandKind::Layer: {
            std::string error;
            if (!ApplyLayerAction(command.layerAction, error)) {
                SetStatus(error);
            }
            break;
        }
        case CommandKind::Mcp: _mcp.ExecuteGame(*this, command.mcp); break;
        case CommandKind::ProbeGround: {
            _groundResult = {command.id, KCDC::Common::ToGlm(command.position), false};
            std::vector<SDK::IPhysicalEntity *> skip;
            for (const auto &ref : command.groundIgnore) {
                SDK::IPhysicalEntity *collider = nullptr;
                if (ref.kind == SelectionKind::World) {
                    collider = _world.Collider(ref.id);
                }
                else if (ref.kind == SelectionKind::Placed) {
                    const auto *object = _scene.Find(ref.id);
                    auto *system       = SDK::IEntitySystem::GetInstance();
                    auto *entity       = object && system ? system->GetEntity(object->entityId) : nullptr;
                    if (entity) {
                        collider = entity->GetPhysics();
                    }
                }
                if (collider) {
                    skip.push_back(collider);
                }
            }
            SDK::Vec3 point;
            _areaCollision.AppendBodies(command.groundIgnoreAreas, skip);
            auto *physics = SDK::CPhysicalWorld::GetInstance();
            if (physics && physics->ResolveGround(command.position, kGroundProbeUp, kGroundProbeDown, point, skip.data(), static_cast<int>(skip.size()))) {
                _groundResult.position = KCDC::Common::ToGlm(point);
                _groundResult.hit      = true;
            }
            break;
        }
        case CommandKind::PatrolPreview:
            if (command.patrolRoute && IsOpen() && Core::gApplication) {
                _patrolPreview.Start(*command.patrolRoute, command.previewRole, Core::gApplication->GetCurrentLevelName());
            }
            else {
                _patrolPreview.Stop();
            }
            break;
        case CommandKind::ReplaceDocument:
            _areaCollision.Reset();
            EditorLayerRender::Get().Clear();
            _patrolPreview.Stop();
            Disarm();
            _scene.Clear();
            ClearUnresolved();
            _effects.Clear();
            _lights.Clear();
            _world.RestoreAll();
            ClearSelection();
            if (command.document != nullptr) {
                _layers = command.document->layers;
                ImportDocument(*command.document);
                _collisionError.clear();
                _areaCollision.ReplacePreview(command.document->areas, _collisionError);
                if (command.document->camera) {
                    const auto &pose = *command.document->camera;
                    _camera.SetPosition(pose.position);
                    _camera.SetRotation(pose.yawDegrees, pose.pitchDegrees);
                }
            }
            // Another project, not an edit of this one: nothing to undo back into.
            _history.Clear();
            _layersPublished = std::make_shared<const EditorLayers>(_layers);
            break;
        case CommandKind::GroupSelection: {
            HistoryStep step;
            step.label = command.group.empty() ? "Ungroup" : "Group selection";
            for (const SelectionRef &ref : SelectionRefs()) {
                HistoryAction action;
                action.id         = ref.id;
                action.groupAfter = command.group;
                if (ref.kind == SelectionKind::Placed) {
                    const PlacedObject *object = _scene.Find(ref.id);
                    if (!object) {
                        continue;
                    }
                    action.kind        = HistoryAction::Kind::Group;
                    action.groupBefore = object->group;
                    _scene.SetGroup(ref.id, command.group);
                }
                else if (ref.kind == SelectionKind::Light) {
                    const PlacedLight *light = _lights.Find(ref.id);
                    if (!light) {
                        continue;
                    }
                    action.kind        = HistoryAction::Kind::LightGroup;
                    action.groupBefore = light->group;
                    _lights.SetGroup(ref.id, command.group);
                }
                else if (ref.kind == SelectionKind::Effect) {
                    const PlacedEffect *effect = _effects.Find(ref.id);
                    if (!effect) {
                        continue;
                    }
                    action.kind        = HistoryAction::Kind::EffectGroup;
                    action.groupBefore = effect->group;
                    _effects.SetGroup(ref.id, command.group);
                }
                else {
                    continue;
                }
                if (action.groupBefore != action.groupAfter) {
                    step.actions.push_back(std::move(action));
                }
            }
            if (!step.actions.empty()) {
                _history.Record(std::move(step), &_layers);
            }
            break;
        }
        case CommandKind::PlaceArmed: PlaceArmed(); break;
        case CommandKind::Preview: SetPreview(command.enable ? static_cast<int>(command.id) : AssetLibrary::kNone, command.effect); break;

        case CommandKind::PlaceLight: {
            const auto *layer = _layers.Find(_layers.Active());
            if (!layer || !layer->visible || layer->locked || (_layers.Active() != "default" && _layers.Members().size() >= EditorLayers::kMaxMembers)) {
                SetStatus("Choose a visible, unlocked layer with room for another object.");
                break;
            }
            const auto id = _lights.Place(command.position, command.lightTuning);
            if (!id) {
                SetStatus("The light could not be created.");
                break;
            }
            std::string error;
            _layers.Assign({LayerKey({SelectionKind::Light, id})}, _layers.Active(), error);
            HistoryStep step;
            step.label = "Place light";
            AppendPlaced(step, SelectionKind::Light, id);
            _history.Record(std::move(step), &_layers);
            SelectRef({SelectionKind::Light, id}, false);
            SetStatus("Placed point light");
            break;
        }
        case CommandKind::SetLightTuning: {
            const auto *entry = _lights.Find(command.id);
            if (!entry) {
                break;
            }
            HistoryAction action;
            action.kind              = HistoryAction::Kind::LightTuning;
            action.id                = command.id;
            action.lightTuningBefore = entry->tuning;
            action.lightTuningAfter  = command.lightTuning;
            if (!_lights.SetTuning(command.id, command.lightTuning)) {
                SetStatus("The light settings could not be applied.");
                break;
            }
            HistoryStep step;
            step.label = "Edit light";
            step.actions.push_back(action);
            _history.Record(std::move(step), &_layers);
            break;
        }
        case CommandKind::SetEffectTuning: {
            const PlacedEffect *effect = _effects.Find(command.id);
            if (effect == nullptr) {
                break;
            }
            HistoryAction action;
            action.kind         = HistoryAction::Kind::EffectTuning;
            action.id           = command.id;
            action.tuningBefore = effect->tuning;
            action.tuningAfter  = command.tuning;
            if (!_effects.SetTuning(command.id, command.tuning)) {
                SetStatus("That effect would not start again with those settings");
                break;
            }
            HistoryStep step;
            step.label   = "Tune " + effect->label;
            step.gesture = command.gesture;
            step.actions.push_back(action);
            _history.Record(std::move(step), &_layers);
            break;
        }

        case CommandKind::DeleteSelection: {
            HistoryStep step;
            std::size_t deleted = 0;
            std::size_t hidden  = 0;
            for (const SelectionRef &ref : SelectionRefs()) {
                if (ref.kind == SelectionKind::Placed) {
                    const PlacedObject *object = _scene.Find(ref.id);
                    if (object == nullptr) {
                        continue;
                    }
                    HistoryAction action;
                    action.kind   = HistoryAction::Kind::Delete;
                    action.id     = ref.id;
                    action.object = *object;
                    if (_scene.Remove(ref.id)) {
                        step.actions.push_back(std::move(action));
                        ++deleted;
                    }
                    continue;
                }

                if (ref.kind == SelectionKind::Light) {
                    const PlacedLight *light = _lights.Find(ref.id);
                    if (light == nullptr) {
                        continue;
                    }
                    HistoryAction action;
                    action.kind  = HistoryAction::Kind::LightDelete;
                    action.id    = ref.id;
                    action.light = *light;
                    if (_lights.Remove(ref.id)) {
                        step.actions.push_back(std::move(action));
                        ++deleted;
                    }
                    continue;
                }

                if (ref.kind == SelectionKind::Effect) {
                    const PlacedEffect *effect = _effects.Find(ref.id);
                    if (effect == nullptr) {
                        continue;
                    }
                    HistoryAction action;
                    action.kind   = HistoryAction::Kind::EffectDelete;
                    action.id     = ref.id;
                    action.effect = *effect;
                    if (_effects.Remove(ref.id)) {
                        step.actions.push_back(std::move(action));
                        ++deleted;
                    }
                    continue;
                }

                const WorldEdit *edit = _world.Find(ref.id);
                if (edit == nullptr || edit->hidden) {
                    continue;
                }
                HistoryAction action;
                action.kind         = HistoryAction::Kind::WorldHide;
                action.id           = ref.id;
                action.hiddenBefore = false;
                action.hiddenAfter  = true;
                if (_world.SetHidden(ref.id, true)) {
                    step.actions.push_back(action);
                    ++hidden;
                }
            }

            step.label = "Delete the selection";
            _history.Record(std::move(step), &_layers);

            // Hidden level objects stay selected so they can be put back.
            _extra.EraseIf([](const SelectionRef &ref) {
                return ref.kind != SelectionKind::World;
            });
            if (_selectedKind != SelectionKind::World) {
                _selectedKind = SelectionKind::None;
                _selected     = 0;
                if (!_extra.Empty()) {
                    const SelectionRef promoted = _extra.PopFront();
                    _selectedKind               = promoted.kind;
                    _selected                   = promoted.id;
                }
            }

            std::snprintf(message, sizeof(message), "Deleted %zu, took %zu of the level's own out -- Ctrl+Z brings them back", deleted, hidden);
            SetStatus(message);
            break;
        }

        case CommandKind::DuplicateSelection: {
            const std::vector<SelectionRef> selection = SelectionRefs();
            const auto customCopies                   = std::count_if(selection.begin(), selection.end(), [this](const SelectionRef &ref) {
                return (ref.kind == SelectionKind::Placed || ref.kind == SelectionKind::Effect || ref.kind == SelectionKind::Light) && _layers.LayerOf(LayerKey(ref)) != "default";
            });
            if (_layers.Members().size() + static_cast<std::size_t>(customCopies) > EditorLayers::kMaxMembers) {
                SetStatus("Too many layer members to duplicate this selection.");
                break;
            }
            std::vector<SelectionRef> copies;
            HistoryStep step;
            if (selection.size() == 1) {
                // One object steps along its own X by its own width, so a turned fence panel continues.
                const SelectionRef &ref = selection.front();
                if (ref.kind == SelectionKind::World) {
                    if (const WorldEdit *edit = _world.Find(ref.id)) {
                        if (const std::uint64_t copy = CopyLevelObject(ref.id, LevelCopyStep(edit->rotation, edit->hasBounds ? &edit->worldBounds : nullptr), step)) {
                            copies.push_back(SelectionRef {SelectionKind::Placed, copy});
                        }
                    }
                }
                const std::uint64_t copy = ref.kind == SelectionKind::Placed ? _scene.Duplicate(ref.id) : ref.kind == SelectionKind::Light ? _lights.Duplicate(ref.id) : ref.kind == SelectionKind::Effect ? _effects.Duplicate(ref.id) : 0;
                if (copy != 0) {
                    AppendPlaced(step, ref.kind, copy);
                    std::string layerError;
                    _layers.Assign({LayerKey({ref.kind, copy})}, _layers.LayerOf(LayerKey(ref)), layerError);
                    copies.push_back(SelectionRef {ref.kind, copy});
                }
            }
            else {
                DuplicateTogether(selection, step, copies);
            }
            if (copies.empty()) {
                SetStatus("Nothing to duplicate: a level object copies only when its mesh is in the asset library");
                break;
            }

            step.label = copies.size() == 1 ? std::string {"Duplicate"} : "Duplicate " + std::to_string(copies.size());
            _history.Record(std::move(step), &_layers);

            ClearSelection();
            for (const SelectionRef &copy : copies) {
                SelectRef(copy, true);
            }
            std::snprintf(message, sizeof(message), "Duplicated %zu", copies.size());
            SetStatus(message);
            break;
        }

        case CommandKind::ConvertSelection: {
            HistoryStep step;
            std::vector<SelectionRef> copies;
            std::size_t levelObjects = 0;
            for (const SelectionRef &ref : SelectionRefs()) {
                const WorldEdit *edit = ref.kind == SelectionKind::World ? _world.Find(ref.id) : nullptr;
                if (edit == nullptr || edit->hidden) {
                    continue;
                }
                ++levelObjects;
                const std::uint64_t copy = CopyLevelObject(ref.id, SDK::Vec3 {}, step);
                if (copy == 0) {
                    continue;
                }
                // The copy takes the original's place: same pose, and the original goes out of the world.
                HistoryAction action;
                action.kind         = HistoryAction::Kind::WorldHide;
                action.id           = ref.id;
                action.hiddenBefore = false;
                action.hiddenAfter  = true;
                if (_world.SetHidden(ref.id, true)) {
                    step.actions.push_back(action);
                }
                copies.push_back(SelectionRef {SelectionKind::Placed, copy});
            }
            if (copies.empty()) {
                SetStatus(levelObjects == 0 ? "Select a level object to convert" : "Nothing converted: the selected level objects' meshes are not in the asset library");
                break;
            }
            step.label = copies.size() == 1 ? std::string {"Convert to a prop"} : "Convert " + std::to_string(copies.size()) + " to props";
            _history.Record(std::move(step), &_layers);

            ClearSelection();
            for (const SelectionRef &copy : copies) {
                SelectRef(copy, true);
            }
            std::snprintf(message, sizeof(message), "Converted %zu of %zu level objects to props -- Ctrl+Z puts them back", copies.size(), levelObjects);
            SetStatus(message);
            break;
        }
        case CommandKind::SelectRegion:
            if (command.selection) {
                const SelectionOutcome outcome = RunSelection(*command.selection);
                if (!outcome.error.empty()) {
                    SetStatus(outcome.error);
                }
            }
            break;

        case CommandKind::Arm: Arm(static_cast<int>(command.id), command.effect); break;

        case CommandKind::Disarm:
            if (_armedModel != AssetLibrary::kNone) {
                Disarm();
                SetStatus("Stopped placing");
            }
            break;

        case CommandKind::Placement: {
            const bool rolled = command.placement.randomYaw != _placement.randomYaw || command.placement.randomScale != _placement.randomScale || command.placement.scaleMin != _placement.scaleMin || command.placement.scaleMax != _placement.scaleMax;
            _placement        = command.placement;
            if (rolled) {
                RollBrush();
            }
            break;
        }

        case CommandKind::RotateArmed: _armedYaw = std::fmod(_armedYaw + command.value + 360.0F, 360.0F); break;
        case CommandKind::ScaleArmed:
            if (!_armedFixedScale) {
                _armedScale = std::clamp(_armedScale * command.value, kMinBrushScale, kMaxBrushScale);
            }
            break;

        case CommandKind::Transform: {
            HistoryAction action;
            action.id = command.id;

            bool moved = false;
            if (command.target == SelectionKind::Light) {
                const PlacedLight *light = _lights.Find(command.id);
                if (light != nullptr) {
                    action.kind   = HistoryAction::Kind::LightMove;
                    action.before = TransformOf(light->position, SDK::Quat {0, 0, 0, 1}, SDK::Vec3 {1.0F, 1.0F, 1.0F});
                    moved         = _lights.SetTransform(command.id, command.position);
                    action.after  = TransformOf(light->position, SDK::Quat {0, 0, 0, 1}, SDK::Vec3 {1.0F, 1.0F, 1.0F});
                }
            }
            else if (command.target == SelectionKind::Effect) {
                const PlacedEffect *effect = _effects.Find(command.id);
                if (effect != nullptr) {
                    action.kind   = HistoryAction::Kind::EffectMove;
                    action.before = TransformOf(effect->position, effect->rotation, SDK::Vec3 {1.0F, 1.0F, 1.0F});
                    moved         = _effects.SetTransform(command.id, command.position, command.rotation);
                    action.after  = TransformOf(effect->position, effect->rotation, SDK::Vec3 {1.0F, 1.0F, 1.0F});
                }
            }
            else if (command.target == SelectionKind::World) {
                const WorldEdit *edit = _world.Find(command.id);
                if (edit != nullptr) {
                    action.kind   = HistoryAction::Kind::WorldTransform;
                    action.before = TransformOf(edit->position, edit->rotation, edit->scale);
                    moved         = _world.SetTransform(command.id, command.position, command.rotation, command.scale);
                    action.after  = TransformOf(edit->position, edit->rotation, edit->scale);
                }
            }
            else {
                const PlacedObject *object = _scene.Find(command.id);
                if (object != nullptr) {
                    action.kind   = HistoryAction::Kind::Transform;
                    action.before = TransformOf(object->position, object->rotation, object->scale);
                    moved         = _scene.SetTransform(command.id, command.position, command.rotation, command.scale);
                    action.after  = TransformOf(object->position, object->rotation, object->scale);
                }
            }

            if (!moved) {
                SetStatus("That object could not be moved");
                break;
            }

            HistoryStep step;
            step.label   = "Move";
            step.gesture = command.gesture;
            step.actions.push_back(action);

            // The group follows the primary rigidly: turned about it, scaled from it.
            const bool isPrimary = command.target == _selectedKind && command.id == _selected;
            if (command.enable && isPrimary && !_extra.Empty()) {
                const glm::quat before = glm::normalize(KCDC::Common::ToGlm(action.before.rotation));
                const glm::quat after  = glm::normalize(KCDC::Common::ToGlm(action.after.rotation));
                const glm::quat turn   = after * glm::inverse(before);

                const float ratios[3] {action.before.scale.x > 0.0F ? action.after.scale.x / action.before.scale.x : 1.0F, action.before.scale.y > 0.0F ? action.after.scale.y / action.before.scale.y : 1.0F,
                    action.before.scale.z > 0.0F ? action.after.scale.z / action.before.scale.z : 1.0F};
                float ratio = ratios[0];
                for (const float candidate : ratios) {
                    if (std::fabs(candidate - 1.0F) > std::fabs(ratio - 1.0F)) {
                        ratio = candidate;
                    }
                }

                const glm::vec3 pivotBefore = KCDC::Common::ToGlm(action.before.position);
                const glm::vec3 pivotAfter  = KCDC::Common::ToGlm(action.after.position);

                for (const SelectionRef &ref : _extra) {
                    const std::optional<RefPose> pose = DescribeRef(ref);
                    if (!pose) {
                        continue;
                    }

                    const glm::vec3 offset   = KCDC::Common::ToGlm(pose->position) - pivotBefore;
                    const SDK::Vec3 movedTo  = KCDC::Common::ToNative(pivotAfter + turn * (offset * ratio));
                    const SDK::Quat turnedTo = KCDC::Common::ToNative(glm::normalize(turn * glm::normalize(KCDC::Common::ToGlm(pose->rotation))));
                    const SDK::Vec3 sizedTo {pose->scale.x * ratio, pose->scale.y * ratio, pose->scale.z * ratio};

                    HistoryAction follower;
                    follower.id     = ref.id;
                    follower.kind   = ref.kind == SelectionKind::World ? HistoryAction::Kind::WorldTransform : ref.kind == SelectionKind::Light ? HistoryAction::Kind::LightMove : ref.kind == SelectionKind::Effect ? HistoryAction::Kind::EffectMove : HistoryAction::Kind::Transform;
                    follower.before = TransformOf(pose->position, pose->rotation, pose->scale);

                    bool followed = false;
                    switch (ref.kind) {
                    case SelectionKind::World: followed = _world.SetTransform(ref.id, movedTo, turnedTo, sizedTo); break;
                    case SelectionKind::Light: followed = _lights.SetTransform(ref.id, movedTo); break;
                    case SelectionKind::Effect: followed = _effects.SetTransform(ref.id, movedTo, turnedTo); break;
                    default: followed = _scene.SetTransform(ref.id, movedTo, turnedTo, sizedTo); break;
                    }
                    const std::optional<RefPose> landed = DescribeRef(ref);
                    if (!followed || !landed) {
                        continue;
                    }
                    follower.after = TransformOf(landed->position, landed->rotation, landed->scale);
                    step.actions.push_back(follower);
                }
                step.label = "Move " + std::to_string(step.actions.size()) + " objects";
            }

            _history.Record(std::move(step), &_layers);
            break;
        }

        case CommandKind::SetPhysics: {
            const PlacedObject *object = _scene.Find(command.id);
            if (object == nullptr) {
                break;
            }
            HistoryAction action;
            action.kind          = HistoryAction::Kind::Physics;
            action.id            = command.id;
            action.physicsBefore = object->physics;
            action.physicsAfter  = command.physics;

            if (_scene.SetPhysics(command.id, command.physics)) {
                HistoryStep step;
                step.label = "Change physics";
                step.actions.push_back(action);
                _history.Record(std::move(step), &_layers);
                std::snprintf(message, sizeof(message), "#%llu is now %s", static_cast<unsigned long long>(command.id), KCDC::Common::Spawn::PropPhysicsToString(command.physics));
                SetStatus(message);
            }
            else {
                SetStatus("That object could not be rebuilt with the new physics");
            }
            break;
        }

        case CommandKind::SetSettings: {
            const PlacedObject *object = _scene.Find(command.id);
            if (object == nullptr) {
                break;
            }
            HistoryAction action;
            action.kind             = HistoryAction::Kind::Settings;
            action.id               = command.id;
            action.settingsBefore   = object->settings;
            const std::string label = object->name;
            std::string error;
            if (!_scene.SetSettings(command.id, command.settings, error)) {
                SetStatus("Those settings were refused: " + error);
                break;
            }
            action.settingsAfter = _scene.Find(command.id)->settings;
            if (action.settingsAfter != action.settingsBefore) {
                HistoryStep step;
                step.label = "Change " + label;
                step.actions.push_back(std::move(action));
                _history.Record(std::move(step), &_layers);
                SetStatus("Changed " + label + " -- the export carries it");
            }
            break;
        }

        case CommandKind::Rebuild: SetStatus(_scene.Rebuild(command.id) ? "Rebuilt, and its collision now matches its scale" : "That object could not be rebuilt"); break;

        case CommandKind::Duplicate: {
            if (_layers.LayerOf(LayerKey({SelectionKind::Placed, command.id})) != "default" && _layers.Members().size() >= EditorLayers::kMaxMembers) {
                SetStatus("Too many layer members to duplicate this object.");
                break;
            }
            const std::uint64_t copy = _scene.Duplicate(command.id);
            if (copy) {
                std::string error;
                _layers.Assign({LayerKey({SelectionKind::Placed, copy})}, _layers.LayerOf(LayerKey({SelectionKind::Placed, command.id})), error);
            }
            if (copy != 0) {
                HistoryStep step;
                step.label = "Duplicate";
                AppendPlaced(step, SelectionKind::Placed, copy);
                _history.Record(std::move(step), &_layers);
                _selectedKind = SelectionKind::Placed;
                _selected     = copy;
                std::snprintf(message, sizeof(message), "Duplicated as #%llu", static_cast<unsigned long long>(copy));
                SetStatus(message);
            }
            else {
                SetStatus("Nothing was duplicated");
            }
            break;
        }

        case CommandKind::Remove: {
            const PlacedObject *object = _scene.Find(command.id);
            if (object == nullptr) {
                break;
            }
            HistoryAction action;
            action.kind   = HistoryAction::Kind::Delete;
            action.id     = command.id;
            action.object = *object;

            if (_scene.Remove(command.id)) {
                HistoryStep step;
                step.label = "Delete " + action.object.name;
                step.actions.push_back(std::move(action));
                _history.Record(std::move(step), &_layers);
                if (_selectedKind == SelectionKind::Placed && _selected == command.id) {
                    ClearSelection();
                }
                SetStatus("Deleted -- Ctrl+Z brings it back");
            }
            break;
        }

        case CommandKind::Undo: {
            const auto authoredSerial = _history.AuthoredRestoreSerial();
            std::string label;
            std::vector<SelectionRef> touched;
            SetStatus(_history.Undo(_scene, _world, _effects, _lights, label, &_layers, &touched) ? "Undid: " + label : std::string {"Nothing to undo"});
            SelectTouched(touched);
            if (_history.AuthoredRestoreSerial() != authoredSerial) {
                _patrolPreview.Stop();
                if (!RestoreAreaCollision()) {
                    SetStatus(_collisionError);
                }
            }
            break;
        }

        case CommandKind::Redo: {
            const auto authoredSerial = _history.AuthoredRestoreSerial();
            std::string label;
            std::vector<SelectionRef> touched;
            SetStatus(_history.Redo(_scene, _world, _effects, _lights, label, &_layers, &touched) ? "Redid: " + label : std::string {"Nothing to redo"});
            SelectTouched(touched);
            if (_history.AuthoredRestoreSerial() != authoredSerial) {
                _patrolPreview.Stop();
                if (!RestoreAreaCollision()) {
                    SetStatus(_collisionError);
                }
            }
            break;
        }

        case CommandKind::Clear: {
            HistoryStep step;
            step.label = "Clear the scene";
            for (const PlacedObject &object : _scene.Objects()) {
                HistoryAction action;
                action.kind   = HistoryAction::Kind::Delete;
                action.id     = object.id;
                action.object = object;
                step.actions.push_back(std::move(action));
            }
            for (const PlacedLight &light : _lights.Lights()) {
                HistoryAction action;
                action.kind  = HistoryAction::Kind::LightDelete;
                action.id    = light.id;
                action.light = light;
                step.actions.push_back(std::move(action));
            }
            for (const PlacedEffect &effect : _effects.Effects()) {
                HistoryAction action;
                action.kind   = HistoryAction::Kind::EffectDelete;
                action.id     = effect.id;
                action.effect = effect;
                step.actions.push_back(std::move(action));
            }
            _scene.Clear();
            ClearUnresolved();
            _effects.Clear();
            _lights.Clear();
            _history.Record(std::move(step), &_layers);
            if (_selectedKind != SelectionKind::World) {
                ClearSelection();
            }
            SetStatus("Cleared everything the editor placed -- Ctrl+Z brings it back");
            break;
        }

        case CommandKind::Select: SelectRef(SelectionRef {command.id != 0 ? command.target : SelectionKind::None, command.id}, command.enable); break;

        case CommandKind::PickAt: {
            const PickResult picked = PickThrough(KCDC::Features::Camera::RayThroughNdc(_camera.GetPose(), command.ndcX, command.ndcY), PickEffort::Thorough);
            if (picked.light != 0) {
                SelectRef({SelectionKind::Light, picked.light}, command.enable);
                SetStatus("Selected point light");
                break;
            }
            if (picked.effect != 0) {
                SelectRef(SelectionRef {SelectionKind::Effect, picked.effect}, command.enable);
                const PlacedEffect *effect = _effects.Find(picked.effect);
                std::snprintf(message, sizeof(message), "Selected effect %s", effect != nullptr ? effect->name.c_str() : "?");
                SetStatus(message);
                break;
            }

            if (picked.world.kind != WorldPickKind::None) {
                const std::uint64_t adopted = _world.Adopt(picked.world);
                if (adopted != 0) {
                    SelectRef(SelectionRef {SelectionKind::World, adopted}, command.enable);
                    const WorldEdit *edit = _world.Find(adopted);
                    std::snprintf(message, sizeof(message), "Selected %s: %s", WorldPickKindToString(picked.world.kind), edit != nullptr ? edit->label.c_str() : "?");
                    SetStatus(message);
                }
                else {
                    std::snprintf(message, sizeof(message), "That is %s (collider owner %d) -- nothing to select", WorldPickKindToString(picked.world.kind), picked.world.foreignId);
                    SetStatus(message);
                    if (!command.enable) {
                        ClearSelection();
                    }
                }
                break;
            }

            if (picked.placed != 0) {
                SelectRef(SelectionRef {SelectionKind::Placed, picked.placed}, command.enable);
                std::snprintf(message, sizeof(message), "Selected #%llu", static_cast<unsigned long long>(picked.placed));
                SetStatus(message);
                break;
            }

            if (!command.enable) {
                ClearSelection();
            }
            std::snprintf(message, sizeof(message), "Nothing under the cursor (weighed %d level object%s)", picked.inspected, picked.inspected == 1 ? "" : "s");
            SetStatus(message);
            break;
        }

        case CommandKind::HideWorld: {
            const WorldEdit *edit = _world.Find(command.id);
            if (edit == nullptr) {
                break;
            }
            HistoryAction action;
            action.kind         = HistoryAction::Kind::WorldHide;
            action.id           = command.id;
            action.hiddenBefore = edit->hidden;
            action.hiddenAfter  = command.enable;

            if (_world.SetHidden(command.id, command.enable)) {
                HistoryStep step;
                step.label = command.enable ? "Take out " + edit->label : "Put back " + edit->label;
                step.actions.push_back(action);
                _history.Record(std::move(step), &_layers);
                SetStatus(command.enable ? "Taken out of the world, collision and all -- Ctrl+Z puts it back" : "Put back");
            }
            else {
                SetStatus("That one cannot be taken out");
            }
            break;
        }

        case CommandKind::ReplaceWorld: {
            const WorldEdit *edit = _world.Find(command.id);
            if (edit == nullptr) {
                break;
            }
            HistoryAction action;
            action.kind              = HistoryAction::Kind::WorldReplace;
            action.id                = command.id;
            action.replacementBefore = edit->replacement;
            action.replacementAfter  = command.model;
            const std::string label  = edit->label;

            if (_world.SetReplacement(command.id, command.model)) {
                HistoryStep step;
                step.label = command.model.empty() ? "Unswap " + label : "Swap " + label;
                step.actions.push_back(action);
                _history.Record(std::move(step), &_layers);
                SetStatus(command.model.empty() ? "Given back the way it was" : "Swapped, collision and all -- Ctrl+Z gives it back");
            }
            else {
                SetStatus("That mesh cannot stand in for it: pick one from the asset library");
            }
            break;
        }

        case CommandKind::RestoreWorld:
            if (_world.Restore(command.id, _layers.LayerOf(LayerKey({SelectionKind::World, command.id})) != "default")) {
                if (_selectedKind == SelectionKind::World && _selected == command.id) {
                    ClearSelection();
                }
                SetStatus("Put back the way it was found");
            }
            break;

        case CommandKind::RestoreAllWorld: {
            std::vector<std::uint64_t> ids;
            for (const auto &world : _world.Edits()) {
                ids.push_back(world.id);
            }
            for (auto id : ids) {
                _world.Restore(id, _layers.LayerOf(LayerKey({SelectionKind::World, id})) != "default");
            }
            if (_selectedKind == SelectionKind::World) {
                ClearSelection();
            }
            SetStatus("Put the level back the way it was found");
            break;
        }

        case CommandKind::Browse: {
            const float radius = std::clamp(command.value, 2.0F, kMaxBrowseRadius);
            std::vector<LevelRow> rows;
            _world.Browse(_camera.GetPose().position, radius, kMaxBrowseRows, rows, _levelTruncated);

            // The editor's own props belong to the scene list.
            std::erase_if(rows, [this](const LevelRow &row) {
                WorldPick pick;
                pick.kind     = row.kind;
                pick.node     = row.node;
                pick.entityId = row.entityId;
                return _scene.FindByPick(pick) != 0 || (row.kind == WorldPickKind::Entity && row.entityId == _camera.GetAnchorEntityId());
            });

            std::snprintf(message, sizeof(message), "Listed %zu level object%s within %.0f m%s", rows.size(), rows.size() == 1 ? "" : "s", radius, _levelTruncated ? " (too many -- bring the radius down)" : "");
            _levelRows = std::make_shared<const std::vector<LevelRow>>(std::move(rows));
            SetStatus(message);
            break;
        }

        case CommandKind::SelectBrowsed: {
            WorldPick pick;
            pick.kind     = command.pickKind;
            pick.node     = command.node;
            pick.entityId = command.entityId;

            if (const auto own = _scene.FindByPick(pick)) {
                if (!CanEdit({SelectionKind::Placed, own})) {
                    SetStatus("Object layer is hidden or locked.");
                    break;
                }
                SelectRef(SelectionRef {SelectionKind::Placed, own}, false);
                break;
            }

            // The node may have been streamed out since it was listed.
            if (pick.kind == WorldPickKind::RenderNode && !_world.IsNodeStanding(pick.node, command.position)) {
                SetStatus("That one is no longer in the level -- list again");
                break;
            }

            std::uint64_t id = _world.FindByPick(pick);
            if (id == 0) {
                const auto *defaultLayer = _layers.Find("default");
                if (!defaultLayer->visible || defaultLayer->locked) {
                    SetStatus("Default layer is hidden or locked.");
                    break;
                }
                id = _world.Adopt(pick);
            }
            if (id == 0) {
                SetStatus("That one cannot be selected; the game has hidden it itself");
                break;
            }
            if (!CanEdit({SelectionKind::World, id})) {
                SetStatus("Object layer is hidden or locked.");
                break;
            }

            SelectRef({SelectionKind::World, id}, false);
            if (command.enable) {
                Command focus;
                focus.kind = CommandKind::Focus;
                Apply(focus);
            }
            break;
        }

        case CommandKind::Focus: {
            SelectionInfo selection;
            BuildSelection(selection);
            if (selection.kind == SelectionKind::None) {
                break;
            }

            // Back off by size, so any object fills the same share of the screen.
            float radius     = 2.0F;
            SDK::Vec3 centre = selection.position;
            if (selection.hasBounds) {
                // The whole group, where it has boxes.
                SDK::AABB all = selection.worldBounds;
                for (const SelectionRef &ref : _extra) {
                    if (const std::optional<RefPose> pose = DescribeRef(ref); pose && pose->hasBounds) {
                        all.min = SDK::Vec3 {std::min(all.min.x, pose->bounds.min.x), std::min(all.min.y, pose->bounds.min.y), std::min(all.min.z, pose->bounds.min.z)};
                        all.max = SDK::Vec3 {std::max(all.max.x, pose->bounds.max.x), std::max(all.max.y, pose->bounds.max.y), std::max(all.max.z, pose->bounds.max.z)};
                    }
                }
                radius = std::max({all.max.x - all.min.x, all.max.y - all.min.y, all.max.z - all.min.z, 1.0F});
                centre = BoxCentre(all);
            }
            _camera.Focus(centre, radius * 2.0F);
            SetStatus("Framed the selection");
            break;
        }

        case CommandKind::DropToGround: {
            SelectionInfo selection;
            BuildSelection(selection);
            auto *physics = SDK::CPhysicalWorld::GetInstance();
            if (selection.kind == SelectionKind::None || physics == nullptr) {
                break;
            }

            SDK::Vec3 ground {};
            if (!physics->ResolveGround(selection.position, kGroundProbeUp, kGroundProbeDown, ground)) {
                SetStatus("No ground under that object");
                break;
            }

            // Keep the origin's height above the bottom of the box.
            const float footOffset = selection.kind != SelectionKind::Light && selection.hasBounds ? selection.position.z - selection.worldBounds.min.z : 0.0F;

            Command move;
            move.kind     = CommandKind::Transform;
            move.target   = selection.kind;
            move.id       = selection.id;
            move.position = SDK::Vec3 {selection.position.x, selection.position.y, ground.z + footOffset};
            move.rotation = selection.rotation;
            move.scale    = selection.scale;
            Apply(move);
            SetStatus("Dropped to the ground");
            break;
        }

        case CommandKind::Import:
            if (std::any_of(_layers.Layers().begin(), _layers.Layers().end(), [](const EditorLayer &layer) {
                    return !layer.visible || layer.locked;
                })) {
                SetStatus("Show and unlock layers before importing a document.");
                break;
            }
            if (command.document) {
                ImportDocument(*command.document, command.authoredBefore, command.authoredAfter);
            }
            break;

        case CommandKind::CameraSpeed: _camera.SetSpeed(command.value); break;
        case CommandKind::CameraFov: _camera.SetFieldOfView(command.value); break;
        case CommandKind::CameraPose: _camera.SetPose(command.position, command.direction); break;

        case CommandKind::CameraView:
            _camera.SetPosition(command.position);
            _camera.SetRotation(command.value, command.value2);
            break;

        case CommandKind::CameraToPlayer: {
            SDK::IEntity *player = KCDC::Common::LocalPlayer();
            if (player == nullptr) {
                SetStatus("There is no local player to go to");
                break;
            }
            const SDK::Vec3 &position = player->GetPos();
            _camera.SetPosition(SDK::Vec3 {position.x, position.y, position.z + 2.0F});
            SetStatus("Moved the camera to the player");
            break;
        }

        case CommandKind::PlayerToCamera: {
            auto *application = Core::gApplication.get();
            if (application == nullptr) {
                break;
            }

            // Stand the player on the ground under the camera.
            SDK::Vec3 target = _camera.GetPose().position;
            if (auto *physics = SDK::CPhysicalWorld::GetInstance()) {
                SDK::Vec3 ground {};
                if (physics->ResolveGround(target, 1.0F, kGroundProbeDown, ground)) {
                    target = SDK::Vec3 {ground.x, ground.y, ground.z + 0.2F};
                }
            }

            KCDC::Features::Player::TeleportRequest request;
            request.position = target;
            request.label    = "Map editor camera";
            application->GetPlayerTeleporter().QueueTeleport(request);
            SetStatus("Sent the player to the camera");
            break;
        }

        case CommandKind::CameraDolly: {
            // A share of the way to the surface under the cursor, never through it.
            const KCDC::Features::Camera::CameraPose &pose = _camera.GetPose();
            const SDK::Vec3 heading                        = KCDC::Features::Camera::RayThroughNdc(pose, command.ndcX, command.ndcY);

            SDK::Vec3 target {};
            SDK::Vec3 normal {};
            bool hit = false;
            ResolveAim(command.ndcX, command.ndcY, kAimRange, target, normal, hit);

            float step = std::max(_camera.GetSpeed() * 0.5F, 1.0F) * command.value;
            if (hit) {
                const float distance = Length(SDK::Vec3 {target.x - pose.position.x, target.y - pose.position.y, target.z - pose.position.z});
                step                 = std::clamp(distance * 0.18F, 0.1F, 200.0F) * command.value;
                if (step > 0.0F) {
                    step = std::min(step, std::max(distance - 0.5F, 0.0F));
                }
            }
            _camera.SetPosition(SDK::Vec3 {pose.position.x + heading.x * step, pose.position.y + heading.y * step, pose.position.z + heading.z * step});
            break;
        }

        case CommandKind::CameraGrab: {
            const SDK::Vec3 eye = _camera.GetPose().position;

            // Orbit the selection if any, otherwise the grabbed point.
            SelectionInfo selection;
            BuildSelection(selection);
            if (command.enable && selection.kind != SelectionKind::None) {
                _grabPivot = selection.hasBounds ? BoxCentre(selection.worldBounds) : selection.position;
            }
            else {
                SDK::Vec3 normal {};
                bool hit = false;
                ResolveAim(command.ndcX, command.ndcY, 20.0F, _grabPivot, normal, hit);
            }
            _grabDistance = std::max(Length(SDK::Vec3 {_grabPivot.x - eye.x, _grabPivot.y - eye.y, _grabPivot.z - eye.z}), 1.0F);
            break;
        }

        case CommandKind::CameraPan: {
            // Scaled at the grabbed depth, so that point stays under the cursor.
            const KCDC::Features::Camera::CameraPose &pose = _camera.GetPose();
            const float metresPerScreen                    = _grabDistance * 2.0F * std::tan(pose.fovDegrees * 0.5F * kDegreesToRad);
            const SDK::Vec3 right                          = KCDC::Features::Camera::PoseRight(pose);
            const SDK::Vec3 up                             = KCDC::Features::Camera::PoseUp(pose);
            const float across                             = -command.value * metresPerScreen;
            const float along                              = command.value2 * metresPerScreen;
            _camera.SetPosition(SDK::Vec3 {pose.position.x + right.x * across + up.x * along, pose.position.y + right.y * across + up.y * along, pose.position.z + right.z * across + up.z * along});
            break;
        }

        case CommandKind::CameraOrbit: {
            const KCDC::Features::Camera::CameraPose &pose = _camera.GetPose();
            SDK::Vec3 offset {pose.position.x - _grabPivot.x, pose.position.y - _grabPivot.y, pose.position.z - _grabPivot.z};
            const float radius = std::max(Length(offset), 0.5F);

            // Rebuilt from angles so pitch can be clamped.
            const float yaw      = std::atan2(-offset.x, -offset.y) * kRadToDegrees - command.value;
            const float pitchNow = std::asin(std::clamp(-offset.z / radius, -1.0F, 1.0F)) * kRadToDegrees;
            const float pitch    = std::clamp(pitchNow - command.value2, -kOrbitPitchLimit, kOrbitPitchLimit);
            const float yawRad   = yaw * kDegreesToRad;
            const float pitchRad = pitch * kDegreesToRad;
            const SDK::Vec3 facing {std::sin(yawRad) * std::cos(pitchRad), std::cos(yawRad) * std::cos(pitchRad), std::sin(pitchRad)};

            offset = SDK::Vec3 {-facing.x * radius, -facing.y * radius, -facing.z * radius};
            _camera.SetPose(SDK::Vec3 {_grabPivot.x + offset.x, _grabPivot.y + offset.y, _grabPivot.z + offset.z}, facing);
            break;
        }
        }
    }

    void MapEditor::RefreshHover() {
        if (!_camera.IsActive() || _looking.load(std::memory_order_acquire) || !_cursorOverWorld.load(std::memory_order_acquire)) {
            _hover = HoverInfo {};
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        if (_lastHover.time_since_epoch().count() != 0 && std::chrono::duration<float>(now - _lastHover).count() < kHoverIntervalSeconds) {
            return;
        }
        _lastHover = now;

        const KCDC::Features::Camera::CameraPose &pose = _camera.GetPose();
        const SDK::Vec3 heading                        = KCDC::Features::Camera::RayThroughNdc(pose, _cursorNdcX.load(std::memory_order_acquire), _cursorNdcY.load(std::memory_order_acquire));

        const ScenePick scene         = PickScene(heading, PickEffort::Quick);
        const std::uint64_t placedHit = scene.placed.id;
        const float placedDistance    = placedHit != 0 ? scene.placed.distance : kPickRange;
        const WorldPick &worldHit     = scene.world;

        _hover = HoverInfo {};

        // Resolved the way a click resolves it.
        float effectDistance          = kPickRange;
        const std::uint64_t effectHit = _effects.PickNearest(pose.position, heading, kPickRange, effectDistance, &_layers);
        const float otherDistance     = std::min(placedHit != 0 ? placedDistance : kPickRange, worldHit.kind != WorldPickKind::None ? worldHit.distance : kPickRange);
        float lightDistance           = kPickRange;
        const auto lightHit           = _lights.PickNearest(pose.position, heading, kPickRange, lightDistance, &_layers);
        if (lightHit && lightDistance <= std::min(otherDistance, effectHit ? effectDistance : kPickRange) + 0.01F) {
            _hover.valid      = true;
            _hover.selectable = true;
            _hover.label      = "Point light";
            _hover.point      = _lights.Find(lightHit)->position;
            _hover.distance   = lightDistance;
            return;
        }
        if (effectHit != 0 && effectDistance <= otherDistance + 0.01F) {
            const PlacedEffect *effect = _effects.Find(effectHit);
            _hover.valid               = true;
            _hover.selectable          = true;
            _hover.label               = effect->label;
            _hover.detail              = effect->name;
            _hover.distance            = effectDistance;
            _hover.point               = effect->position;
            return;
        }

        const bool worldWins = worldHit.kind != WorldPickKind::None && (placedHit == 0 || (worldHit.boundsOnly && worldHit.distance < placedDistance - 0.01F));
        if (placedHit != 0 && !worldWins) {
            const PlacedObject *object = _scene.Find(placedHit);
            if (object != nullptr) {
                _hover.valid      = true;
                _hover.selectable = true;
                _hover.label      = object->name;
                _hover.detail     = "placed by the editor";
                _hover.distance   = placedDistance;
                _hover.point      = SDK::Vec3 {pose.position.x + heading.x * placedDistance, pose.position.y + heading.y * placedDistance, pose.position.z + heading.z * placedDistance};
            }
            return;
        }

        if (worldHit.kind == WorldPickKind::None) {
            return;
        }

        DescribeWorldPick(worldHit, _hover.label, _hover.detail);
        _hover.valid      = true;
        _hover.selectable = worldHit.kind == WorldPickKind::RenderNode || worldHit.kind == WorldPickKind::Entity;
        _hover.point      = worldHit.hitPoint;
        _hover.distance   = worldHit.distance;
    }

    void MapEditor::BuildSelection(SelectionInfo &selection) const {
        selection = SelectionInfo {};

        if (_selectedKind == SelectionKind::Placed) {
            const PlacedObject *object = _scene.Find(_selected);
            if (object == nullptr) {
                return;
            }

            selection.kind                   = SelectionKind::Placed;
            selection.id                     = object->id;
            selection.label                  = object->name;
            selection.detail                 = object->kind == PlacedKind::Prop ? object->model : std::string(PlacedKindLabel(object->kind)) + ": " + object->model;
            selection.mesh                   = object->model;
            selection.placedKind             = object->kind;
            selection.model                  = object->model;
            selection.settings               = object->settings;
            selection.position               = object->position;
            selection.rotation               = object->rotation;
            selection.scale                  = object->scale;
            selection.worldBounds            = object->worldBounds;
            selection.hasBounds              = object->hasBounds;
            selection.physics                = object->physics;
            selection.collisionRebuildFailed = object->collisionRebuildFailed;
            return;
        }

        if (_selectedKind == SelectionKind::Effect) {
            const PlacedEffect *effect = _effects.Find(_selected);
            if (effect == nullptr) {
                return;
            }
            const Shared::Game::VfxEffectEntry *entry = Shared::Game::VfxEffectAt(effect->effectIndex);

            selection.kind        = SelectionKind::Effect;
            selection.id          = effect->id;
            selection.label       = effect->label;
            selection.detail      = effect->name;
            selection.className   = effect->name;
            selection.position    = effect->position;
            selection.rotation    = effect->rotation;
            selection.worldBounds = EditorEffects::BoundsOf(*effect);
            selection.hasBounds   = true;
            selection.tuning      = effect->tuning;
            selection.continuous  = entry == nullptr || entry->continuous;
            return;
        }

        if (_selectedKind == SelectionKind::Light) {
            if (const auto *entry = _lights.Find(_selected)) {
                selection.kind        = SelectionKind::Light;
                selection.id          = entry->id;
                selection.label       = "Point light";
                selection.position    = entry->position;
                selection.worldBounds = EditorLights::BoundsOf(*entry);
                selection.hasBounds   = true;
                selection.lightTuning = entry->tuning;
            }
            return;
        }
        if (_selectedKind != SelectionKind::World) {
            return;
        }

        const WorldEdit *edit = _world.Find(_selected);
        if (edit == nullptr) {
            return;
        }

        selection.kind        = SelectionKind::World;
        selection.id          = edit->id;
        selection.label       = edit->label;
        selection.detail      = edit->detail.empty() ? WorldPickKindToString(edit->kind) : edit->detail;
        selection.mesh        = WorldObjects::MeshPath(*edit);
        selection.className   = edit->className;
        selection.position    = edit->position;
        selection.rotation    = edit->rotation;
        selection.scale       = edit->scale;
        selection.worldBounds = edit->worldBounds;
        selection.hasBounds   = edit->hasBounds;
        selection.hidden      = edit->hidden;
        selection.moved       = edit->moved;
        selection.replacement = edit->replacement;
        selection.loaded      = edit->loaded;
        selection.layerOff    = edit->layerOff;
        selection.onLayer     = edit->onSwitchedLayer;
        selection.layerId     = edit->layerId;
    }

    void MapEditor::PublishSnapshot() {
        EditorSnapshot snapshot;
        snapshot.collisionContext    = _collisionContext.load();
        snapshot.collisionResults    = _collisionResults;
        snapshot.collisionError      = _collisionError;
        snapshot.cameraHeld          = _camera.IsActive();
        snapshot.mapTravel           = _mapTravel.State();
        snapshot.camera              = _camera.GetPose();
        snapshot.cameraSpeed         = _camera.GetSpeed();
        snapshot.cameraRequestedFov  = _camera.GetRequestedFieldOfView();
        snapshot.status              = _status;
        snapshot.patrolPreviewActive = _patrolPreview.Active();
        snapshot.patrolProgress      = _patrolPreview.Progress();
        snapshot.patrolPreviewStatus = _patrolPreview.Status();
        snapshot.patrolPath          = _patrolPreview.Path();
        snapshot.completedCommand    = _completedCommand;
        snapshot.unresolvedDocument  = _unresolvedPublished;
        for (const SelectionRef &ref : SelectionRefs()) {
            if (ref.kind == SelectionKind::Placed) {
                if (const PlacedObject *object = _scene.Find(ref.id)) {
                    snapshot.selectedDocument.objects.push_back(DocumentObject {object->model, object->position, object->rotation, object->scale, object->physics, object->group, "default", object->kind, object->settings});
                }
            }
            else if (ref.kind == SelectionKind::Light) {
                if (const PlacedLight *light = _lights.Find(ref.id)) {
                    snapshot.selectedDocument.lights.push_back(DocumentLight {light->position, light->tuning, light->group});
                }
            }
            else if (ref.kind == SelectionKind::Effect) {
                if (const PlacedEffect *effect = _effects.Find(ref.id)) {
                    snapshot.selectedDocument.effects.push_back(DocumentEffect {effect->name, effect->position, effect->rotation, effect->tuning, effect->group});
                }
            }
            else if (ref.kind == SelectionKind::World) {
                Shared::Game::LevelEditDefinition definition;
                if (const WorldEdit *edit = _world.Find(ref.id); edit != nullptr && WorldObjects::ToDefinition(*edit, definition)) {
                    snapshot.selectedDocument.world.push_back(std::move(definition));
                }
            }
        }

        BuildSelection(snapshot.selection);
        if (_brushAimWanted.load(std::memory_order_relaxed) && _camera.IsActive()) {
            ResolveAim(_brushNdcX.load(std::memory_order_relaxed), _brushNdcY.load(std::memory_order_relaxed), 0.0F, snapshot.brushPoint, snapshot.brushNormal, snapshot.brushHit);
        }
        snapshot.hover        = _hover;
        snapshot.groundResult = _groundResult;
        if (snapshot.cameraHeld && _patrolTabVisible.load(std::memory_order_relaxed)) {
            SDK::Vec3 normal;
            ResolveAim(0, 0, 5, snapshot.cameraGround.point, normal, snapshot.cameraGround.valid);
            if (!snapshot.cameraGround.valid) {
                auto *physics               = SDK::CPhysicalWorld::GetInstance();
                snapshot.cameraGround.valid = physics && physics->ResolveGround(snapshot.cameraGround.point, kGroundProbeUp, kGroundProbeDown, snapshot.cameraGround.point);
            }
        }

        if (snapshot.selection.kind != SelectionKind::None) {
            snapshot.selectedRefs.Insert(SelectionRef {snapshot.selection.kind, snapshot.selection.id});
        }
        for (const SelectionRef &ref : _extra) {
            if (const std::optional<RefPose> pose = DescribeRef(ref)) {
                snapshot.extraSelection.push_back(ref);
                snapshot.extraBounds.push_back(pose->hasBounds ? pose->bounds : SDK::AABB {pose->position, pose->position});
                snapshot.selectedRefs.Insert(ref);
            }
        }

        snapshot.previewModel    = _previewGhost != 0 || _previewEmitter != 0 ? _previewModel : AssetLibrary::kNone;
        snapshot.previewIsEffect = _previewIsEffect;
        snapshot.previewSized    = _previewSized;
        snapshot.previewSize     = _previewSize;

        snapshot.armedModel    = _armedModel;
        snapshot.armedIsEffect = _armedIsEffect;
        snapshot.armedOnGround = _armedOnGround && _armedShown;
        snapshot.armedSized    = _armedSized;
        snapshot.armedSize     = _armedSize;
        snapshot.armedYaw      = _armedYaw;
        snapshot.armedScale    = _armedScale;

        snapshot.levelRows      = _levelRows;
        snapshot.levelTruncated = _levelTruncated;
        if (_layersPublished->Revision() != _layers.Revision()) {
            _layersPublished = std::make_shared<const EditorLayers>(_layers);
        }
        snapshot.layers = _layersPublished;
        for (const auto &world : _world.Edits()) {
            const auto layer = _layers.LayerOf(LayerKey({SelectionKind::World, world.id}));
            DocumentWorldLayer member;
            if (layer != "default" && WorldObjects::ToDefinition(world, member.object, true)) {
                member.layer = layer;
                snapshot.worldLayers.push_back(std::move(member));
            }
        }

        const KCDC::Features::Camera::FlyKeyBindings &keys = KCDC::Features::Camera::FlyKeyBindings::Get();
        snapshot.flyKeys = keys.Describe(KCDC::Features::Camera::FlyKey::Forward) + " " + keys.Describe(KCDC::Features::Camera::FlyKey::Left) + " " + keys.Describe(KCDC::Features::Camera::FlyKey::Back) + " " + keys.Describe(KCDC::Features::Camera::FlyKey::Right) + ", "
                           + keys.Describe(KCDC::Features::Camera::FlyKey::Up) + " / " + keys.Describe(KCDC::Features::Camera::FlyKey::Down) + ", " + keys.Describe(KCDC::Features::Camera::FlyKey::Boost) + " / Alt";

        snapshot.undoLabel             = _history.NextUndoLabel();
        snapshot.redoLabel             = _history.NextRedoLabel();
        snapshot.authoredRestore       = _history.AuthoredRestore();
        snapshot.authoredRestoreSerial = _history.AuthoredRestoreSerial();

        if (SDK::IEntity *player = KCDC::Common::LocalPlayer()) {
            snapshot.hasPlayer      = true;
            snapshot.playerPosition = player->GetPos();
        }
        if (const auto *application = Core::gApplication.get()) {
            snapshot.level   = application->GetCurrentLevelName();
            snapshot.offline = application->IsOfflineBuilderReady();
        }

        const std::vector<PlacedObject> &objects = _scene.Objects();
        snapshot.rows.reserve(objects.size());
        for (const PlacedObject &object : objects) {
            SceneRow row;
            row.id          = object.id;
            row.entityId    = object.entityId;
            row.kind        = object.kind;
            row.settings    = object.settings;
            row.name        = object.name;
            row.model       = object.model;
            row.group       = object.group;
            row.position    = object.position;
            row.rotation    = object.rotation;
            row.scale       = object.scale;
            row.physics     = object.physics;
            row.worldBounds = object.worldBounds;
            row.hasBounds   = object.hasBounds;
            snapshot.rows.push_back(std::move(row));
        }

        for (const auto &entry : _lights.Lights()) {
            snapshot.lightRows.push_back({entry.id, entry.position, entry.tuning, entry.handle != 0, entry.group});
        }
        snapshot.effectRows.reserve(_effects.Count());
        for (const PlacedEffect &effect : _effects.Effects()) {
            snapshot.effectRows.push_back(EffectRow {effect.id, effect.label, effect.name, effect.position, effect.rotation, effect.tuning, effect.handle != 0, effect.group});
        }
        if (auto *application = Core::gApplication.get()) {
            snapshot.liveEmitters = application->GetVfxService().LiveCount();
        }

        // Adopted but unchanged objects, such as a region selection's, are not edits.
        for (const WorldEdit &edit : _world.Edits()) {
            if (!edit.IsRemoved() && !edit.moved) {
                continue;
            }
            WorldRow row;
            row.id       = edit.id;
            row.label    = edit.label;
            row.kind     = edit.detail.empty() ? WorldPickKindToString(edit.kind) : edit.detail;
            row.position = edit.position;
            row.hidden   = edit.hidden;
            row.moved    = edit.moved;
            row.swapped  = !edit.replacement.empty();
            row.portable = edit.kind == WorldPickKind::RenderNode || edit.guid != 0;
            row.loaded   = edit.loaded;
            row.layerOff = edit.layerOff;
            snapshot.worldRows.push_back(std::move(row));
        }
        _world.ToDocument(snapshot.worldDocument);

        auto published = std::make_shared<const EditorSnapshot>(std::move(snapshot));
        std::scoped_lock lock(_snapshotMutex);
        _snapshot = std::move(published);
    }

    // --- Render thread --------------------------------------------------------

    std::shared_ptr<const AuthoredHistoryState> MapEditor::CaptureAuthoredHistory() const {
        auto state                      = std::make_shared<AuthoredHistoryState>();
        state->document.areas           = _areas.Authored();
        state->document.areaGroups      = _areas.Groups();
        state->document.patrolRoutes    = _patrols.Authored();
        state->document.views           = _views;
        state->document.propFavorites   = _library.GetFavoriteFolders();
        state->document.effectFavorites = _effectLibrary.GetFavoriteFolders();
        state->selectedAreas            = _areas.SelectedIds();
        state->selectedPatrols          = _patrols.SelectedIds();
        return state;
    }

    std::array<std::uint64_t, 7> MapEditor::AuthoredHistoryRevision() const {
        return {_areas.ContentRevision(), _areas.SelectionRevision(), _patrols.ContentRevision(), _patrols.SelectionRevision(), _viewsRevision, _library.FavoritesRevision(), _effectLibrary.FavoritesRevision()};
    }

    void MapEditor::ResetAuthoredHistoryBaseline() {
        _authoredHistoryBaseline      = CaptureAuthoredHistory();
        _authoredHistoryContent       = ProjectToJson(_authoredHistoryBaseline->document);
        _authoredHistoryBaselineValid = _authoredHistoryBaseline->IsValid();
        _authoredHistoryRevision      = AuthoredHistoryRevision();
    }

    void MapEditor::SynchronizeAuthoredHistory(const EditorSnapshot &snapshot) {
        if (_authoredHistoryPendingRestore != 0) {
            if (snapshot.completedCommand < _authoredHistoryPendingRestore) {
                return;
            }
            _authoredHistoryPendingRestore = 0;
        }
        if (snapshot.authoredRestoreSerial != _authoredHistoryRestoreSeen) {
            if (const auto &state = snapshot.authoredRestore) {
                std::string error;
                if (!state->IsValid() || !_areas.ReplaceState(state->document.areas, state->document.areaGroups, state->selectedAreas, error) || !_patrols.ReplaceState(state->document.patrolRoutes, state->selectedPatrols, error)) {
                    _notice = "Could not restore authored history: " + error;
                    return;
                }
                _library.RestoreFavoriteFolders(state->document.propFavorites);
                _effectLibrary.RestoreFavoriteFolders(state->document.effectFavorites);
                _views = state->document.views;
                ++_viewsRevision;
            }
            _authoredHistoryRestoreSeen = snapshot.authoredRestoreSerial;
            // Applying history is not another edit. A null restore denotes a
            // cleared timeline, such as opening another project.
            ResetAuthoredHistoryBaseline();
        }
        else if (!_authoredHistoryBaseline) {
            ResetAuthoredHistoryBaseline();
        }
    }

    bool MapEditor::RecordAuthoredChanges(const std::string &label, bool finishGesture) {
        if (_authoredHistoryPendingRestore != 0 || _groundSnap.Pending()) {
            return false;
        }
        if (!finishGesture && ImGui::GetCurrentContext() && (ImGuizmo::IsUsingAny() || ImGui::IsAnyItemActive())) {
            return false;
        }
        const auto revision = AuthoredHistoryRevision();
        if (_authoredHistoryBaseline && revision == _authoredHistoryRevision) {
            return false;
        }
        // Remember rejected intermediate content too: only another mutation can
        // make it valid, so idle frames need neither copies nor validation.
        _authoredHistoryRevision = revision;
        const auto state         = CaptureAuthoredHistory();
        // Inspector drags may pass through a temporarily invalid polygon or
        // box. Keep the last coherent before-state until the gesture ends.
        if (!state->IsValid()) {
            return false;
        }
        auto content = ProjectToJson(state->document);
        if (!_authoredHistoryBaseline || !_authoredHistoryBaselineValid || content == _authoredHistoryContent) {
            _authoredHistoryBaseline      = state;
            _authoredHistoryContent       = std::move(content);
            _authoredHistoryBaselineValid = true;
            return false;
        }
        Command command;
        command.kind             = CommandKind::RecordAuthored;
        command.authoredBefore   = _authoredHistoryBaseline;
        command.authoredAfter    = state;
        command.group            = label.empty() ? "Edit authored content" : label;
        _authoredHistoryBaseline = state;
        _authoredHistoryContent  = std::move(content);
        Queue(std::move(command));
        return true;
    }

    bool MapEditor::RestoreAreaCollision() {
        _collisionError.clear();
        const auto &restored = _history.AuthoredRestore();
        return !restored || _areaCollision.ReplacePreview(restored->document.areas, _collisionError);
    }

    void MapEditor::PublishAreaCollision(const EditorSnapshot &snapshot) {
        if (_mcpDispatchGeneration == 0 && ImGui::GetCurrentContext() && (ImGuizmo::IsUsingAny() || ImGui::IsAnyItemActive())) {
            return;
        }
        if (!_mcp.Available() || _authoredHistoryPendingRestore != 0 || (_areaCollisionRevision == _areas.ContentRevision() && _areaCollisionContext == snapshot.collisionContext)) {
            return;
        }
        _areaCollisionRevision = _areas.ContentRevision();
        _areaCollisionContext  = snapshot.collisionContext;
        auto document          = std::make_shared<MapDocument>();
        document->areas        = _areas.Authored();
        Command command;
        command.kind     = CommandKind::SyncAreaCollision;
        command.id       = snapshot.collisionContext;
        command.document = std::move(document);
        Queue(std::move(command));
    }

    void MapEditor::Render() {
        const std::shared_ptr<const EditorSnapshot> published = ReadSnapshot();
        const EditorSnapshot &snapshot                        = *published;
        SynchronizeAuthoredHistory(snapshot);
        // The sky belongs to the offline session rather than to the panel:
        // F7 hands the controls back to walk the level in the conditions it
        // was built for, and leaving gives them up.
        _environment.Synchronize(snapshot.offline);
        UpdateProjectBaseline(snapshot);
        if (IsOpen()) {
            UpdateProject(snapshot);
        }
        RecordAuthoredChanges();
        _mcp.Render(*this, snapshot);
        PublishAreaCollision(snapshot);
        if (snapshot.mapTravel.value("active", false)) {
            _pointerOverUi.store(false);
            _keyboardCaptured.store(false);
            _groundSnap.Reset();
            FinishReleasedGizmoDrag(true);
            if (std::exchange(_overlayPublished, false)) {
                Gizmos::GizmosService::Get().SetEditorOverlay({});
            }
            ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_Always);
            ImGui::SetNextWindowBgAlpha(0.85F);
            if (ImGui::Begin("World Builder map travel", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings)) {
                const auto status = snapshot.mapTravel.value("status", "");
                ImGui::TextUnformatted("World Builder map travel");
                ImGui::TextUnformatted(status == "selecting" || status == "opening" ? "Click a destination. Drag to pan. Escape cancels." : "Loading destination. Escape cancels before placement.");
                const auto message = snapshot.mapTravel.value("message", "");
                if (!message.empty()) {
                    ImGui::TextUnformatted(message.c_str());
                }
            }
            ImGui::End();
            return;
        }
        // A moved level object can stream out before Manipulate sees mouse-up.
        // Release its shared drag state even when no gizmo can be drawn.
        FinishReleasedGizmoDrag(!IsOpen());
        if (!IsOpen()) {
            _patrolTabVisible.store(false, std::memory_order_relaxed);
            _groundSnap.Reset();
            if (std::exchange(_overlayPublished, false)) {
                Gizmos::GizmosService::Get().SetEditorOverlay({});
            }
            return;
        }

        ImGuiIO &io = ImGui::GetIO();

        SyncEditDraft(snapshot);
        SyncUserSettings();
        if (_authoredHistoryPendingRestore != 0) {
            return;
        }

        if (!_placementEverSent || !(_placementDraft == _placementSent)) {
            Command command;
            command.kind      = CommandKind::Placement;
            command.placement = _placementDraft;
            Queue(std::move(command));
            _placementSent     = _placementDraft;
            _placementEverSent = true;
        }

        if (snapshot.armedModel != AssetLibrary::kNone && !snapshot.armedIsEffect && snapshot.armedSized) {
            _library.NoteSize(snapshot.armedModel, snapshot.armedSize);
        }
        if (snapshot.previewModel != AssetLibrary::kNone && !snapshot.previewIsEffect && snapshot.previewSized) {
            _library.NoteSize(snapshot.previewModel, snapshot.previewSize);
        }

        // Set by whichever library tab is drawn.
        _frameHover       = AssetLibrary::kNone;
        _frameHoverEffect = false;

        ImGuizmo::BeginFrame();
        ImGuizmo::SetOrthographic(false);
        _areas.BeginFrame();
        _patrols.BeginFrame();
        // The background list: behind every window.
        ImGuizmo::SetDrawlist(ImGui::GetBackgroundDrawList());

        RenderToolbar(snapshot);
        if (_authoredHistoryPendingRestore != 0) {
            return;
        }
        RenderSidebar(snapshot);
        if (_authoredHistoryPendingRestore != 0) {
            return;
        }
        // World-space lines go to the native renderer, which draws them on
        // the game thread; markers, labels and the gizmo stay on ImGui.
        std::vector<Gizmos::Drawing> lines;
        _patrolTabVisible.store(_patrols.IsTabVisible(), std::memory_order_relaxed);
        if (_viewport.grid && snapshot.cameraHeld) {
            lines.push_back(_viewport.Grid(glm::vec3(snapshot.camera.position.x, snapshot.camera.position.y, snapshot.camera.position.z)));
        }
        if (snapshot.layers->Find("default")->visible) {
            _areas.RenderOverlay(snapshot, lines);
            _patrols.RenderOverlay(snapshot, lines);
        }
        RenderEffectMarkers(snapshot);
        RenderLightMarkers(snapshot);
        RenderSelectionOutline(snapshot, lines);
        Gizmos::GizmosService::Get().SetEditorOverlay(std::move(lines));
        _overlayPublished = true;
        _groundSnap.BeginFrame();
        RenderGizmo(snapshot);
        _groundSnap.EndFrame();
        RenderHoverReadout(snapshot);
        RenderBrushReadout(snapshot);
        RenderViewportInput(snapshot);
        RecordAuthoredChanges();
        PublishAreaCollision(snapshot);
        Command patrolRequest;
        if (_patrols.TakePreviewRequest(patrolRequest.patrolRoute, patrolRequest.previewRole)) {
            patrolRequest.kind = CommandKind::PatrolPreview;
            Queue(std::move(patrolRequest));
        }

        // Build the turntable once the mouse rests; drop it at once on leaving.
        const auto now = std::chrono::steady_clock::now();
        if (_frameHover != _previewHover || _frameHoverEffect != _previewHoverEffect) {
            _previewHover       = _frameHover;
            _previewHoverEffect = _frameHoverEffect;
            _previewHoverSince  = now;
        }
        const bool previewChanged = _previewHover != _previewSent || _previewHoverEffect != _previewSentEffect;
        if (!_mcpPreviewPinned && previewChanged && (_previewHover == AssetLibrary::kNone || now - _previewHoverSince >= kPreviewSettle)) {
            Command command;
            command.kind   = CommandKind::Preview;
            command.enable = _previewHover != AssetLibrary::kNone;
            command.effect = _previewHoverEffect;
            command.id     = command.enable ? static_cast<std::uint64_t>(_previewHover) : 0;
            Queue(std::move(command));
            _previewSent       = _previewHover;
            _previewSentEffect = _previewHoverEffect;
        }

        int picked = AssetLibrary::kNone;
        if (_library.TakePicked(picked)) {
            Command command;
            command.kind = CommandKind::Arm;
            command.id   = static_cast<std::uint64_t>(picked);
            Queue(std::move(command));
        }
        if (_effectLibrary.TakePicked(picked)) {
            Command command;
            command.kind   = CommandKind::Arm;
            command.id     = static_cast<std::uint64_t>(picked);
            command.effect = true;
            Queue(std::move(command));
        }

        // Whether a right-click may start flying; a gizmo drag is not the world.
        _pointerOverUi.store(io.WantCaptureMouse || (_gizmoDrawn && (ImGuizmo::IsOver() || ImGuizmo::IsUsing())), std::memory_order_release);
        _keyboardCaptured.store(io.WantTextInput, std::memory_order_release);

        // Hidden while flying: it is warped to the centre every frame.
        if (_looking.load(std::memory_order_acquire)) {
            io.MouseDrawCursor = false;
            ImGui::SetMouseCursor(ImGuiMouseCursor_None);
        }
        else {
            io.MouseDrawCursor = true;
        }
    }

    void MapEditor::RenderToolbar(const EditorSnapshot &snapshot) {
        const ImGuiViewport *viewport = ImGui::GetMainViewport();

        constexpr float kPadding = 7.0F;
        const float barHeight    = ImGui::GetFrameHeight() * 2.0F + 4.0F + kPadding * 2.0F;
        const float barWidth     = std::max(viewport->Size.x - _sidebarWidth, 200.0F);

        ImGui::SetNextWindowPos(viewport->Pos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(barWidth, barHeight), ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0F);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0F, kPadding));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0F);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2.0F, 4.0F));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.09F, 0.09F, 0.11F, 0.94F));

        constexpr ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNavFocus;
        if (ImGui::Begin("##kcdc_editor_toolbar", nullptr, flags)) {
            if (snapshot.offline) {
                ImGui::BeginDisabled(!IsSettled(snapshot));
                if (ImGui::Button("Map")) {
                    Command command;
                    command.kind = CommandKind::OpenNativeMap;
                    Queue(std::move(command));
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Open the native map and click to travel within this region");
                }
                ImGui::EndDisabled();
                ToolbarSeparator();
            }
            constexpr float kToolWidth = 62.0F;

            if (ToolbarToggle("Move", _operation == 0, ("Move  (" + _keymap.Label(EditorAction::Move) + ")").c_str(), kToolWidth)) {
                _operation = 0;
            }
            ImGui::SameLine();
            if (ToolbarToggle("Rotate", _operation == 1, ("Rotate  (" + _keymap.Label(EditorAction::Rotate) + ")").c_str(), kToolWidth)) {
                _operation = 1;
            }
            ImGui::SameLine();
            if (ToolbarToggle("Scale", _operation == 2, ("Scale  (" + _keymap.Label(EditorAction::Scale) + ")").c_str(), kToolWidth)) {
                _operation = 2;
            }

            ToolbarSeparator();

            // Scale has no world frame.
            if (_operation == 2) {
                ImGui::BeginDisabled();
                ToolbarToggle("Local axes", true, "Scaling is always along the object's own axes", 86.0F);
                ImGui::EndDisabled();
            }
            else if (ToolbarToggle(_viewport.localSpace ? "Local axes" : "World axes", _viewport.localSpace, ("Switch local/world coordinates  (" + _keymap.Label(EditorAction::ToggleAxes) + ")").c_str(), 86.0F)) {
                _viewport.localSpace = !_viewport.localSpace;
            }

            ToolbarSeparator();

            bool *snapEnabled      = _operation == 1 ? &_viewport.snapRotate : _operation == 2 ? &_viewport.snapScale : &_viewport.snapTranslate;
            float *snapStep        = _operation == 1 ? &_viewport.snapRotateStep : _operation == 2 ? &_viewport.snapScaleStep : &_viewport.snapTranslateStep;
            const char *snapFormat = _operation == 1 ? "%.0f deg" : _operation == 2 ? "x%.2f" : "%.2f m";

            if (ToolbarToggle("Snap", *snapEnabled, ("Step this tool in fixed increments  (" + _keymap.Label(EditorAction::ToggleSnap) + ")").c_str(), 52.0F)) {
                *snapEnabled = !*snapEnabled;
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(!*snapEnabled);
            ImGui::SetNextItemWidth(72.0F);
            ImGui::DragFloat("##kcdc_editor_snap_step", snapStep, _operation == 1 ? 1.0F : 0.01F, _operation == 1 ? 1.0F : 0.01F, _operation == 1 ? 90.0F : 10.0F, snapFormat, ImGuiSliderFlags_AlwaysClamp);
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
                ImGui::SetTooltip("How far one step of this tool goes.\nEach tool keeps its own.");
            }

            ToolbarSeparator();
            if (ToolbarToggle("Grid", _viewport.grid, "Show a world-space grid following the snapped camera position", 48.0F)) {
                _viewport.grid = !_viewport.grid;
            }
            ImGui::SameLine();
            if (ImGui::Button("Grid settings")) {
                ImGui::OpenPopup("Grid settings");
            }
            if (ImGui::BeginPopup("Grid settings")) {
                ImGui::Checkbox("Match move snap", &_viewport.gridMatchSnap);
                ImGui::BeginDisabled(_viewport.gridMatchSnap);
                ImGui::SliderFloat("Spacing (m)", &_viewport.gridStep, 0.01F, 50.0F, "%.2f", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
                ImGui::EndDisabled();
                ImGui::TextDisabled("Current spacing: %.2f m", _viewport.GridStep());
                ImGui::SliderFloat("Line width (px)", &_viewport.gridLineWidth, 1.0F, 8.0F, "%.1f", ImGuiSliderFlags_AlwaysClamp);
                ImGui::SliderFloat("Height below/above camera", &_viewport.gridHeightOffset, -100.0F, 100.0F, "%.2f m", ImGuiSliderFlags_AlwaysClamp);
                ImGui::TextWrapped("The horizontal grid follows the camera in whole cells. It does not change placement or snapping.");
                ImGui::EndPopup();
            }

            // The previous item already advanced to the next row.
            ImGui::BeginDisabled(snapshot.undoLabel.empty());
            if (ToolbarToggle("Undo", false, snapshot.undoLabel.empty() ? "Nothing to undo" : ("Undo: " + snapshot.undoLabel + "  (" + _keymap.Label(EditorAction::Undo) + ")").c_str(), 48.0F)) {
                Command command;
                command.kind = CommandKind::Undo;
                Queue(std::move(command));
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(snapshot.redoLabel.empty());
            if (ToolbarToggle("Redo", false, snapshot.redoLabel.empty() ? "Nothing to redo" : ("Redo: " + snapshot.redoLabel + "  (" + _keymap.Label(EditorAction::Redo) + ")").c_str(), 48.0F)) {
                Command command;
                command.kind = CommandKind::Redo;
                Queue(std::move(command));
            }
            ImGui::EndDisabled();

            ToolbarSeparator();

            // Z-up names; ImGuizmo's view cube assumes Y-up.
            ImGui::TextDisabled("View");
            ImGui::SameLine(0.0F, 6.0F);
            if (ToolbarToggle("Top", false, "Look straight down", 44.0F)) {
                QueueLook(snapshot, SDK::Vec3 {0.0F, 0.02F, -1.0F});
            }
            ImGui::SameLine();
            if (ToolbarToggle("North", false, "Look along +Y", 52.0F)) {
                QueueLook(snapshot, SDK::Vec3 {0.0F, 1.0F, 0.0F});
            }
            ImGui::SameLine();
            if (ToolbarToggle("East", false, "Look along +X", 46.0F)) {
                QueueLook(snapshot, SDK::Vec3 {1.0F, 0.0F, 0.0F});
            }

            // A click always picks; these decide what a drag does.
            ToolbarSeparator();
            ImGui::TextDisabled("Drag");
            ImGui::SameLine(0.0F, 6.0F);
            const std::string shapeKey = "\nKey: " + _keymap.Label(EditorAction::ToggleLasso) + " switches between box and lasso.";
            if (ToolbarToggle("Box", !_brushSelect && !_dragLasso, ("Dragging selects what is inside a rectangle; Ctrl adds, Shift takes out." + shapeKey).c_str(), 40.0F)) {
                _brushSelect = false;
                _dragLasso   = false;
            }
            ImGui::SameLine();
            if (ToolbarToggle("Lasso", !_brushSelect && _dragLasso, ("Dragging selects what is inside the outline you draw; Ctrl adds, Shift takes out." + shapeKey).c_str(), 48.0F)) {
                _brushSelect = false;
                _dragLasso   = true;
            }
            ImGui::SameLine();
            const std::string brushTip = "Dragging paints: what the brush passes over is added to the selection, Shift takes out.\nKey: " + _keymap.Label(EditorAction::ToggleBrush) + " turns it on or off.";
            if (ToolbarToggle("Brush", _brushSelect, brushTip.c_str(), 48.0F)) {
                _brushSelect = !_brushSelect;
            }
            ImGui::SameLine();
            if (ImGui::Button("Selection settings")) {
                ImGui::OpenPopup("Selection settings");
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
                ImGui::SetTooltip("Brush radius, reach, Visible only, the filter every tool uses, and Select similar");
            }
            // The filter's fields want a steady width; the popup sizes itself to them otherwise.
            ImGui::SetNextWindowSizeConstraints(ImVec2(440.0F, 0.0F), ImVec2(440.0F, viewport->Size.y * 0.8F));
            if (ImGui::BeginPopup("Selection settings")) {
                RenderSelectionSettings(snapshot);
                ImGui::EndPopup();
            }

            // The armed mesh wins over the selection: it is what a click does.
            char label[160];
            ImVec4 colour = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
            if (snapshot.armedModel != AssetLibrary::kNone) {
                const std::string_view name = ArmedName(snapshot);
                std::snprintf(label, sizeof(label), "placing %.*s", static_cast<int>(name.size()), name.data());
                colour = kAccentHover;
            }
            else if (snapshot.selection.kind != SelectionKind::None) {
                std::snprintf(label, sizeof(label), "%s", snapshot.selection.label.c_str());
                colour = snapshot.selection.kind == SelectionKind::World ? kWorldColour : kPlacedColour;
            }
            else {
                std::snprintf(label, sizeof(label), "nothing selected");
            }

            const float helpWidth = 26.0F;
            const float tail      = ImGui::CalcTextSize(label).x + helpWidth + 24.0F;
            if (tail < barWidth - (ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x + 12.0F)) {
                ImGui::SameLine(barWidth - tail);
                ImGui::TextColored(colour, "%s", label);
                ImGui::SameLine(0.0F, 12.0F);
                if (ToolbarToggle("?", _showHelp, "Keys and mouse", helpWidth)) {
                    _showHelp = !_showHelp;
                }
            }
        }
        ImGui::End();

        ImGui::PopStyleColor();
        ImGui::PopStyleVar(4);

        if (_showHelp) {
            RenderHelp(snapshot, barHeight);
        }
    }

    void MapEditor::RenderHelp(const EditorSnapshot &snapshot, float toolbarHeight) {
        const ImGuiViewport *viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + 16.0F, viewport->Pos.y + toolbarHeight + 8.0F), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.88F);

        constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNavFocus;
        if (ImGui::Begin("Keys###kcdc_editor_help", &_showHelp, flags)) {
            if (ImGui::BeginTable("##kcdc_editor_keys", 2, ImGuiTableFlags_SizingFixedFit)) {
                const auto row = [](const char *keys, const char *what) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextColored(ImVec4(0.85F, 0.85F, 0.9F, 1.0F), "%s", keys);
                    ImGui::TableNextColumn();
                    ImGui::TextDisabled("%s", what);
                };
                const auto heading = [](const char *text) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextColored(kAccentHover, "%s", text);
                };

                heading("Camera");
                const std::string fly = "hold to fly: " + snapshot.flyKeys;
                row("Right mouse", fly.c_str());
                row("Wheel", "flying: speed. Otherwise: close in on the cursor");
                row("Middle drag", "pan");
                row("Alt + left drag", "orbit the selection, or what you grabbed");
                row(_keymap.Label(EditorAction::Frame).c_str(), "frame the selection");
                heading("Placing");
                row("Library hover", "the real mesh turns in front of the camera");
                row("Library click", "pick a mesh up; a preview follows the cursor");
                row("Effects tab", "the same for particle effects, previewed live");
                row("Left click", "put one down (keeps placing)");
                row(("Shift + wheel / " + _keymap.Label(EditorAction::TurnArmed)).c_str(), "turn the preview");
                row("Ctrl + wheel", "resize the preview");
                row("Esc / Right click", "stop placing (RMB drag keeps placement; Esc again clears selection)");
                heading("Editing");
                row("Left click", "select anything -- a placed prop or the level's own");
                row("Ctrl + click", "add to the selection, or take out of it");
                const std::string tools = _keymap.Label(EditorAction::Move) + " " + _keymap.Label(EditorAction::Rotate) + " " + _keymap.Label(EditorAction::Scale);
                row(tools.c_str(), "move, rotate, scale");
                row((_keymap.Label(EditorAction::ToggleAxes) + " / " + _keymap.Label(EditorAction::ToggleSnap)).c_str(), "local or world, snapping");
                row(_keymap.Label(EditorAction::Duplicate).c_str(), "duplicate the selection; a level object becomes a prop");
                row(_keymap.Label(EditorAction::Delete).c_str(), "delete props, take the level's own out");
                row(_keymap.Label(EditorAction::DropToGround).c_str(), "drop to the ground");
                row((_keymap.Label(EditorAction::Undo) + " / " + _keymap.Label(EditorAction::Redo)).c_str(), "undo, redo -- and select what changed");
                row("Keys tab", "change any of these keys");
                heading("Selecting many");
                row("Left drag", "select what is inside a box or lasso on screen");
                row(_keymap.Label(EditorAction::ToggleLasso).c_str(), "switch the drag between box and lasso");
                row(_keymap.Label(EditorAction::ToggleBrush).c_str(), "brush on or off: a drag paints, adding what it passes over");
                row("Ctrl / Shift + drag", "add to the selection / take out of it");
                row("Selection settings", "brush size, reach and the filter every tool uses");
                ImGui::EndTable();
            }
        }
        ImGui::End();
    }

    void MapEditor::RenderSidebar(const EditorSnapshot &snapshot) {
        const ImGuiViewport *viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + viewport->Size.x - kSidebarWidth, viewport->Pos.y), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(kSidebarWidth, viewport->Size.y), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSizeConstraints(ImVec2(std::min(320.0F, viewport->Size.x), std::min(300.0F, viewport->Size.y)), viewport->Size);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0F);

        bool open                               = true;
        constexpr ImGuiWindowFlags sidebarFlags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
        if (ImGui::Begin("World builder###kcdc_map_editor", &open, sidebarFlags)) {
            _sidebarWidth = ImGui::GetWindowWidth();
            if (!snapshot.cameraHeld) {
                ImGui::TextColored(kWarnColour, "The editor has no camera yet -- load a level.");
            }
            ImGui::Text("%s%s", _mapName, _projectDirty ? " *" : "");
            ImGui::TextDisabled("F7 returns to the player; press it again to edit.");
            if (snapshot.offline && ImGui::Button("Return to menu")) {
                _pendingExit = true;
            }
            ImGui::Text("%zu placed, %zu level edit%s", snapshot.rows.size(), snapshot.worldRows.size(), snapshot.worldRows.size() == 1 ? "" : "s");
            if (!snapshot.level.empty()) {
                ImGui::SameLine();
                ImGui::TextDisabled("in %s", snapshot.level.c_str());
            }
            ImGui::Separator();

            // The status line is pinned below the tabs.
            const float statusHeight = ImGui::GetTextLineHeightWithSpacing() * (_notice.empty() ? 2.0F : 4.0F) + 12.0F;
            if (ImGui::BeginChild("##kcdc_editor_body", ImVec2(0.0F, -statusHeight))) {
                constexpr const char *tabs[] {"Project", "Blueprints", "Library", "Effects", "Scene", "Level", "Object", "Areas", "Camera", "Environment", "Patrols", "MCP", "Layers", "Lights", "Keys"};
                auto *storage         = ImGui::GetStateStorage();
                const ImGuiID tabKey  = ImGui::GetID("##kcdc_editor_tabs");
                int activeTab         = storage->GetInt(tabKey, 0);
                const auto &style     = ImGui::GetStyle();
                const float available = ImGui::GetContentRegionAvail().x;
                float rowWidth        = 0.0F;
                for (int index = 0; index < static_cast<int>(std::size(tabs)); ++index) {
                    const float width = ImGui::CalcTextSize(tabs[index]).x + style.FramePadding.x * 2.0F;
                    if (rowWidth > 0.0F && rowWidth + style.ItemSpacing.x + width <= available) {
                        ImGui::SameLine();
                        rowWidth += style.ItemSpacing.x;
                    }
                    else {
                        rowWidth = 0.0F;
                    }
                    ImGui::PushStyleColor(ImGuiCol_Button, style.Colors[activeTab == index ? ImGuiCol_TabSelected : ImGuiCol_Tab]);
                    if (ImGui::Button(tabs[index])) {
                        activeTab = index;
                        storage->SetInt(tabKey, activeTab);
                        ImGui::SetScrollY(0.0F);
                    }
                    ImGui::PopStyleColor();
                    rowWidth += width;
                }
                ImGui::Separator();
                // A key capture left waiting on another tab would take the next key pressed anywhere.
                if (activeTab != 14) {
                    _capturingAction = -1;
                }
                ImGui::PushID(activeTab);
                switch (activeTab) {
                case 0: RenderFiles(snapshot); break;
                case 1: RenderBlueprints(snapshot); break;
                case 2: RenderLibraryTab(snapshot); break;
                case 3: RenderEffectsTab(snapshot); break;
                case 13: RenderLightsTab(snapshot); break;
                case 14: RenderKeysTab(); break;
                case 4: RenderSceneTab(snapshot); break;
                case 5: RenderLevelTab(snapshot); break;
                case 6: RenderObjectTab(snapshot); break;
                case 7:
                    ImGui::BeginDisabled(!snapshot.layers->Editable("area:default"));
                    _areas.RenderTab(snapshot, _notice, _viewport);
                    ImGui::EndDisabled();
                    if (const std::vector<std::string> selectedAreas = _areas.SelectedIds(); selectedAreas.size() == 1) {
                        if (const Shared::Game::AreaDefinition *area = _areas.Find(selectedAreas.front())) {
                            ImGui::Separator();
                            if (ImGui::Button("Select objects inside this area", ImVec2(-1.0F, 0.0F))) {
                                SelectionQuery query;
                                query.region.kind = SelectionRegionKind::Area;
                                query.region.area = area->shape;
                                QueueSelection(std::move(query), ModifierSelectionMode());
                            }
                            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
                                ImGui::SetTooltip("Selects every object inside it that the selection filter lets through (toolbar: Selection settings).\nCtrl adds to the selection, Shift takes out.");
                            }
                        }
                    }
                    break;
                case 8: RenderCameraTab(snapshot); break;
                case 9: _environment.RenderTab(snapshot, _notice); break;
                case 10:
                    ImGui::BeginDisabled(!snapshot.layers->Editable("patrol:default"));
                    _patrols.RenderTab(snapshot, _notice);
                    ImGui::EndDisabled();
                    break;
                case 11: _mcp.RenderTab(); break;
                case 12: RenderLayersTab(snapshot); break;
                }
                ImGui::PopID();
            }
            ImGui::EndChild();

            ImGui::Separator();
            ImGui::TextWrapped("%s", snapshot.status.c_str());
            if (!_notice.empty()) {
                ImGui::TextColored(kWarnColour, "%s", _notice.c_str());
            }
        }
        ImGui::End();
        ImGui::PopStyleVar();

        if (!open) {
            if (snapshot.offline) {
                _pendingExit = true;
            }
            else {
                _closeRequested.store(true, std::memory_order_release);
            }
        }
    }

    void MapEditor::RenderLibraryTab(const EditorSnapshot &snapshot) {
        ImGui::Spacing();

        if (snapshot.armedModel != AssetLibrary::kNone && !snapshot.armedIsEffect) {
            // Doors, smart objects and items sit past the last mesh row.
            const std::string_view name = ArmedName(snapshot);
            ImGui::TextColored(kAccentHover, "Placing %.*s", static_cast<int>(name.size()), name.data());
            if (snapshot.armedSized) {
                ImGui::SameLine();
                ImGui::TextDisabled("%.1f x %.1f x %.1f m", snapshot.armedSize.x * snapshot.armedScale, snapshot.armedSize.y * snapshot.armedScale, snapshot.armedSize.z * snapshot.armedScale);
            }
            ImGui::TextDisabled("yaw %.0f deg, scale x%.2f", snapshot.armedYaw, snapshot.armedScale);
            if (ImGui::Button("Stop placing [Esc / RMB]", ImVec2(-1.0F, 0.0F))) {
                Command command;
                command.kind = CommandKind::Disarm;
                Queue(std::move(command));
            }
        }
        else if (_library.Selected() != AssetLibrary::kNone) {
            ImGui::BeginDisabled(!snapshot.cameraHeld);
            if (ImGui::Button("Pick it up again", ImVec2(-1.0F, 0.0F))) {
                Command command;
                command.kind = CommandKind::Arm;
                command.id   = static_cast<std::uint64_t>(_library.Selected());
                Queue(std::move(command));
            }
            ImGui::EndDisabled();
        }
        else {
            ImGui::TextDisabled("Click a mesh to pick it up; a preview follows the cursor.");
        }

        const float optionsHeight = ImGui::GetFrameHeightWithSpacing() * 2.0F;
        _library.Render(std::max(ImGui::GetContentRegionAvail().y - optionsHeight, 200.0F));
        _frameHover = _library.Hovered();

        if (ImGui::CollapsingHeader("Placement")) {
            RenderPlacementOptions();
        }
    }

    void MapEditor::RenderEffectsTab(const EditorSnapshot &snapshot) {
        ImGui::Spacing();

        if (snapshot.armedModel != AssetLibrary::kNone && snapshot.armedIsEffect) {
            const std::string_view name = ArmedName(snapshot);
            ImGui::TextColored(kEffectColour, "Placing %.*s", static_cast<int>(name.size()), name.data());
            ImGui::TextDisabled("yaw %.0f deg, size x%.2f", snapshot.armedYaw, snapshot.armedScale);
            if (ImGui::Button("Stop placing [Esc / RMB]", ImVec2(-1.0F, 0.0F))) {
                Command command;
                command.kind = CommandKind::Disarm;
                Queue(std::move(command));
            }
        }
        else {
            ImGui::TextDisabled("Rest on an effect to see it; click it to place it.");
        }

        // Every emitter on this machine counts, the game's own included.
        const float used = static_cast<float>(snapshot.liveEmitters) / static_cast<float>(KCDC::Features::Vfx::VfxService::kMaxLiveEmitters);
        char budget[64];
        std::snprintf(budget, sizeof(budget), "%zu of %zu emitters", snapshot.liveEmitters, KCDC::Features::Vfx::VfxService::kMaxLiveEmitters);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, used > 0.85F ? ImVec4(0.85F, 0.3F, 0.25F, 1.0F) : kAccent);
        ImGui::ProgressBar(std::min(used, 1.0F), ImVec2(-1.0F, 0.0F), budget);
        ImGui::PopStyleColor();

        const float optionsHeight = ImGui::GetFrameHeightWithSpacing() * 2.0F;
        _effectLibrary.Render(std::max(ImGui::GetContentRegionAvail().y - optionsHeight, 200.0F));
        if (_effectLibrary.Hovered() != AssetLibrary::kNone) {
            _frameHover       = _effectLibrary.Hovered();
            _frameHoverEffect = true;
        }

        if (ImGui::CollapsingHeader("New effects")) {
            Shared::Game::VfxTuning &tuning = _placementDraft.effectTuning;
            ImGui::SetNextItemWidth(160.0F);
            ImGui::SliderFloat("Size", &tuning.scale, Shared::Game::VfxTuning::kMinScale, 8.0F, "x%.2f", ImGuiSliderFlags_Logarithmic);
            ImGui::SetNextItemWidth(160.0F);
            ImGui::SliderFloat("Particles", &tuning.countScale, Shared::Game::VfxTuning::kMinScale, 4.0F, "x%.2f", ImGuiSliderFlags_Logarithmic);
            ImGui::Checkbox("Start already running", &tuning.prime);
            HelpMarker("Starts a loop at the state it would reach after running a while.\nWhat a fire or a smoke column wants.");
            ImGui::SetNextItemWidth(160.0F);
            ImGui::SliderFloat("One-shot repeat", &_placementDraft.oneShotRepeat, 0.0F, 30.0F, "%.1f s");
            HelpMarker("A one-shot effect plays once and is gone; this restarts it so it can be seen.\n0 plays it once.");
            ImGui::TextDisabled("Placement, grid and scatter are shared with the Library tab.");
        }
    }

    void MapEditor::RenderEffectList(const EditorSnapshot &snapshot) {
        if (snapshot.effectRows.empty()) {
            return;
        }

        ImGui::SeparatorText("Effects");
        if (!ImGui::BeginTable("##kcdc_editor_effects", 2, ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp, ImVec2(0.0F, 120.0F))) {
            return;
        }
        ImGui::TableSetupColumn("Effect", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 52.0F);

        for (const EffectRow &row : snapshot.effectRows) {
            if (!Framework::Utils::StringUtils::ContainsIgnoreCase(row.label, _sceneFilter) && !Framework::Utils::StringUtils::ContainsIgnoreCase(row.name, _sceneFilter)) {
                continue;
            }

            ImGui::PushID(static_cast<int>(row.id) + 0x20000000);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();

            const SelectionRef ref {SelectionKind::Effect, row.id};
            const bool isSelected = (snapshot.selection.kind == SelectionKind::Effect && snapshot.selection.id == row.id) || snapshot.selectedRefs.Contains(ref);
            if (ImGui::Selectable(row.label.c_str(), isSelected, ImGuiSelectableFlags_SpanAllColumns)) {
                Command command;
                command.kind   = CommandKind::Select;
                command.target = SelectionKind::Effect;
                command.id     = row.id;
                command.enable = ImGui::GetIO().KeyCtrl;
                Queue(std::move(command));
            }
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                Command command;
                command.kind = CommandKind::Focus;
                Queue(std::move(command));
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
                ImGui::SetTooltip("%s\n%.1f, %.1f, %.1f\nDouble-click to frame it.", row.name.c_str(), row.position.x, row.position.y, row.position.z);
            }

            ImGui::TableNextColumn();
            if (row.playing) {
                ImGui::TextDisabled("playing");
            }
            else {
                ImGui::TextColored(kWarnColour, "stopped");
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    void MapEditor::RenderLightsTab(const EditorSnapshot &snapshot) {
        ImGui::SeparatorText("Point lights");
        ImGui::TextWrapped("Place a standalone light anywhere to illuminate nearby surfaces. Move it with the gizmo or Position fields in Object, then adjust its color, intensity, radius and shadows.");
        if (ImGui::Button("Add light in front of camera", ImVec2(-1, 0))) {
            Command command;
            command.kind     = CommandKind::PlaceLight;
            command.position = KCDC::Common::ToNative(KCDC::Common::ToGlm(snapshot.camera.position) + KCDC::Common::ToGlm(Camera::PoseForward(snapshot.camera)) * 3.0F);
            Queue(std::move(command));
        }
        for (const auto &row : snapshot.lightRows) {
            ImGui::PushID(static_cast<int>(row.id));
            const SelectionRef ref {SelectionKind::Light, row.id};
            const bool selected = (snapshot.selection.kind == ref.kind && snapshot.selection.id == ref.id) || snapshot.selectedRefs.Contains(ref);
            ImGui::BeginDisabled(!snapshot.layers->Editable(LayerKey(ref)));
            const auto label = "Light #" + std::to_string(row.id) + (row.playing ? "" : " (inactive)");
            if (ImGui::Selectable(label.c_str(), selected)) {
                Command command;
                command.kind   = CommandKind::Select;
                command.target = ref.kind;
                command.id     = ref.id;
                command.enable = ImGui::GetIO().KeyCtrl;
                Queue(std::move(command));
            }
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        if (snapshot.selection.kind == SelectionKind::Light) {
            RenderLightTuning(snapshot);
        }
    }

    void MapEditor::RenderLightTuning(const EditorSnapshot &snapshot) {
        const auto &selection = snapshot.selection;
        if (_lightDraftId != selection.id || !ImGui::IsAnyItemActive()) {
            _lightDraft   = selection.lightTuning;
            _lightDraftId = selection.id;
        }
        ImGui::BeginDisabled(!snapshot.layers->Editable(LayerKey({SelectionKind::Light, selection.id})));
        ImGui::SeparatorText("Light settings");
        bool commit = false;
        ImGui::ColorEdit3("Color", &_lightDraft.color.x);
        commit |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::DragFloat("Intensity", &_lightDraft.intensity, 0.05F, 0.0F, 100.0F, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        commit |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::DragFloat("Radius (m)", &_lightDraft.radius, 0.1F, 0.1F, 100.0F, "%.1f", ImGuiSliderFlags_AlwaysClamp);
        commit |= ImGui::IsItemDeactivatedAfterEdit();
        commit |= ImGui::Checkbox("Cast shadows", &_lightDraft.castShadows);
        if (commit && _lightDraft.IsSane()) {
            Command command;
            command.kind        = CommandKind::SetLightTuning;
            command.id          = selection.id;
            command.lightTuning = _lightDraft;
            Queue(std::move(command));
        }
        ImGui::EndDisabled();
    }

    void MapEditor::RenderLightMarkers(const EditorSnapshot &snapshot) {
        if (!snapshot.cameraHeld) {
            return;
        }
        const auto *viewport = ImGui::GetMainViewport();
        auto *draw           = ImGui::GetBackgroundDrawList();
        for (const auto &row : snapshot.lightRows) {
            if (!snapshot.layers->Visible(LayerKey({SelectionKind::Light, row.id}))) {
                continue;
            }
            float x = 0, y = 0;
            if (!Camera::ProjectToNdc(snapshot.camera, row.position, x, y)) {
                continue;
            }
            const auto at = NdcToScreen(*viewport, x, y);
            const SelectionRef ref {SelectionKind::Light, row.id};
            const bool selected = (snapshot.selection.kind == ref.kind && snapshot.selection.id == ref.id) || snapshot.selectedRefs.Contains(ref);
            const auto color    = row.playing ? IM_COL32(255, 225, 100, 240) : IM_COL32(220, 80, 80, 240);
            draw->AddCircle(at, 7, color, 16, selected ? 3.0F : 1.5F);
            draw->AddLine(ImVec2(at.x - 11, at.y), ImVec2(at.x + 11, at.y), color);
            draw->AddLine(ImVec2(at.x, at.y - 11), ImVec2(at.x, at.y + 11), color);
            if (selected) {
                draw->AddText(ImVec2(at.x + 15, at.y - 7), color, "Point light");
            }
        }
    }

    void MapEditor::RenderEffectTuning(const EditorSnapshot &snapshot) {
        const SelectionInfo &selection = snapshot.selection;
        if (_tuningDraftId != selection.id || !ImGui::IsAnyItemActive()) {
            _tuningDraft   = selection.tuning;
            _tuningDraftId = selection.id;
        }

        ImGui::SeparatorText("Effect");

        // Committed on release: each change restarts the emitter.
        bool commit      = false;
        const auto field = [&commit](bool) {
            commit = commit || ImGui::IsItemDeactivatedAfterEdit();
        };

        field(ImGui::SliderFloat("Size", &_tuningDraft.scale, Shared::Game::VfxTuning::kMinScale, 8.0F, "x%.2f", ImGuiSliderFlags_Logarithmic));
        field(ImGui::SliderFloat("Particles", &_tuningDraft.countScale, Shared::Game::VfxTuning::kMinScale, 4.0F, "x%.2f", ImGuiSliderFlags_Logarithmic));
        field(ImGui::SliderFloat("Speed", &_tuningDraft.speedScale, 0.0F, 4.0F, "x%.2f"));
        field(ImGui::SliderFloat("Time", &_tuningDraft.timeScale, Shared::Game::VfxTuning::kMinScale, 4.0F, "x%.2f"));
        field(ImGui::SliderFloat("Strength", &_tuningDraft.strength, -1.0F, 1.0F, _tuningDraft.strength < 0.0F ? "as authored" : "%.2f"));
        field(ImGui::SliderFloat("Repeat every", &_tuningDraft.pulsePeriod, 0.0F, 30.0F, _tuningDraft.pulsePeriod <= 0.0F ? "never" : "%.1f s"));
        if (ImGui::Checkbox("Start already running", &_tuningDraft.prime)) {
            commit = true;
        }
        if (!selection.continuous) {
            ImGui::TextDisabled("A one-shot: it plays once per repeat.");
        }

        if (commit && _tuningDraft.IsSane()) {
            Command command;
            command.kind   = CommandKind::SetEffectTuning;
            command.id     = selection.id;
            command.tuning = _tuningDraft;
            Queue(std::move(command));
        }
    }

    void MapEditor::RenderEffectMarkers(const EditorSnapshot &snapshot) {
        if (!snapshot.cameraHeld || snapshot.effectRows.empty()) {
            return;
        }

        const ImGuiViewport *viewport = ImGui::GetMainViewport();
        ImDrawList *draw              = ImGui::GetBackgroundDrawList();
        const ImVec2 mouse            = ImGui::GetIO().MousePos;

        for (const EffectRow &row : snapshot.effectRows) {
            if (!snapshot.layers->Visible(LayerKey({SelectionKind::Effect, row.id}))) {
                continue;
            }
            float ndcX = 0.0F;
            float ndcY = 0.0F;
            if (!KCDC::Features::Camera::ProjectToNdc(snapshot.camera, row.position, ndcX, ndcY)) {
                continue;
            }
            const ImVec2 at = NdcToScreen(*viewport, ndcX, ndcY);

            const SelectionRef ref {SelectionKind::Effect, row.id};
            const bool selected = (snapshot.selection.kind == SelectionKind::Effect && snapshot.selection.id == row.id) || snapshot.selectedRefs.Contains(ref);
            const ImU32 colour  = !row.playing ? IM_COL32(200, 90, 80, 220) : selected ? IM_COL32(255, 220, 120, 255) : IM_COL32(255, 160, 80, 200);

            // A diamond: effects have nothing else to see them by when idle.
            constexpr float kSize = 7.0F;
            draw->AddQuadFilled(ImVec2(at.x, at.y - kSize), ImVec2(at.x + kSize, at.y), ImVec2(at.x, at.y + kSize), ImVec2(at.x - kSize, at.y), (colour & 0x00FFFFFFU) | 0x60000000U);
            draw->AddQuad(ImVec2(at.x, at.y - kSize), ImVec2(at.x + kSize, at.y), ImVec2(at.x, at.y + kSize), ImVec2(at.x - kSize, at.y), colour, selected ? 2.0F : 1.2F);

            const float dx = mouse.x - at.x;
            const float dy = mouse.y - at.y;
            if (selected || dx * dx + dy * dy < 400.0F) {
                draw->AddText(ImVec2(at.x + kSize + 4.0F, at.y - 7.0F), colour, row.label.c_str());
            }
        }
    }

    void MapEditor::RenderPlacementOptions() {
        static const char *kPhysicsLabels[] = {"Rigid -- falls and can be pushed", "Static -- solid and immovable", "None -- geometry only, no collision"};

        int physicsIndex = IndexFromPhysics(_placementDraft.physics);
        ImGui::SetNextItemWidth(-1.0F);
        if (ImGui::Combo("##kcdc_editor_physics", &physicsIndex, kPhysicsLabels, IM_ARRAYSIZE(kPhysicsLabels))) {
            _placementDraft.physics = PhysicsFromIndex(physicsIndex);
        }

        ImGui::Checkbox("Stand it on the surface", &_placementDraft.alignToSurface);
        HelpMarker("Tilts the mesh to the slope under the cursor instead of keeping it upright.\nRight for a rock on a hillside, wrong for a house.");

        ImGui::Checkbox("Snap to grid", &_placementDraft.snapToGrid);
        ImGui::SameLine();
        ImGui::BeginDisabled(!_placementDraft.snapToGrid);
        ImGui::SetNextItemWidth(90.0F);
        ImGui::DragFloat("##kcdc_editor_grid", &_placementDraft.gridStep, 0.05F, 0.1F, 50.0F, "%.2f m");
        ImGui::EndDisabled();

        ImGui::Checkbox("Random turn", &_placementDraft.randomYaw);
        ImGui::SameLine();
        ImGui::Checkbox("Random size", &_placementDraft.randomScale);
        if (_placementDraft.randomScale) {
            ImGui::SetNextItemWidth(-1.0F);
            ImGui::DragFloatRange2("##kcdc_editor_scale_range", &_placementDraft.scaleMin, &_placementDraft.scaleMax, 0.01F, kMinBrushScale, kMaxBrushScale, "from x%.2f", "to x%.2f");
        }
        HelpMarker("Each placement rolls a new turn and size: scatter rocks,\nbushes and debris without them all looking stamped.");

        ImGui::SetNextItemWidth(120.0F);
        ImGui::DragFloat("Height offset", &_placementDraft.heightOffset, 0.01F, -20.0F, 20.0F, "%.2f m");
        ImGui::SetNextItemWidth(120.0F);
        ImGui::DragFloat("Fallback distance", &_placementDraft.fallbackDistance, 0.25F, 0.5F, 200.0F, "%.1f m");
        HelpMarker("How far ahead a mesh goes when the cursor points at nothing, e.g. the sky.");
    }

    void MapEditor::RenderSceneTab(const EditorSnapshot &snapshot) {
        ImGui::Spacing();
        ImGui::SetNextItemWidth(-1.0F);
        ImGui::InputTextWithHint("##kcdc_editor_scene_filter", "Filter placed objects...", _sceneFilter, sizeof(_sceneFilter));

        if (ImGui::BeginTable("##kcdc_editor_outliner", 3, ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp, ImVec2(0.0F, 220.0F))) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 34.0F);
            ImGui::TableSetupColumn("Mesh", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Physics", ImGuiTableColumnFlags_WidthFixed, 52.0F);
            ImGui::TableHeadersRow();

            for (const SceneRow &row : snapshot.rows) {
                if (!Framework::Utils::StringUtils::ContainsIgnoreCase(row.name, _sceneFilter) && !Framework::Utils::StringUtils::ContainsIgnoreCase(row.model, _sceneFilter)) {
                    continue;
                }

                ImGui::PushID(static_cast<int>(row.id));
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%llu", static_cast<unsigned long long>(row.id));

                ImGui::TableNextColumn();
                char label[160];
                std::snprintf(label, sizeof(label), "%s%s", row.name.c_str(), row.entityId == 0 ? "  (not built)" : "");
                const bool isSelected = snapshot.selection.kind == SelectionKind::Placed && snapshot.selection.id == row.id;
                const bool inGroup    = snapshot.selectedRefs.Contains(SelectionRef {SelectionKind::Placed, row.id});
                if (ImGui::Selectable(label, isSelected || inGroup, ImGuiSelectableFlags_SpanAllColumns)) {
                    Command command;
                    command.kind   = CommandKind::Select;
                    command.target = SelectionKind::Placed;
                    command.id     = row.id;
                    command.enable = ImGui::GetIO().KeyCtrl;
                    Queue(std::move(command));
                }
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    Command command;
                    command.kind = CommandKind::Focus;
                    Queue(std::move(command));
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
                    ImGui::SetTooltip("%s\n%.1f, %.1f, %.1f\nDouble-click to frame it.", row.model.c_str(), row.position.x, row.position.y, row.position.z);
                }

                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", row.kind == PlacedKind::Prop ? KCDC::Common::Spawn::PropPhysicsToString(row.physics) : PlacedKindLabel(row.kind));
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        RenderEffectList(snapshot);
        RenderLightsTab(snapshot);

        if (snapshot.rows.empty() && snapshot.effectRows.empty() && snapshot.lightRows.empty()) {
            ImGui::TextDisabled("Nothing placed yet -- pick a mesh in the library.");
        }

        if ((!snapshot.rows.empty() || !snapshot.effectRows.empty() || !snapshot.lightRows.empty()) && DangerButton("Clear everything placed", ImVec2(-1.0F, 0.0F))) {
            Command command;
            command.kind = CommandKind::Clear;
            Queue(std::move(command));
        }
    }

    void MapEditor::RenderLevelTab(const EditorSnapshot &snapshot) {
        const auto deleted             = std::count_if(snapshot.worldRows.begin(), snapshot.worldRows.end(), [](const WorldRow &row) {
            return row.hidden || row.swapped;
        });
        const std::string deletedLabel = "Deleted (" + std::to_string(deleted) + ")###level_deleted";
        if (ImGui::BeginTabBar("##level_lists")) {
            if (ImGui::BeginTabItem("Nearby")) {
                RenderNearbyLevelObjects(snapshot);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(deletedLabel.c_str())) {
                RenderLevelEdits(snapshot);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }

    void MapEditor::RenderLevelEdits(const EditorSnapshot &snapshot) {
        ImGui::Spacing();
        ImGui::TextWrapped("Deleted level objects stay here even when far away. Double-click an object to frame it.");
        ImGui::Checkbox("Include moved objects", &_includeMovedLevelEdits);
        ImGui::SetNextItemWidth(-1.0F);
        ImGui::InputTextWithHint("##level_edit_filter", "Filter deleted objects: name, class or mesh...", _levelEditFilter, sizeof(_levelEditFilter));

        const bool hasEdits = std::any_of(snapshot.worldRows.begin(), snapshot.worldRows.end(), [](const WorldRow &row) {
            return row.hidden || row.moved || row.swapped;
        });
        ImGui::BeginDisabled(!hasEdits);
        if (ImGui::Button("Restore all level edits", ImVec2(-1.0F, 0.0F))) {
            Command command;
            command.kind = CommandKind::RestoreAllWorld;
            Queue(std::move(command));
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
            ImGui::SetTooltip("Undoes every level-object move and deletion, including objects outside this filter.");
        }

        if (ImGui::BeginTable("##kcdc_editor_world", 3, ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp, ImVec2(0.0F, std::max(ImGui::GetContentRegionAvail().y, 160.0F)))) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Object", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 80.0F);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 60.0F);
            ImGui::TableHeadersRow();

            std::size_t shown = 0;
            for (const WorldRow &row : snapshot.worldRows) {
                if (!row.hidden && !row.swapped && !(_includeMovedLevelEdits && row.moved)) {
                    continue;
                }
                if (!Framework::Utils::StringUtils::ContainsIgnoreCase(row.label, _levelEditFilter) && !Framework::Utils::StringUtils::ContainsIgnoreCase(row.kind, _levelEditFilter)) {
                    continue;
                }
                ++shown;
                ImGui::PushID(static_cast<int>(row.id) + 0x40000000);
                ImGui::TableNextRow();

                ImGui::TableNextColumn();
                const bool isSelected = (snapshot.selection.kind == SelectionKind::World && snapshot.selection.id == row.id) || snapshot.selectedRefs.Contains(SelectionRef {SelectionKind::World, row.id});
                if (ImGui::Selectable(row.label.c_str(), isSelected, ImGuiSelectableFlags_AllowDoubleClick)) {
                    Command command;
                    command.kind   = CommandKind::Select;
                    command.target = SelectionKind::World;
                    command.id     = row.id;
                    command.enable = ImGui::GetIO().KeyCtrl;
                    Queue(std::move(command));
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        Command focus;
                        focus.kind = CommandKind::Focus;
                        Queue(std::move(focus));
                    }
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
                    ImGui::SetTooltip("%s\n%.1f, %.1f, %.1f%s", row.kind.c_str(), row.position.x, row.position.y, row.position.z, row.portable ? "" : "\n\nNot authored by the level, so a saved map cannot name it.");
                }

                ImGui::TableNextColumn();
                ImGui::TextColored(row.hidden || row.swapped ? kWarnColour : kWorldColour, row.hidden ? "Deleted" : row.swapped ? "Swapped" : "Moved");
                if (!row.loaded) {
                    ImGui::TextDisabled("Unloaded");
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
                        ImGui::SetTooltip("Streamed out: the game dropped this object while the camera is away.\nThe edit is kept, saved, and applied again when it streams back in.\nFrom far off the level's baked distant mesh still shows it as it was.");
                    }
                }
                else if (row.layerOff) {
                    ImGui::TextDisabled("layer off");
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
                        ImGui::SetTooltip("Its level layer is switched off right now, so the game hides it.\nIt shows again, moved or not, when the level switches the layer on.");
                    }
                }

                ImGui::TableNextColumn();
                if (row.hidden && ImGui::SmallButton("Restore")) {
                    Command command;
                    command.kind   = CommandKind::HideWorld;
                    command.id     = row.id;
                    command.enable = false;
                    Queue(std::move(command));
                }
                else if (!row.hidden && row.swapped && ImGui::SmallButton("Unswap")) {
                    Command command;
                    command.kind = CommandKind::ReplaceWorld;
                    command.id   = row.id;
                    Queue(std::move(command));
                }
                ImGui::PopID();
            }
            if (shown == 0) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextDisabled("No matching level edits.");
            }
            ImGui::EndTable();
        }
    }

    void MapEditor::RenderNearbyLevelObjects(const EditorSnapshot &snapshot) {
        ImGui::Spacing();
        ImGui::TextWrapped("What the level itself put around the camera: its entities, and its brushes and vegetation.");

        ImGui::Checkbox("Prefer solid objects", &_viewport.pickPreferPhysics);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Prefer the solid object under the cursor over overlapping bounding boxes.\nTurn off to pick non-solid decoration in front of walls.\nThe list below can select either kind directly.");
        }

        ImGui::SetNextItemWidth(160.0F);
        ImGui::SliderFloat("Radius", &_levelRadius, 5.0F, kMaxBrowseRadius, "%.0f m", ImGuiSliderFlags_Logarithmic);
        ImGui::SameLine();
        ImGui::BeginDisabled(!snapshot.cameraHeld);
        if (ImGui::Button("List", ImVec2(-1.0F, 0.0F))) {
            Command command;
            command.kind  = CommandKind::Browse;
            command.value = _levelRadius;
            Queue(std::move(command));
        }
        ImGui::EndDisabled();

        ImGui::Checkbox("Entities", &_levelEntities);
        ImGui::SameLine();
        ImGui::Checkbox("Geometry", &_levelGeometry);
        ImGui::SetNextItemWidth(-1.0F);
        ImGui::InputTextWithHint("##kcdc_editor_level_filter", "Filter: name, class or mesh path...", _levelFilter, sizeof(_levelFilter));

        if (snapshot.levelTruncated) {
            ImGui::TextColored(kWarnColour, "Not everything is listed -- bring the radius down.");
        }

        if (ImGui::BeginTable("##kcdc_editor_level", 3, ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp, ImVec2(0.0F, std::max(ImGui::GetContentRegionAvail().y, 160.0F)))) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Object", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("m", ImGuiTableColumnFlags_WidthFixed, 38.0F);
            ImGui::TableHeadersRow();

            int shown = 0;
            for (const LevelRow &row : *snapshot.levelRows) {
                const bool isEntity = row.kind == WorldPickKind::Entity;
                if ((isEntity && !_levelEntities) || (!isEntity && !_levelGeometry)) {
                    continue;
                }
                if (!Framework::Utils::StringUtils::ContainsIgnoreCase(row.label, _levelFilter) && !Framework::Utils::StringUtils::ContainsIgnoreCase(row.detail, _levelFilter)) {
                    continue;
                }

                ImGui::PushID(shown++);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();

                if (row.hidden) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                }
                if (ImGui::Selectable(row.label.c_str(), false, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
                    Command command;
                    command.kind     = CommandKind::SelectBrowsed;
                    command.pickKind = row.kind;
                    command.node     = row.node;
                    command.entityId = row.entityId;
                    command.position = row.position;
                    command.enable   = ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
                    Queue(std::move(command));
                }
                if (row.hidden) {
                    ImGui::PopStyleColor();
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
                    ImGui::SetTooltip("%s\n%.1f, %.1f, %.1f%s\n\nClick to select, double-click to frame.", row.detail.c_str(), row.position.x, row.position.y, row.position.z, row.hidden ? "\nhidden" : row.edited ? "\nalready in the editor's list" : "");
                }

                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", isEntity ? row.detail.c_str() + (row.detail.rfind("Entity  ", 0) == 0 ? 8 : 0) : row.detail.substr(0, row.detail.find("  ")).c_str());

                ImGui::TableNextColumn();
                ImGui::TextDisabled("%.0f", row.distance);
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }

    void MapEditor::RenderObjectTab(const EditorSnapshot &snapshot) {
        if (ImGui::Button("Copy selection as API calls")) {
            CopySelection(snapshot);
        }
        const SelectionInfo &selection = snapshot.selection;
        if (selection.kind == SelectionKind::None) {
            ImGui::Spacing();
            ImGui::TextDisabled("Click anything in the world -- a prop you placed, or the level's own.");
            return;
        }

        const bool isWorld  = selection.kind == SelectionKind::World;
        const bool isLight  = selection.kind == SelectionKind::Light;
        const bool isEffect = selection.kind == SelectionKind::Effect;
        // A door, smart object or item: built at its own size, with physics of its own.
        const bool isBuilt = selection.kind == SelectionKind::Placed && selection.placedKind != PlacedKind::Prop;

        ImGui::Spacing();
        if (!snapshot.extraSelection.empty()) {
            ImGui::TextColored(kAccentHover, "%zu selected -- the gizmo moves them together", snapshot.extraSelection.size() + 1);
            if (ImGui::Button("Duplicate them [Ctrl+D]", ImVec2(ImGui::GetContentRegionAvail().x * 0.5F - 2.0F, 0.0F))) {
                Command command;
                command.kind = CommandKind::DuplicateSelection;
                Queue(std::move(command));
            }
            ImGui::SameLine();
            if (DangerButton("Delete them [Del]", ImVec2(-1.0F, 0.0F))) {
                Command command;
                command.kind = CommandKind::DeleteSelection;
                Queue(std::move(command));
            }
            ImGui::Separator();
            ImGui::TextDisabled("The one the gizmo is on:");
        }
        if (ImGui::Button("Select similar nearby", ImVec2(-1.0F, 0.0F))) {
            SelectionQuery query;
            query.similarTo     = SelectionRef {selection.kind, selection.id};
            query.region.radius = _similarRadius;
            QueueSelection(std::move(query), ModifierSelectionMode());
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
            ImGui::SetTooltip("Selects every object with the same mesh within %.0f m -- for an entity without one, the same class.\nCtrl adds to the selection, Shift takes out. The radius is in the toolbar's Selection settings.", _similarRadius);
        }
        ImGui::TextColored(isWorld ? kWorldColour : isEffect ? kEffectColour : kPlacedColour, "%s", selection.label.c_str());
        ImGui::TextDisabled("%s", isWorld ? "the level's own" : isEffect ? "an effect placed by the editor" : isBuilt ? "placed by the editor; the server builds the real one from the export" : "placed by the editor");
        ImGui::TextWrapped("%s", selection.detail.c_str());

        if (selection.hidden) {
            ImGui::TextColored(kWarnColour, "Taken out of the world: not drawn, not solid.");
        }
        if (isWorld && !selection.loaded) {
            ImGui::TextColored(kWarnColour, "Streamed out -- the edit waits and applies when it streams back in.");
        }
        else if (isWorld && selection.layerOff) {
            ImGui::TextColored(kWarnColour, "On level layer %u, switched off: the game hides it for now.", static_cast<unsigned>(selection.layerId));
        }
        else if (isWorld && selection.onLayer) {
            ImGui::TextDisabled("On level layer %u, which the level switches; edits survive the switch.", static_cast<unsigned>(selection.layerId));
        }
        ImGui::Separator();

        bool changed = ImGui::DragFloat3("Position", &_editPosition.x, 0.05F, 0.0F, 0.0F, "%.2f");
        if (ImGui::IsItemActivated()) {
            _fieldGesture = NextGesture();
        }

        SDK::Ang3 angles = SDK::AnglesFromQuat(_editRotation);
        float degrees[3] {angles.x * kRadToDegrees, angles.y * kRadToDegrees, angles.z * kRadToDegrees};
        if (!isLight && ImGui::DragFloat3("Pitch/roll/yaw", degrees, 0.5F, 0.0F, 0.0F, "%.1f")) {
            _editRotation = SDK::QuatFromAngles(SDK::Ang3 {degrees[0] * kDegreesToRad, degrees[1] * kDegreesToRad, degrees[2] * kDegreesToRad});
            changed       = true;
        }
        if (ImGui::IsItemActivated()) {
            _fieldGesture = NextGesture();
        }

        const SDK::Vec3 previousScale = _editScale;
        if (!isEffect && !isLight && !isBuilt && ImGui::DragFloat3("Scale", &_editScale.x, 0.01F, kMinObjectScale, kMaxObjectScale, "%.3f")) {
            if (_uniformScale) {
                float ratio = 1.0F;
                if (_editScale.x != previousScale.x && previousScale.x > 0.0F) {
                    ratio = _editScale.x / previousScale.x;
                }
                else if (_editScale.y != previousScale.y && previousScale.y > 0.0F) {
                    ratio = _editScale.y / previousScale.y;
                }
                else if (_editScale.z != previousScale.z && previousScale.z > 0.0F) {
                    ratio = _editScale.z / previousScale.z;
                }
                _editScale = ClampObjectScale(SDK::Vec3 {previousScale.x * ratio, previousScale.y * ratio, previousScale.z * ratio});
            }
            changed = true;
        }
        if (!isEffect && !isLight && !isBuilt && ImGui::IsItemActivated()) {
            _fieldGesture = NextGesture();
        }
        if (!isEffect && !isLight && !isBuilt) {
            ImGui::Checkbox("Keep proportions", &_uniformScale);
        }

        const bool nonUniform = std::fabs(_editScale.x - _editScale.y) > 1e-3F || std::fabs(_editScale.y - _editScale.z) > 1e-3F;
        if (selection.kind == SelectionKind::Placed && nonUniform) {
            ImGui::TextColored(kWarnColour, "Stretched: its collision is built at the largest axis.");
        }

        if (changed) {
            QueueTransform(selection, _fieldGesture != 0 ? _fieldGesture : NextGesture());
        }

        if (isBuilt) {
            RenderPlacedSettings(selection);
        }
        else if (selection.kind == SelectionKind::Placed) {
            int physicsIndex                    = IndexFromPhysics(selection.physics);
            static const char *kPhysicsLabels[] = {"Rigid", "Static", "None"};
            ImGui::SetNextItemWidth(140.0F);
            if (ImGui::Combo("Physics", &physicsIndex, kPhysicsLabels, IM_ARRAYSIZE(kPhysicsLabels))) {
                Command command;
                command.kind    = CommandKind::SetPhysics;
                command.id      = selection.id;
                command.physics = PhysicsFromIndex(physicsIndex);
                Queue(std::move(command));
            }
        }

        ImGui::Separator();

        const float half = ImGui::GetContentRegionAvail().x * 0.5F - 2.0F;
        if (ImGui::Button("Drop to the ground [End]", ImVec2(half, 0.0F))) {
            Command command;
            command.kind = CommandKind::DropToGround;
            Queue(std::move(command));
        }
        ImGui::SameLine();
        if (ImGui::Button("Frame it [F]", ImVec2(-1.0F, 0.0F))) {
            Command command;
            command.kind = CommandKind::Focus;
            Queue(std::move(command));
        }

        if (isEffect || isLight) {
            if (isLight) {
                RenderLightTuning(snapshot);
            }
            else {
                RenderEffectTuning(snapshot);
            }

            ImGui::Separator();
            if (ImGui::Button("Duplicate [Ctrl+D]", ImVec2(half, 0.0F))) {
                Command command;
                command.kind = CommandKind::DuplicateSelection;
                Queue(std::move(command));
            }
            ImGui::SameLine();
            if (DangerButton("Delete [Del]", ImVec2(-1.0F, 0.0F))) {
                Command command;
                command.kind = CommandKind::DeleteSelection;
                Queue(std::move(command));
            }
            return;
        }

        if (!isWorld) {
            if (selection.collisionRebuildFailed) {
                ImGui::TextColored(kWarnColour, "Collision could not be updated. Try rebuilding it.");
            }
            if (ImGui::Button("Rebuild its collision", ImVec2(half, 0.0F))) {
                Command command;
                command.kind = CommandKind::Rebuild;
                command.id   = selection.id;
                Queue(std::move(command));
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
                ImGui::SetTooltip("Collision updates automatically after scaling pauses.\nUse this to retry if an object could not be rebuilt.");
            }
            ImGui::SameLine();
            if (ImGui::Button("Duplicate [Ctrl+D]", ImVec2(-1.0F, 0.0F))) {
                Command command;
                command.kind = CommandKind::Duplicate;
                command.id   = selection.id;
                Queue(std::move(command));
            }

            if (DangerButton("Delete [Del]", ImVec2(-1.0F, 0.0F))) {
                Command command;
                command.kind = CommandKind::Remove;
                command.id   = selection.id;
                Queue(std::move(command));
            }
            return;
        }

        ImGui::BeginDisabled(selection.hidden || !selection.replacement.empty());
        if (ImGui::Button("Copy as a prop [Ctrl+D]", ImVec2(half, 0.0F))) {
            Command command;
            command.kind = CommandKind::DuplicateSelection;
            Queue(std::move(command));
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Places an editable prop of the same mesh beside it.\nOnly meshes in the asset library can be copied; the level's own material changes are not.");
        }
        ImGui::SameLine();
        if (ImGui::Button("Convert to a prop", ImVec2(-1.0F, 0.0F))) {
            Command command;
            command.kind = CommandKind::ConvertSelection;
            Queue(std::move(command));
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Takes it out of the world and puts an editable prop of the same mesh where it stood, as one undo step.\nWorks on every selected level object whose mesh is in the asset library.");
        }
        ImGui::EndDisabled();

        if (ImGui::Button(selection.hidden ? "Put it back in the world" : "Take it out of the world [Del]", ImVec2(-1.0F, 0.0F))) {
            Command command;
            command.kind   = CommandKind::HideWorld;
            command.id     = selection.id;
            command.enable = !selection.hidden;
            Queue(std::move(command));
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
            ImGui::SetTooltip("Not drawn and not solid, and it goes into the saved map's removal list.\nReversible: Ctrl+Z, or the button again.\n\nFrom far off the level's baked distant mesh still draws it; up close it is gone.");
        }

        RenderWorldSwap(selection);

        ImGui::BeginDisabled(!selection.moved && !selection.hidden && selection.replacement.empty());
        if (ImGui::Button("Put it back the way it was found", ImVec2(-1.0F, 0.0F))) {
            Command command;
            command.kind = CommandKind::RestoreWorld;
            command.id   = selection.id;
            Queue(std::move(command));
        }
        ImGui::EndDisabled();
    }

    void MapEditor::RenderPlacedSettings(const SelectionInfo &selection) {
        // Taken from the selection again whenever nothing is being typed or
        // dragged, so an undo or an MCP edit shows through.
        if (_settingsDraftId != selection.id || (!ImGui::IsAnyItemActive() && _settingsDraft != selection.settings)) {
            _settingsDraft   = selection.settings.is_object() ? selection.settings : nlohmann::json::object();
            _settingsDraftId = selection.id;
            std::snprintf(_settingsName, sizeof(_settingsName), "%s", _settingsDraft.value("name", std::string()).c_str());
            std::snprintf(_settingsKey, sizeof(_settingsKey), "%s", _settingsDraft.value("keyItem", std::string()).c_str());
        }

        bool commit         = false;
        const auto deferred = [&commit] {
            // A text field or a slider commits once, when it is let go.
            commit = commit || ImGui::IsItemDeactivatedAfterEdit();
        };
        const auto optionalFraction = [&](const char *label, const char *key, const char *tooltip) {
            bool own = _settingsDraft.contains(key);
            if (ImGui::Checkbox(label, &own)) {
                if (own) {
                    _settingsDraft[key] = 1.0F;
                }
                else {
                    _settingsDraft.erase(key);
                }
                commit = true;
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
                ImGui::SetTooltip("%s", tooltip);
            }
            if (own) {
                float value = _settingsDraft.value(key, 1.0F);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(-1.0F);
                ImGui::PushID(key);
                if (ImGui::SliderFloat("##fraction", &value, 0.0F, 1.0F, "%.2f")) {
                    _settingsDraft[key] = value;
                }
                deferred();
                ImGui::PopID();
            }
        };

        ImGui::Separator();
        ImGui::SetNextItemWidth(-90.0F);
        if (ImGui::InputTextWithHint("Name", "what a script finds it by", _settingsName, sizeof(_settingsName))) {
            if (_settingsName[0] != '\0') {
                _settingsDraft["name"] = _settingsName;
            }
            else {
                _settingsDraft.erase("name");
            }
        }
        deferred();

        if (selection.placedKind == PlacedKind::Door) {
            bool locked = _settingsDraft.value("locked", false);
            if (ImGui::Checkbox("Locked", &locked)) {
                _settingsDraft["locked"] = locked;
                commit                   = true;
            }
            ImGui::SameLine();
            bool lockpickable = _settingsDraft.value("lockpickable", true);
            if (ImGui::Checkbox("Can be picked", &lockpickable)) {
                _settingsDraft["lockpickable"] = lockpickable;
                commit                         = true;
            }

            static const char *kLockLabels[] = {"No lock", "Bolt on one side", "Bolt and keyhole", "Keyhole on both sides"};
            static const char *kLockNames[]  = {"none", "bolt", "boltAndKey", "key"};
            const std::string lock           = _settingsDraft.value("lock", std::string("boltAndKey"));
            int lockIndex                    = 2;
            for (int index = 0; index < IM_ARRAYSIZE(kLockNames); ++index) {
                if (lock == kLockNames[index]) {
                    lockIndex = index;
                }
            }
            ImGui::SetNextItemWidth(-90.0F);
            if (ImGui::Combo("Lock", &lockIndex, kLockLabels, IM_ARRAYSIZE(kLockLabels))) {
                _settingsDraft["lock"] = kLockNames[lockIndex];
                commit                 = true;
            }
            optionalFraction("Own difficulty", "lockDifficulty", "Off: the leaf's own lockpicking difficulty. On: 0 is the easiest lock, 1 the hardest.");

            ImGui::SetNextItemWidth(-90.0F);
            if (ImGui::InputTextWithHint("Key", "item class GUID; empty for none", _settingsKey, sizeof(_settingsKey))) {
                if (_settingsKey[0] != '\0') {
                    _settingsDraft["keyItem"] = _settingsKey;
                }
                else {
                    _settingsDraft.erase("keyItem");
                }
            }
            deferred();
        }
        else if (selection.placedKind == PlacedKind::Item) {
            int amount = _settingsDraft.value("amount", 1);
            ImGui::SetNextItemWidth(120.0F);
            if (ImGui::InputInt("Amount", &amount, 0, 0)) {
                _settingsDraft["amount"] = std::clamp(amount, 1, 10000);
            }
            deferred();
            int quality = _settingsDraft.value("quality", 0);
            ImGui::SetNextItemWidth(120.0F);
            if (ImGui::InputInt("Quality", &quality, 0, 0)) {
                if (std::clamp(quality, 0, 255) != 0) {
                    _settingsDraft["quality"] = std::clamp(quality, 0, 255);
                }
                else {
                    _settingsDraft.erase("quality");
                }
            }
            deferred();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
                ImGui::SetTooltip("The game's grade, 0 for the class's own.");
            }
            optionalFraction("Own health", "health", "Off: the class's own. On: 0 is broken, 1 is whole.");
            optionalFraction("Own condition", "condition", "Off: the class's own. On: 0 is filthy, 1 is clean.");
        }
        else if (selection.placedKind == PlacedKind::SmartObject) {
            ImGui::TextDisabled("Sits, sleeps, gambles or crafts as the level's own does once a server loads the export.");
        }

        if (commit && _settingsDraft != selection.settings) {
            Command command;
            command.kind     = CommandKind::SetSettings;
            command.target   = SelectionKind::Placed;
            command.id       = selection.id;
            command.settings = _settingsDraft;
            Queue(std::move(command));
        }
    }

    void MapEditor::RenderWorldSwap(const SelectionInfo &selection) {
        ImGui::Spacing();
        if (!selection.replacement.empty()) {
            ImGui::TextWrapped("Swapped for %s", selection.replacement.c_str());
            if (ImGui::Button("Take the swap back", ImVec2(-1.0F, 0.0F))) {
                Command command;
                command.kind = CommandKind::ReplaceWorld;
                command.id   = selection.id;
                Queue(std::move(command));
            }
        }

        ImGui::SetNextItemWidth(-1.0F);
        ImGui::InputTextWithHint("##swap", selection.replacement.empty() ? "Swap for a mesh... (search the library)" : "Swap for another mesh...", _swapQuery, sizeof(_swapQuery));
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
            ImGui::SetTooltip("Takes it out of the world and stands the chosen mesh where it stood, collision and all:\na felled tree's stump, a broken cart, a burnt house. It moves with it, and it goes into the saved map.");
        }
        if (_swapQuery[0] == '\0') {
            return;
        }

        // A handful of meshes, in the asset library's own search order:
        // searched again only when the query changes, and never a door, a
        // smart object or an item, which no level object can stand as.
        constexpr std::size_t kMatches = 8;
        const auto &items              = _library.Items();
        if (_swapMatchedQuery != _swapQuery) {
            _swapMatchedQuery = _swapQuery;
            _swapMatches.clear();
            for (const int match : _library.Search(_swapQuery)) {
                std::string model;
                if (SplitWorldObjectKey(items[static_cast<std::size_t>(match)].path, model) == PlacedKind::Prop) {
                    _swapMatches.push_back(match);
                }
            }
        }
        const std::vector<int> &matches = _swapMatches;
        for (std::size_t index = 0; index < std::min(matches.size(), kMatches); ++index) {
            const AssetLibraryItem &item = items[static_cast<std::size_t>(matches[index])];
            ImGui::PushID(matches[index]);
            if (ImGui::Selectable(item.name.c_str())) {
                Command command;
                command.kind  = CommandKind::ReplaceWorld;
                command.id    = selection.id;
                command.model = item.path;
                Queue(std::move(command));
                _swapQuery[0] = '\0';
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
                ImGui::SetTooltip("%s", item.path.c_str());
            }
            ImGui::PopID();
        }
        if (matches.empty()) {
            ImGui::TextDisabled("No mesh in the library matches.");
        }
    }

    SelectionMode MapEditor::ModifierSelectionMode() {
        const ImGuiIO &io = ImGui::GetIO();
        return io.KeyShift ? SelectionMode::Remove : io.KeyCtrl ? SelectionMode::Add : SelectionMode::Replace;
    }

    SelectionFilter MapEditor::CurrentFilter() const {
        SelectionFilter filter;
        filter.kinds         = _filterKinds;
        filter.text          = _filterText;
        filter.mesh          = _filterMesh;
        filter.className     = _filterClass;
        filter.group         = _filterGroup;
        filter.layer         = _filterLayer;
        filter.includeHidden = _filterHidden;
        if (_filterPhysics >= 0) {
            filter.physics = static_cast<KCDC::Common::Spawn::PropPhysics>(_filterPhysics);
        }
        return filter;
    }

    void MapEditor::QueueSelection(SelectionQuery query, SelectionMode mode) {
        query.mode        = mode;
        query.filter      = CurrentFilter();
        query.visibleOnly = _visibleOnly;
        Command command;
        command.kind      = CommandKind::SelectRegion;
        command.selection = std::make_shared<const SelectionQuery>(std::move(query));
        Queue(std::move(command));
    }

    void MapEditor::QueueBrushSample(const ImVec2 &pixel) {
        SelectionQuery query;
        query.aimNdc        = ScreenToNdc(*ImGui::GetMainViewport(), pixel);
        query.region.radius = _brushRadius;
        QueueSelection(std::move(query), _brushMode);
        _lastBrushMouse = pixel;
    }

    bool MapEditor::HandleSelectionTools(const EditorSnapshot &snapshot, bool overWorld) {
        const ImGuiIO &io              = ImGui::GetIO();
        const ImGuiViewport *viewport  = ImGui::GetMainViewport();
        ImDrawList *draw               = ImGui::GetBackgroundDrawList();
        constexpr ImU32 kToolColour    = IM_COL32(120, 200, 255, 230);
        constexpr ImU32 kToolFill      = IM_COL32(120, 200, 255, 40);
        constexpr float kDragThreshold = 4.0F;
        constexpr int kMaxDabsPerFrame = 6;
        // Area handles and patrol points take the left button in their own tabs.
        const bool active = !_areas.IsTabVisible() && !_patrols.IsTabVisible();

        _brushAimWanted.store(active && _brushSelect && overWorld, std::memory_order_relaxed);
        if (!active) {
            _leftPressed    = false;
            _regionDragging = false;
            _brushPainting  = false;
            return false;
        }

        // The brush's circle lies on the surface under the cursor.
        if (_brushSelect) {
            float ndcX = 0.0F;
            float ndcY = 0.0F;
            CursorNdc(ndcX, ndcY);
            _brushNdcX.store(ndcX, std::memory_order_relaxed);
            _brushNdcY.store(ndcY, std::memory_order_relaxed);
            _brushScreenRadius = 0.0F;
            if (overWorld && snapshot.brushHit) {
                const glm::vec3 centre = KCDC::Common::ToGlm(snapshot.brushPoint);
                glm::vec3 normal       = KCDC::Common::ToGlm(snapshot.brushNormal);
                normal                 = glm::length(normal) > 0.01F ? glm::normalize(normal) : glm::vec3(0.0F, 0.0F, 1.0F);
                const glm::vec3 side   = glm::normalize(glm::abs(normal.z) < 0.9F ? glm::cross(normal, glm::vec3(0.0F, 0.0F, 1.0F)) : glm::cross(normal, glm::vec3(1.0F, 0.0F, 0.0F)));
                const glm::vec3 other  = glm::cross(normal, side);
                ImVec2 points[48];
                int visible = 0;
                for (int index = 0; index < 48; ++index) {
                    const float angle     = 6.2831853F * static_cast<float>(index) / 48.0F;
                    const glm::vec3 point = centre + (side * std::cos(angle) + other * std::sin(angle)) * _brushRadius;
                    float x               = 0.0F;
                    float y               = 0.0F;
                    if (Camera::ProjectToNdc(snapshot.camera, KCDC::Common::ToNative(point), x, y)) {
                        points[visible++] = NdcToScreen(*viewport, x, y);
                    }
                }
                if (visible == 48) {
                    draw->AddConvexPolyFilled(points, visible, kToolFill);
                    draw->AddPolyline(points, visible, kToolColour, ImDrawFlags_Closed, 2.0F);
                    // The circle seen at a slant is an ellipse; its narrower half spaces the dabs.
                    _brushScreenRadius = 0.5F * std::min(std::hypot(points[0].x - points[24].x, points[0].y - points[24].y), std::hypot(points[12].x - points[36].x, points[12].y - points[36].y));
                }
            }
        }

        if (overWorld && !io.KeyAlt && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            _leftPressed = true;
            _regionStart = io.MousePos;
            _regionOutline.assign(1, io.MousePos);
            return true;
        }
        if (!_leftPressed) {
            return false;
        }
        const bool held = ImGui::IsMouseDown(ImGuiMouseButton_Left);

        // Until it moves, a press may still be a click.
        if (!_regionDragging && !_brushPainting) {
            if (held && std::hypot(io.MousePos.x - _regionStart.x, io.MousePos.y - _regionStart.y) < kDragThreshold) {
                return true;
            }
            if (!held) {
                _leftPressed        = false;
                const glm::vec2 ndc = ScreenToNdc(*viewport, _regionStart);
                Command command;
                command.kind   = CommandKind::PickAt;
                command.ndcX   = ndc.x;
                command.ndcY   = ndc.y;
                command.enable = io.KeyCtrl;
                Queue(std::move(command));
                return true;
            }
            if (_brushSelect) {
                // A stroke adds to the selection, or takes out with Shift; Escape, or a click on nothing, starts over.
                _brushPainting = true;
                _brushMode     = ModifierSelectionMode() == SelectionMode::Remove ? SelectionMode::Remove : SelectionMode::Add;
                QueueBrushSample(_regionStart);
            }
            else {
                _regionDragging = true;
            }
        }

        if (_brushPainting) {
            if (!held) {
                _brushPainting = false;
                _leftPressed   = false;
                return true;
            }
            // Dabs half a circle apart along the cursor's path, so a fast stroke leaves no gaps.
            const float spacing  = _brushScreenRadius > 0.0F ? std::clamp(_brushScreenRadius * 0.5F, 4.0F, 64.0F) : 16.0F;
            const ImVec2 from    = _lastBrushMouse;
            const float distance = std::hypot(io.MousePos.x - from.x, io.MousePos.y - from.y);
            if (overWorld && distance >= spacing) {
                const int dabs = std::min(static_cast<int>(distance / spacing), kMaxDabsPerFrame);
                for (int dab = 1; dab <= dabs; ++dab) {
                    const float along = static_cast<float>(dab) / static_cast<float>(dabs);
                    QueueBrushSample(ImVec2(from.x + (io.MousePos.x - from.x) * along, from.y + (io.MousePos.y - from.y) * along));
                }
            }
            return true;
        }

        // A box or a lasso.
        if (_dragLasso && _regionOutline.size() < SelectionRegion::kMaxOutlinePoints) {
            const ImVec2 &last = _regionOutline.back();
            if ((io.MousePos.x - last.x) * (io.MousePos.x - last.x) + (io.MousePos.y - last.y) * (io.MousePos.y - last.y) >= 36.0F) {
                _regionOutline.push_back(io.MousePos);
            }
        }
        std::vector<ImVec2> outline = _regionOutline;
        if (!_dragLasso) {
            outline = {_regionStart, ImVec2(io.MousePos.x, _regionStart.y), io.MousePos, ImVec2(_regionStart.x, io.MousePos.y)};
        }
        else {
            outline.push_back(io.MousePos);
        }
        if (held) {
            if (outline.size() >= 3) {
                draw->AddPolyline(outline.data(), static_cast<int>(outline.size()), kToolColour, ImDrawFlags_Closed, 1.5F);
            }
            return true;
        }

        _regionDragging = false;
        _leftPressed    = false;
        SelectionQuery query;
        query.region.kind  = SelectionRegionKind::Screen;
        query.region.reach = _selectReach;
        for (const ImVec2 &pixel : outline) {
            query.region.outline.push_back(ScreenToNdc(*viewport, pixel));
        }
        if (query.region.outline.size() > SelectionRegion::kMaxOutlinePoints) {
            query.region.outline.resize(SelectionRegion::kMaxOutlinePoints);
        }
        QueueSelection(std::move(query), ModifierSelectionMode());
        return true;
    }

    void MapEditor::RenderSelectionSettings(const EditorSnapshot &snapshot) {
        ImGui::SeparatorText("Tools");
        ImGui::TextDisabled("A click picks one object. A drag selects by box or lasso (%s switches),", _keymap.Label(EditorAction::ToggleLasso).c_str());
        ImGui::TextDisabled("or paints with the brush while it is on (%s). Ctrl adds, Shift takes out;", _keymap.Label(EditorAction::ToggleBrush).c_str());
        ImGui::TextDisabled("a box or lasso otherwise replaces the selection, a brush stroke adds to it.");
        ImGui::SliderFloat("Brush radius", &_brushRadius, 0.25F, 50.0F, "%.2f m", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
            ImGui::SetTooltip("The brush takes what it passes over -- a tree by its crown -- and everything standing within this radius of the ground under it.");
        }
        ImGui::SliderFloat("Box and lasso reach", &_selectReach, 5.0F, SelectionRegion::kMaxReach, "%.0f m", ImGuiSliderFlags_AlwaysClamp);
        ImGui::Checkbox("Visible only", &_visibleOnly);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
            ImGui::SetTooltip("Skips objects behind a wall, under a roof or otherwise out of the camera's sight.");
        }

        ImGui::SeparatorText("Filter");
        ImGui::TextDisabled("Box, lasso, brush, Select similar and Select objects inside an area use it.");
        constexpr const char *kKindLabels[] {"Props", "Effects", "Lights", "Brushes", "Vegetation", "Entities"};
        for (std::size_t bit = 0; bit < std::size(kKindLabels); ++bit) {
            bool on = (_filterKinds & (1U << bit)) != 0;
            if (ImGui::Checkbox(kKindLabels[bit], &on)) {
                _filterKinds = static_cast<std::uint8_t>(on ? _filterKinds | (1U << bit) : _filterKinds & ~(1U << bit));
            }
            if (bit % 3 != 2) {
                ImGui::SameLine();
            }
        }
        ImGui::InputTextWithHint("Search", "words; -word leaves out", _filterText, sizeof(_filterText));
        const float fromWidth = ImGui::CalcTextSize("From selection").x + ImGui::GetStyle().FramePadding.x * 2.0F;
        ImGui::SetNextItemWidth(ImGui::CalcItemWidth() - fromWidth - ImGui::GetStyle().ItemInnerSpacing.x);
        ImGui::InputTextWithHint("##kcdc_filter_mesh", "exact mesh path", _filterMesh, sizeof(_filterMesh));
        ImGui::SameLine(0.0F, ImGui::GetStyle().ItemInnerSpacing.x);
        ImGui::BeginDisabled(snapshot.selection.mesh.empty());
        if (ImGui::Button("From selection##mesh")) {
            std::snprintf(_filterMesh, sizeof(_filterMesh), "%s", snapshot.selection.mesh.c_str());
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextUnformatted("Mesh");
        ImGui::SetNextItemWidth(ImGui::CalcItemWidth() - fromWidth - ImGui::GetStyle().ItemInnerSpacing.x);
        ImGui::InputTextWithHint("##kcdc_filter_class", "exact entity class", _filterClass, sizeof(_filterClass));
        ImGui::SameLine(0.0F, ImGui::GetStyle().ItemInnerSpacing.x);
        ImGui::BeginDisabled(snapshot.selection.className.empty());
        if (ImGui::Button("From selection##class")) {
            std::snprintf(_filterClass, sizeof(_filterClass), "%s", snapshot.selection.className.c_str());
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextUnformatted("Class");
        ImGui::InputTextWithHint("Group", "a placed group's name", _filterGroup, sizeof(_filterGroup));
        if (!_filterLayer.empty() && snapshot.layers->Find(_filterLayer) == nullptr) {
            _filterLayer.clear();
        }
        const EditorLayer *layer = _filterLayer.empty() ? nullptr : snapshot.layers->Find(_filterLayer);
        if (ImGui::BeginCombo("Layer", layer != nullptr ? layer->name.c_str() : "Any")) {
            if (ImGui::Selectable("Any", _filterLayer.empty())) {
                _filterLayer.clear();
            }
            for (const EditorLayer &entry : snapshot.layers->Layers()) {
                if (ImGui::Selectable(entry.name.c_str(), entry.id == _filterLayer)) {
                    _filterLayer = entry.id;
                }
            }
            ImGui::EndCombo();
        }
        constexpr const char *kPhysicsLabels[] {"Any", "Rigid", "Static", "None"};
        int physics = _filterPhysics + 1;
        if (ImGui::Combo("Physics", &physics, kPhysicsLabels, IM_ARRAYSIZE(kPhysicsLabels))) {
            _filterPhysics = physics - 1;
        }
        ImGui::Checkbox("Include objects taken out of the world", &_filterHidden);
        if (ImGui::Button("Clear filter")) {
            _filterKinds   = SelectableKinds::All;
            _filterText[0] = _filterMesh[0] = _filterClass[0] = _filterGroup[0] = '\0';
            _filterLayer.clear();
            _filterPhysics = -1;
            _filterHidden  = false;
        }
        ImGui::SameLine();
        if (ImGui::Button("Select all matching within reach")) {
            SelectionQuery query;
            query.region.centre = KCDC::Common::ToGlm(snapshot.camera.position);
            query.region.radius = std::min(_selectReach, SelectionRegion::kMaxRadius);
            QueueSelection(std::move(query), ModifierSelectionMode());
        }

        ImGui::SeparatorText("Select similar");
        ImGui::SliderFloat("Radius", &_similarRadius, 1.0F, SelectionRegion::kMaxRadius, "%.0f m", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
        ImGui::BeginDisabled(snapshot.selection.kind == SelectionKind::None);
        if (ImGui::Button("Select similar to the selected object")) {
            SelectionQuery query;
            query.similarTo     = SelectionRef {snapshot.selection.kind, snapshot.selection.id};
            query.region.radius = _similarRadius;
            QueueSelection(std::move(query), ModifierSelectionMode());
        }
        ImGui::EndDisabled();
        ImGui::TextDisabled("Same mesh -- an entity without one matches by class -- within the filter's layer, group, search words and physics.");

        ImGui::SeparatorText("Selected");
        std::size_t counts[5] {};
        for (const SelectionRef &ref : snapshot.selectedRefs) {
            ++counts[static_cast<std::size_t>(ref.kind)];
        }
        ImGui::Text("%zu selected: %zu props, %zu level objects, %zu effects, %zu lights", snapshot.selectedRefs.Size(), counts[static_cast<std::size_t>(SelectionKind::Placed)], counts[static_cast<std::size_t>(SelectionKind::World)],
            counts[static_cast<std::size_t>(SelectionKind::Effect)], counts[static_cast<std::size_t>(SelectionKind::Light)]);
        ImGui::TextDisabled("Delete takes them all out in one step; Ctrl+L puts them on the active layer.");
    }

    void MapEditor::RenderKeysTab() {
        ImGui::TextWrapped("Click a key to change it, then press the new key together with any Ctrl, Shift or Alt it needs. Escape, a click elsewhere or flying cancels. A key without Ctrl or Alt does nothing while you fly, so the fly keys stay free.");
        if (ImGui::Button("Default: 1 2 3")) {
            _keymap.ApplyPreset("default");
        }
        ImGui::SameLine();
        if (ImGui::Button("W E R")) {
            _keymap.ApplyPreset("wer");
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
            ImGui::SetTooltip("Move, rotate and scale on W, E and R; turning the mesh being placed moves to T.");
        }
        for (const auto &[first, second] : _keymap.Conflicts()) {
            ImGui::TextColored(kWarnColour, "%s and %s share %s, so that key does both.", EditorActions()[static_cast<std::size_t>(first)].label, EditorActions()[static_cast<std::size_t>(second)].label, _keymap.Label(first).c_str());
        }
        ImGui::TextDisabled("Not remappable: Escape, the mouse, F7, and Ctrl+Shift+Z for redo.");

        // Capturing: the first key pressed this frame, with the modifiers held, becomes the chord.
        // A click anywhere or flying gives up on it, so a fly key pressed later never rebinds the action.
        if (_capturingAction >= 0) {
            const bool clicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsMouseClicked(ImGuiMouseButton_Middle);
            if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || clicked || _looking.load(std::memory_order_acquire)) {
                _capturingAction = -1;
            }
            else {
                const ImGuiIO &io = ImGui::GetIO();
                for (int key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_NamedKey_END; ++key) {
                    if (IsChordKey(static_cast<ImGuiKey>(key)) && ImGui::IsKeyPressed(static_cast<ImGuiKey>(key), false)) {
                        _keymap.Set(static_cast<EditorAction>(_capturingAction), KeyChord {static_cast<ImGuiKey>(key), io.KeyCtrl, io.KeyShift, io.KeyAlt});
                        _capturingAction = -1;
                        _capturedFrame   = ImGui::GetFrameCount();
                        break;
                    }
                }
            }
        }

        if (ImGui::BeginTable("##kcdc_editor_keymap", 3, ImGuiTableFlags_RowBg)) {
            // A button stretched to its column has no width of its own, so the key column is sized for a long chord.
            const ImGuiStyle &style = ImGui::GetStyle();
            ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("Ctrl+Shift+Delete").x + style.FramePadding.x * 2.0F);
            ImGui::TableSetupColumn("Reset", ImGuiTableColumnFlags_WidthFixed);
            for (std::size_t index = 0; index < EditorActions().size(); ++index) {
                const EditorAction action = static_cast<EditorAction>(index);
                ImGui::PushID(static_cast<int>(index));
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(EditorActions()[index].label);
                ImGui::TableNextColumn();
                const bool capturing   = _capturingAction == static_cast<int>(index);
                const std::string text = capturing ? std::string {"press a key..."} : _keymap.Label(action);
                if (ImGui::Button(text.c_str(), ImVec2(-FLT_MIN, 0.0F))) {
                    _capturingAction = capturing ? -1 : static_cast<int>(index);
                }
                ImGui::TableNextColumn();
                if (ImGui::SmallButton("Clear")) {
                    _keymap.Set(action, KeyChord {});
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Default")) {
                    _keymap.Set(action, EditorActions()[index].standard);
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }

    EditorUserSettings MapEditor::CaptureUserSettings() const {
        EditorUserSettings settings;
        settings.keys          = _keymap;
        settings.operation     = _operation;
        settings.uniformScale  = _uniformScale;
        settings.showHelp      = _showHelp;
        settings.brushSelect   = _brushSelect;
        settings.dragLasso     = _dragLasso;
        settings.brushRadius   = _brushRadius;
        settings.selectReach   = _selectReach;
        settings.similarRadius = _similarRadius;
        settings.visibleOnly   = _visibleOnly;
        return settings;
    }

    void MapEditor::ApplyUserSettings(const EditorUserSettings &settings) {
        _keymap        = settings.keys;
        _operation     = settings.operation;
        _uniformScale  = settings.uniformScale;
        _showHelp      = settings.showHelp;
        _brushSelect   = settings.brushSelect;
        _dragLasso     = settings.dragLasso;
        _brushRadius   = settings.brushRadius;
        _selectReach   = settings.selectReach;
        _similarRadius = settings.similarRadius;
        _visibleOnly   = settings.visibleOnly;
    }

    void MapEditor::SyncUserSettings() {
        const std::string path = MapFiles::Directory() + "/editor.settings.json";
        if (!std::exchange(_userSettingsLoaded, true)) {
            std::string data;
            std::string error;
            EditorUserSettings settings;
            if (MapFiles::ReadPath(path, data, error)) {
                std::string warning;
                settings.FromJson(nlohmann::json::parse(data, nullptr, false), warning);
                if (!warning.empty()) {
                    _notice = "Editor settings: " + warning;
                }
            }
            ApplyUserSettings(settings);
            _savedUserSettings = settings.ToJson();
            return;
        }
        // Checked twice a second, written only when something changed.
        const auto now = std::chrono::steady_clock::now();
        if (now - _lastSettingsCheck < std::chrono::milliseconds(500)) {
            return;
        }
        _lastSettingsCheck           = now;
        const nlohmann::json current = CaptureUserSettings().ToJson();
        if (current == _savedUserSettings) {
            return;
        }
        std::string error;
        if (!MapFiles::WritePath(path, current.dump(2), error)) {
            _notice = "Could not save the editor settings: " + error;
        }
        // Remembered either way, so a failing disk is reported once rather than every half second.
        _savedUserSettings = current;
    }

    void MapEditor::RenderCameraTab(const EditorSnapshot &snapshot) {
        if (snapshot.mapTravel.value("status", "idle") != "idle") {
            ImGui::TextWrapped("%s", snapshot.mapTravel.value("message", "").c_str());
        }
        ImGui::Spacing();
        ImGui::Text("Position  %.2f, %.2f, %.2f", snapshot.camera.position.x, snapshot.camera.position.y, snapshot.camera.position.z);
        ImGui::Text("Facing    yaw %.1f, pitch %.1f", snapshot.camera.yawDegrees, snapshot.camera.pitchDegrees);
        ImGui::TextDisabled("Rendering at %.1f degrees, aspect %.3f", snapshot.camera.fovDegrees, snapshot.camera.aspectRatio);
        ImGui::Separator();

        float speed = snapshot.cameraSpeed;
        if (ImGui::SliderFloat("Speed", &speed, 0.5F, 200.0F, "%.1f m/s", ImGuiSliderFlags_Logarithmic)) {
            Command command;
            command.kind  = CommandKind::CameraSpeed;
            command.value = speed;
            Queue(std::move(command));
        }

        float fov = snapshot.cameraRequestedFov;
        if (ImGui::SliderFloat("Field of view", &fov, 20.0F, 140.0F, "%.0f degrees")) {
            Command command;
            command.kind  = CommandKind::CameraFov;
            command.value = fov;
            Queue(std::move(command));
        }

        ImGui::SeparatorText("Views");
        ImGui::SetNextItemWidth(-90.0F);
        ImGui::InputTextWithHint("##kcdc_editor_view_name", "name this view", _viewName, sizeof(_viewName));
        ImGui::SameLine();
        if (ImGui::Button("Remember", ImVec2(-1.0F, 0.0F))) {
            DocumentView view;
            view.name         = _viewName[0] != '\0' ? std::string {_viewName} : "View " + std::to_string(_views.size() + 1);
            view.position     = snapshot.camera.position;
            view.yawDegrees   = snapshot.camera.yawDegrees;
            view.pitchDegrees = snapshot.camera.pitchDegrees;
            _views.push_back(std::move(view));
            ++_viewsRevision;
            _viewName[0] = '\0';
        }

        if (ImGui::BeginChild("##kcdc_editor_views", ImVec2(0.0F, 110.0F), ImGuiChildFlags_Borders)) {
            if (_views.empty()) {
                ImGui::TextDisabled("Remember a view to come back to it. Views are saved with the map.");
            }
            for (std::size_t index = 0; index < _views.size(); ++index) {
                ImGui::PushID(static_cast<int>(index));
                if (ImGui::Selectable(_views[index].name.c_str(), false, ImGuiSelectableFlags_AllowOverlap, ImVec2(ImGui::GetContentRegionAvail().x - 24.0F, 0.0F))) {
                    Command command;
                    command.kind     = CommandKind::CameraView;
                    command.position = _views[index].position;
                    command.value    = _views[index].yawDegrees;
                    command.value2   = _views[index].pitchDegrees;
                    Queue(std::move(command));
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("x")) {
                    _views.erase(_views.begin() + static_cast<std::ptrdiff_t>(index));
                    ++_viewsRevision;
                    ImGui::PopID();
                    break;
                }
                ImGui::PopID();
            }
        }
        ImGui::EndChild();

        ImGui::SeparatorText("Player");
        ImGui::TextDisabled("The camera passes through everything; the player does not, and lands on the ground below it.");

        ImGui::BeginDisabled(!snapshot.hasPlayer);
        if (ImGui::Button("Go to the player", ImVec2(-1.0F, 0.0F))) {
            Command command;
            command.kind = CommandKind::CameraToPlayer;
            Queue(std::move(command));
        }
        if (ImGui::Button("Bring the player here", ImVec2(-1.0F, 0.0F))) {
            Command command;
            command.kind = CommandKind::PlayerToCamera;
            Queue(std::move(command));
        }
        ImGui::EndDisabled();

        if (snapshot.hasPlayer) {
            ImGui::TextDisabled("Player at %.1f, %.1f, %.1f", snapshot.playerPosition.x, snapshot.playerPosition.y, snapshot.playerPosition.z);
        }
        else {
            ImGui::TextDisabled("No local player in this level.");
        }
    }

    void MapEditor::SyncEditDraft(const EditorSnapshot &snapshot) {
        const SelectionInfo &selection = snapshot.selection;
        if (selection.kind == SelectionKind::None) {
            _editId = 0;
            return;
        }

        // The draft leads the snapshot during a drag and until its last move lands.
        const bool dragging = ImGuizmo::IsUsing() || ImGui::IsAnyItemActive();
        if (_editId == selection.id && (dragging || _groundSnap.Pending() || HasPendingTransform(selection.id))) {
            return;
        }

        _editId       = selection.id;
        _editPosition = selection.position;
        _editRotation = selection.rotation;
        _editScale    = selection.scale;
    }

    void MapEditor::RenderGizmo(const EditorSnapshot &snapshot) {
        _gizmoDrawn = false;
        if ((_areas.IsTabVisible() || _patrols.IsTabVisible()) && !snapshot.layers->Editable("area:default")) {
            FinishReleasedGizmoDrag(true);
            return;
        }
        if (!snapshot.cameraHeld) {
            return;
        }

        const ImGuiViewport *viewport = ImGui::GetMainViewport();

        float view[16];
        float projection[16];
        KCDC::Features::Camera::BuildViewMatrix(snapshot.camera, view);
        KCDC::Features::Camera::BuildProjectionMatrix(snapshot.camera, kGizmoNear, kGizmoFar, projection);

        ImGuizmo::SetRect(viewport->Pos.x, viewport->Pos.y, viewport->Size.x, viewport->Size.y);

        const auto ground = [&](std::string_view key, glm::vec3 &position, bool moved, float footOffset, bool request) {
            return _groundSnap.Update(
                key, position, moved, ImGuizmo::IsUsingAny(), ImGui::GetIO().KeyShift, footOffset, snapshot.groundResult,
                [&](std::uint64_t id, const glm::vec3 &point) {
                    Command query;
                    query.kind     = CommandKind::ProbeGround;
                    query.id       = id;
                    query.position = KCDC::Common::ToNative(point);
                    if (_areas.IsTabVisible()) {
                        query.groundIgnoreAreas = _areas.SelectedIds();
                    }
                    if (!_patrols.IsTabVisible() && !_areas.IsTabVisible()) {
                        query.groundIgnore = snapshot.extraSelection;
                        query.groundIgnore.push_back({snapshot.selection.kind, snapshot.selection.id});
                    }
                    Queue(std::move(query));
                },
                request);
        };
        if (_patrols.IsTabVisible()) {
            const float snap[3] {_viewport.snapTranslateStep, _viewport.snapTranslateStep, _viewport.snapTranslateStep};
            _gizmoDrawn   = _patrols.RenderGizmo(view, projection, _viewport.snapTranslate ? snap : nullptr, ground);
            _gizmoGesture = 0;
            return;
        }

        // While the Areas tab is open the gizmo belongs to the drawn area it has selected.
        const int areaOperation   = _areas.IsEditingHandle() || _areas.GroundPlacementRequested() || _groundSnap.Pending() ? 0 : _operation;
        const bool areaSnapping   = areaOperation == 1 ? _viewport.snapRotate : areaOperation == 2 ? _viewport.snapScale : _viewport.snapTranslate;
        const float areaSnapValue = areaOperation == 1 ? _viewport.snapRotateStep : areaOperation == 2 ? _viewport.snapScaleStep : _viewport.snapTranslateStep;
        const float areaSnapAxes[3] {areaSnapValue, areaSnapValue, areaSnapValue};
        if (_areas.IsTabVisible()) {
            _gizmoDrawn   = _areas.RenderGizmo(view, projection, _operation, _viewport.localSpace, areaSnapping ? areaSnapAxes : nullptr, ground, _groundSnap.Pending());
            _gizmoGesture = 0;
            return;
        }

        const SelectionInfo &selection = snapshot.selection;

        // No gizmo while a mesh is in hand: the click belongs to the brush.
        if (selection.kind == SelectionKind::None || selection.hidden || !selection.loaded || snapshot.armedModel != AssetLibrary::kNone) {
            _gizmoGesture = 0;
            return;
        }

        // An effect is sized through its tuning, not the gizmo.
        static const ImGuizmo::OPERATION kOperations[] {ImGuizmo::TRANSLATE, ImGuizmo::ROTATE, ImGuizmo::SCALE};
        const int tool                      = selection.kind == SelectionKind::Light || (selection.kind == SelectionKind::Effect && _operation == 2) ? 0 : _operation;
        const ImGuizmo::OPERATION operation = kOperations[std::clamp(tool, 0, 2)];

        // Scale is always local; ImGuizmo ignores WORLD for it.
        const ImGuizmo::MODE mode = _viewport.localSpace || operation == ImGuizmo::SCALE ? ImGuizmo::LOCAL : ImGuizmo::WORLD;

        float matrix[16];
        ComposeMatrix(_editPosition, _editRotation, _editScale, matrix);

        const bool snapping   = operation == ImGuizmo::ROTATE ? _viewport.snapRotate : operation == ImGuizmo::SCALE ? _viewport.snapScale : _viewport.snapTranslate;
        const float snapValue = operation == ImGuizmo::ROTATE ? _viewport.snapRotateStep : operation == ImGuizmo::SCALE ? _viewport.snapScaleStep : _viewport.snapTranslateStep;
        const float snapAxes[3] {snapValue, snapValue, snapValue};

        _gizmoDrawn = true;
        ImGuizmo::SetID(static_cast<int>(selection.id));
        const bool wasDragging = ImGuizmo::IsUsing();
        const bool manipulated = ImGuizmo::Manipulate(view, projection, operation, mode, matrix, nullptr, snapping ? snapAxes : nullptr);
        if (!wasDragging && ImGuizmo::IsUsing()) {
            _gizmoGesture = NextGesture();
        }

        if (!ImGuizmo::IsUsing() && !_groundSnap.Pending()) {
            _gizmoGesture = 0;
        }
        else if (_gizmoGesture == 0) {
            _gizmoGesture = NextGesture();
        }

        auto translated        = manipulated ? glm::vec3(matrix[12], matrix[13], matrix[14]) : KCDC::Common::ToGlm(_editPosition);
        const float footOffset = selection.kind != SelectionKind::Effect && selection.kind != SelectionKind::Light && selection.hasBounds ? selection.position.z - selection.worldBounds.min.z : 0;
        const bool grounded    = operation == ImGuizmo::TRANSLATE && ground("object:" + std::to_string(static_cast<int>(selection.kind)) + ":" + std::to_string(selection.id), translated, manipulated, footOffset, false);
        if (!manipulated && !grounded) {
            return;
        }
        if (operation == ImGuizmo::TRANSLATE) {
            matrix[12] = translated.x;
            matrix[13] = translated.y;
            matrix[14] = translated.z;
        }

        SDK::Vec3 position {};
        SDK::Quat rotation {0.0F, 0.0F, 0.0F, 1.0F};
        SDK::Vec3 scale {1.0F, 1.0F, 1.0F};
        DecomposeMatrix(matrix, position, rotation, scale);

        // Take back only the dragged component: round-tripping the others
        // through a decomposition drifts them (a long rotate shrinks the object).
        if (operation == ImGuizmo::TRANSLATE) {
            _editPosition = position;
        }
        else if (operation == ImGuizmo::ROTATE) {
            _editRotation = rotation;
        }
        else {
            if (_uniformScale) {
                // The dragged handle is the axis that moved most.
                const float rx = _editScale.x > 0.0F ? scale.x / _editScale.x : 1.0F;
                const float ry = _editScale.y > 0.0F ? scale.y / _editScale.y : 1.0F;
                const float rz = _editScale.z > 0.0F ? scale.z / _editScale.z : 1.0F;
                float ratio    = rx;
                if (std::fabs(ry - 1.0F) > std::fabs(ratio - 1.0F)) {
                    ratio = ry;
                }
                if (std::fabs(rz - 1.0F) > std::fabs(ratio - 1.0F)) {
                    ratio = rz;
                }
                scale = SDK::Vec3 {_editScale.x * ratio, _editScale.y * ratio, _editScale.z * ratio};
            }
            _editScale = ClampObjectScale(scale);
        }

        QueueTransform(selection, _gizmoGesture);
    }

    void MapEditor::RenderSelectionOutline(const EditorSnapshot &snapshot, std::vector<Gizmos::Drawing> &lines) {
        const SelectionInfo &selection = snapshot.selection;
        if (!snapshot.cameraHeld || selection.kind == SelectionKind::None || !selection.hasBounds || !selection.loaded) {
            return;
        }

        ImU32 colour = selection.kind == SelectionKind::World ? IM_COL32(120, 190, 255, 210) : IM_COL32(255, 196, 72, 200);
        if (selection.hidden) {
            colour = IM_COL32(230, 120, 90, 150);
        }
        BoxEdges(lines.emplace_back(EditorOutline(colour, 1.5F)), selection.worldBounds);

        // The rest of the group, fainter.
        for (const SDK::AABB &box : snapshot.extraBounds) {
            BoxEdges(lines.emplace_back(EditorOutline(IM_COL32(255, 220, 140, 120), 1.0F)), box);
        }
    }

    void MapEditor::RenderHoverReadout(const EditorSnapshot &snapshot) {
        const ImGuiViewport *viewport = ImGui::GetMainViewport();
        ImGuiIO &io                   = ImGui::GetIO();

        float ndcX = 0.0F;
        float ndcY = 0.0F;
        CursorNdc(ndcX, ndcY);
        _cursorNdcX.store(ndcX, std::memory_order_release);
        _cursorNdcY.store(ndcY, std::memory_order_release);

        const bool overWorld = snapshot.cameraHeld && !io.WantCaptureMouse && !(_gizmoDrawn && (ImGuizmo::IsOver() || ImGuizmo::IsUsing()));
        _cursorOverWorld.store(overWorld, std::memory_order_release);

        if (!overWorld || !snapshot.hover.valid || _looking.load(std::memory_order_acquire)) {
            return;
        }

        // Where the ray struck; off the cursor means the aim itself is wrong.
        float markX = 0.0F;
        float markY = 0.0F;
        if (KCDC::Features::Camera::ProjectToNdc(snapshot.camera, snapshot.hover.point, markX, markY)) {
            const ImVec2 mark = NdcToScreen(*viewport, markX, markY);
            ImDrawList *draw  = ImGui::GetBackgroundDrawList();
            draw->AddLine(ImVec2(mark.x - 6.0F, mark.y), ImVec2(mark.x + 6.0F, mark.y), IM_COL32(255, 255, 255, 150), 1.0F);
            draw->AddLine(ImVec2(mark.x, mark.y - 6.0F), ImVec2(mark.x, mark.y + 6.0F), IM_COL32(255, 255, 255, 150), 1.0F);
        }

        char text[288];
        std::snprintf(text, sizeof(text), "%s\n%s\n%.1f m away%s", snapshot.hover.label.c_str(), snapshot.hover.detail.c_str(), snapshot.hover.distance, snapshot.hover.selectable ? "" : "  --  cannot be selected");

        const ImVec2 size = ImGui::CalcTextSize(text);
        const ImVec2 corner {io.MousePos.x + 18.0F, io.MousePos.y + 18.0F};
        ImDrawList *overlay = ImGui::GetForegroundDrawList();
        overlay->AddRectFilled(ImVec2(corner.x - 6.0F, corner.y - 4.0F), ImVec2(corner.x + size.x + 6.0F, corner.y + size.y + 4.0F), IM_COL32(12, 12, 16, 220), 3.0F);
        overlay->AddText(corner, snapshot.hover.selectable ? IM_COL32(220, 230, 245, 255) : IM_COL32(160, 160, 170, 255), text);
    }

    void MapEditor::RenderBrushReadout(const EditorSnapshot &snapshot) {
        if (snapshot.armedModel == AssetLibrary::kNone || !_cursorOverWorld.load(std::memory_order_acquire) || _looking.load(std::memory_order_acquire)) {
            return;
        }

        const ImGuiIO &io = ImGui::GetIO();
        char text[224];
        const std::string_view name = ArmedName(snapshot);
        std::snprintf(text, sizeof(text), "%.*s\n%s  yaw %.0f  x%.2f", static_cast<int>(name.size()), name.data(), snapshot.armedOnGround ? "on the ground" : "in the air", snapshot.armedYaw, snapshot.armedScale);

        const ImVec2 size = ImGui::CalcTextSize(text);
        const ImVec2 corner {io.MousePos.x + 18.0F, io.MousePos.y + 18.0F};
        ImDrawList *overlay = ImGui::GetForegroundDrawList();
        overlay->AddRectFilled(ImVec2(corner.x - 6.0F, corner.y - 4.0F), ImVec2(corner.x + size.x + 6.0F, corner.y + size.y + 4.0F), IM_COL32(12, 20, 30, 220), 3.0F);
        overlay->AddText(corner, snapshot.armedOnGround ? IM_COL32(150, 215, 255, 255) : IM_COL32(255, 170, 140, 255), text);
    }

    void MapEditor::RenderViewportInput(const EditorSnapshot &snapshot) {
        ImGuiIO &io = ImGui::GetIO();

        // A key pressed for the Keys tab is that tab's alone.
        if (_capturingAction >= 0 || _capturedFrame == ImGui::GetFrameCount()) {
            return;
        }

        const bool armed = snapshot.armedModel != AssetLibrary::kNone;
        // Escape also works while flying or editing a field. RMB cancellation
        // belongs to DriveCamera, which knows whether the gesture rotated it.
        if (armed && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            Command command;
            command.kind = CommandKind::Disarm;
            Queue(std::move(command));
            _orbiting = false;
            _panning  = false;
            return;
        }

        // A chord with Ctrl or Alt works while flying too; a bare key is a fly key then.
        const bool flying   = _looking.load(std::memory_order_acquire);
        const auto shortcut = [&](EditorAction action) {
            const KeyChord &chord = _keymap.Chord(action);
            return (!flying || chord.ctrl || chord.alt) && _keymap.Pressed(action);
        };
        if (!io.WantTextInput) {
            if (shortcut(EditorAction::Save)) {
                SaveProject(snapshot, false);
            }
            if (shortcut(EditorAction::SaveAs)) {
                SaveProject(snapshot, true);
            }
            if (shortcut(EditorAction::CopyApi)) {
                CopySelection(snapshot);
            }
            Command command;
            if (shortcut(EditorAction::AssignLayer)) {
                command.kind        = CommandKind::Layer;
                command.layerAction = {{"action", "assign"}, {"layerId", snapshot.layers->Active()}};
                Queue(std::move(command));
                return;
            }
            if (shortcut(EditorAction::Redo) || (io.KeyCtrl && io.KeyShift && !io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_Z, false))) {
                command.kind = CommandKind::Redo;
                Queue(std::move(command));
                return;
            }
            else if (shortcut(EditorAction::Undo)) {
                command.kind = CommandKind::Undo;
                Queue(std::move(command));
                return;
            }
            else if (shortcut(EditorAction::Duplicate)) {
                if (_patrols.IsTabVisible() && snapshot.layers->Editable("patrol:default")) {
                    _patrols.DuplicateSelected();
                }
                else if (_areas.IsTabVisible() && snapshot.layers->Editable("area:default")) {
                    _areas.DuplicateSelected();
                }
                else if (snapshot.selection.kind != SelectionKind::None) {
                    command.kind = CommandKind::DuplicateSelection;
                    Queue(std::move(command));
                }
            }
        }

        // While flying the keys move and the wheel is the throttle.
        if (_looking.load(std::memory_order_acquire)) {
            _leftPressed    = false;
            _regionDragging = false;
            _brushPainting  = false;
            _brushAimWanted.store(false, std::memory_order_relaxed);
            if (io.MouseWheel != 0.0F) {
                Command command;
                command.kind  = CommandKind::CameraSpeed;
                command.value = snapshot.cameraSpeed * std::pow(1.25F, io.MouseWheel);
                Queue(std::move(command));
            }
            _orbiting = false;
            _panning  = false;
            return;
        }

        const bool gizmoBusy          = _gizmoDrawn && (ImGuizmo::IsOver() || ImGuizmo::IsUsing());
        const bool overWorld          = snapshot.cameraHeld && !io.WantCaptureMouse && !gizmoBusy;
        const ImGuiViewport *viewport = ImGui::GetMainViewport();
        const float viewportHeight    = std::max(viewport->Size.y, 1.0F);

        float ndcX = 0.0F;
        float ndcY = 0.0F;
        CursorNdc(ndcX, ndcY);

        // Wheel: turn or size the brush, otherwise dolly. Never resizes the selection.
        if (overWorld && io.MouseWheel != 0.0F) {
            Command command;
            if (armed && io.KeyCtrl) {
                command.kind  = CommandKind::ScaleArmed;
                command.value = std::pow(1.1F, io.MouseWheel);
            }
            else if (armed && io.KeyShift) {
                command.kind  = CommandKind::RotateArmed;
                command.value = (_viewport.snapRotate ? _viewport.snapRotateStep : 15.0F) * io.MouseWheel;
            }
            else {
                command.kind  = CommandKind::CameraDolly;
                command.ndcX  = ndcX;
                command.ndcY  = ndcY;
                command.value = io.MouseWheel;
            }
            Queue(std::move(command));
        }

        // Pan and orbit settle their pivot on press, on the game thread.
        if (overWorld && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
            _panning = true;
            Command command;
            command.kind = CommandKind::CameraGrab;
            command.ndcX = ndcX;
            command.ndcY = ndcY;
            Queue(std::move(command));
        }
        if (_panning) {
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
                _panning = false;
            }
            else if (io.MouseDelta.x != 0.0F || io.MouseDelta.y != 0.0F) {
                Command command;
                command.kind   = CommandKind::CameraPan;
                command.value  = io.MouseDelta.x / viewportHeight;
                command.value2 = io.MouseDelta.y / viewportHeight;
                Queue(std::move(command));
            }
        }

        if (overWorld && io.KeyAlt && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            _orbiting = true;
            Command command;
            command.kind   = CommandKind::CameraGrab;
            command.ndcX   = ndcX;
            command.ndcY   = ndcY;
            command.enable = true;
            Queue(std::move(command));
        }
        if (_orbiting) {
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                _orbiting = false;
            }
            else if (io.MouseDelta.x != 0.0F || io.MouseDelta.y != 0.0F) {
                Command command;
                command.kind   = CommandKind::CameraOrbit;
                command.value  = io.MouseDelta.x * kOrbitSensitivity;
                command.value2 = io.MouseDelta.y * kOrbitSensitivity;
                Queue(std::move(command));
            }
        }

        if (!io.WantTextInput) {
            if (_keymap.Pressed(EditorAction::Move)) {
                _operation = 0;
            }
            if (_keymap.Pressed(EditorAction::Rotate)) {
                _operation = 1;
            }
            if (_keymap.Pressed(EditorAction::Scale)) {
                _operation = 2;
            }
            if (_keymap.Pressed(EditorAction::ToggleBrush)) {
                _brushSelect = !_brushSelect;
            }
            if (_keymap.Pressed(EditorAction::ToggleLasso)) {
                _brushSelect = false;
                _dragLasso   = !_dragLasso;
            }
            if (_keymap.Pressed(EditorAction::ToggleAxes)) {
                _viewport.localSpace = !_viewport.localSpace;
            }
            if (_keymap.Pressed(EditorAction::ToggleSnap)) {
                bool &snapping = _patrols.IsTabVisible() || (_areas.IsTabVisible() && _areas.IsEditingHandle()) ? _viewport.snapTranslate : _operation == 1 ? _viewport.snapRotate : _operation == 2 ? _viewport.snapScale : _viewport.snapTranslate;
                snapping       = !snapping;
            }

            if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
                if (_patrols.IsTabVisible()) {
                    _patrols.ClearPoint();
                    return;
                }
                if (_areas.IsTabVisible() && _areas.IsEditingHandle()) {
                    _areas.ClearHandle();
                    return;
                }
                Command command;
                command.kind = CommandKind::Select;
                command.id   = 0;
                Queue(std::move(command));
                return;
            }

            if (armed && _keymap.Pressed(EditorAction::TurnArmed, true, true)) {
                Command command;
                command.kind  = CommandKind::RotateArmed;
                command.value = (_viewport.snapRotate ? _viewport.snapRotateStep : 15.0F) * (io.KeyShift ? -1.0F : 1.0F);
                Queue(std::move(command));
            }

            const SelectionInfo &selection = snapshot.selection;
            const bool deletePressed       = _keymap.Pressed(EditorAction::Delete);
            if (_areas.IsTabVisible() && snapshot.layers->Editable("area:default") && deletePressed) {
                _areas.DeleteSelected();
            }
            if (_patrols.IsTabVisible() && snapshot.layers->Editable("patrol:default") && deletePressed) {
                _patrols.DeleteSelected();
            }
            if (!_areas.IsTabVisible() && !_patrols.IsTabVisible() && selection.kind != SelectionKind::None) {
                if (_keymap.Pressed(EditorAction::SelectSimilar)) {
                    SelectionQuery query;
                    query.similarTo     = SelectionRef {selection.kind, selection.id};
                    query.region.radius = _similarRadius;
                    QueueSelection(std::move(query), ModifierSelectionMode());
                }
                if (deletePressed) {
                    // On a level object already out, Del puts it back.
                    Command command;
                    if (snapshot.extraSelection.empty() && selection.kind == SelectionKind::World && selection.hidden) {
                        command.kind   = CommandKind::HideWorld;
                        command.id     = selection.id;
                        command.enable = false;
                    }
                    else {
                        command.kind = CommandKind::DeleteSelection;
                    }
                    Queue(std::move(command));
                }
                if (_keymap.Pressed(EditorAction::Frame)) {
                    Command command;
                    command.kind = CommandKind::Focus;
                    Queue(std::move(command));
                }
                if (_keymap.Pressed(EditorAction::DropToGround)) {
                    Command command;
                    command.kind = CommandKind::DropToGround;
                    Queue(std::move(command));
                }
            }
        }

        if (armed) {
            _leftPressed    = false;
            _regionDragging = false;
            _brushPainting  = false;
            _brushAimWanted.store(false, std::memory_order_relaxed);
        }
        else if (HandleSelectionTools(snapshot, overWorld)) {
            return;
        }

        // A click on the world places the brush's mesh, or picks.
        if (!overWorld || io.KeyAlt || !ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            return;
        }

        // With the Areas tab open, a click on an area's marker selects the area rather than what is under it.
        if (snapshot.layers->Editable("patrol:default") && _patrols.Pick(io.MousePos, snapshot)) {
            return;
        }
        if (!armed && snapshot.layers->Editable("area:default") && _areas.Pick(io.MousePos)) {
            return;
        }

        Command command;
        if (armed) {
            command.kind = CommandKind::PlaceArmed;
            (snapshot.armedIsEffect ? _effectLibrary : _library).NoteUsed(snapshot.armedModel);
        }
        else {
            command.kind   = CommandKind::PickAt;
            command.ndcX   = ndcX;
            command.ndcY   = ndcY;
            command.enable = io.KeyCtrl;
        }
        Queue(std::move(command));
    }
} // namespace KCDC::Features::Prop
