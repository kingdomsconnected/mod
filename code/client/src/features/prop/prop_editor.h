#pragma once
#include "prop_editor_map_travel.h"

#include "features/area/area_collision_service.h"

#include "features/camera/anchor_camera.h"
#include "features/patrol/patrol_preview.h"
#include "prop_editor_areas.h"
#include "prop_editor_document.h"
#include "prop_editor_effects.h"
#include "prop_editor_environment.h"
#include "prop_editor_history.h"
#include "prop_editor_keymap.h"
#include "prop_editor_library.h"
#include "prop_editor_lights.h"
#include "prop_editor_mcp.h"
#include "prop_editor_objects.h"
#include "prop_editor_patrols.h"
#include "prop_editor_scene.h"
#include "prop_editor_selection.h"

#include "common/spawn/prop_spawner.h"
#include "sdk/types.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace KCDC::Features::Prop {
    /** The primary selection, flattened from whichever list holds it. */
    struct SelectionInfo {
        SelectionKind kind = SelectionKind::None;
        std::uint64_t id   = 0;
        std::string label;
        std::string detail;    // a mesh path, or what kind of world object it is
        std::string mesh;      // what a selection filter matches as its mesh; may be empty
        std::string className; // a level entity's class, or an effect's name

        SDK::Vec3 position {};
        SDK::Quat rotation {0.0F, 0.0F, 0.0F, 1.0F};
        SDK::Vec3 scale {1.0F, 1.0F, 1.0F};

        SDK::AABB worldBounds {};
        bool hasBounds = false;

        bool hidden = false;
        bool moved  = false;

        std::string replacement; // World only: the mesh standing in its place, or empty

        PlacedKind placedKind = PlacedKind::Prop; // Placed only: a mesh, or what the export builds
        std::string model;                        // Placed only: the mesh, door leaf, blueprint or item class
        nlohmann::json settings;                  // Placed only: a door's, smart object's or item's settings

        bool loaded           = true;  // World only: false while streamed out, the edit waiting for it
        bool layerOff         = false; // World only: on a level layer that is switched off
        bool onLayer          = false; // World only: on a layer the level switches at all
        std::uint16_t layerId = 0;

        KCDC::Common::Spawn::PropPhysics physics = KCDC::Common::Spawn::PropPhysics::Static;

        bool collisionRebuildFailed = false; // Placed only

        Shared::Game::LightTuning lightTuning;
        Shared::Game::VfxTuning tuning; // Effect only
        bool continuous = true;         // Effect only: whether it loops
    };

    /** Where a member of the selection stands, for grouped moves and framing. */
    struct RefPose {
        SDK::Vec3 position {};
        SDK::Quat rotation {0.0F, 0.0F, 0.0F, 1.0F};
        SDK::Vec3 scale {1.0F, 1.0F, 1.0F};
        SDK::AABB bounds {};
        bool hasBounds = false;
    };

    /** One adopted level object as the panel sees it. */
    struct WorldRow {
        std::uint64_t id = 0;
        std::string label;
        std::string kind;
        SDK::Vec3 position {};
        bool hidden   = false;
        bool moved    = false;
        bool swapped  = false; // another mesh stands in its place
        bool portable = false; // whether a saved map can name it again
        bool loaded   = true;  // false while streamed out
        bool layerOff = false; // hidden by the level's own layer switch
    };

    /** What the cursor rests on, refreshed a few times a second. */
    struct HoverInfo {
        bool valid      = false;
        bool selectable = false; // false for terrain and anything that cannot be held
        std::string label;
        std::string detail;
        SDK::Vec3 point {};
        float distance = 0.0F;
    };

    /** One placed object, copied off the game thread. */
    struct SceneRow {
        std::uint64_t id       = 0;
        SDK::EntityId entityId = 0;
        PlacedKind kind        = PlacedKind::Prop;
        nlohmann::json settings;
        std::string name;
        std::string model;
        SDK::Vec3 position {};
        SDK::Quat rotation {0.0F, 0.0F, 0.0F, 1.0F};
        SDK::Vec3 scale {1.0F, 1.0F, 1.0F};
        KCDC::Common::Spawn::PropPhysics physics = KCDC::Common::Spawn::PropPhysics::Static;
        std::string group;
        SDK::AABB worldBounds {};
        bool hasBounds = false;
    };

    /** One placed effect, copied off the game thread. */
    struct EffectRow {
        std::uint64_t id = 0;
        std::string label;
        std::string name;
        SDK::Vec3 position {};
        SDK::Quat rotation {0.0F, 0.0F, 0.0F, 1.0F};
        Shared::Game::VfxTuning tuning;
        bool playing = false;
        std::string group;
    };

    struct LightRow {
        std::uint64_t id = 0;
        SDK::Vec3 position {};
        Shared::Game::LightTuning tuning;
        bool playing = false;
        std::string group;
    };

    /** Placement brush settings; sent to the tick whenever the panel changes them. */
    struct PlacementOptions {
        KCDC::Common::Spawn::PropPhysics physics = KCDC::Common::Spawn::PropPhysics::Static;

        /** Tilts the mesh to the surface normal instead of keeping it upright. */
        bool alignToSurface = false;

        /** Snaps X/Y to a grid, then re-finds the ground height. */
        bool snapToGrid = false;
        float gridStep  = 1.0F;

        /** A fresh yaw per placement, for scattering. */
        bool randomYaw = false;

        /** A fresh uniform scale per placement, between the two. */
        bool randomScale = false;
        float scaleMin   = 0.85F;
        float scaleMax   = 1.15F;

        /** Metres along the surface normal. */
        float heightOffset = 0.0F;

        /** Metres ahead when the cursor points at nothing. */
        float fallbackDistance = 8.0F;

        /** What a placed effect starts with. */
        Shared::Game::VfxTuning effectTuning;

        /** Seconds between restarts of a one-shot effect, so it stays visible; 0 plays it once. */
        float oneShotRepeat = 3.0F;

        bool operator==(const PlacementOptions &other) const = default;
    };

    /** Everything the panel draws, published by the tick; the panel never reads the world live. */
    struct EditorSnapshot {
        std::uint64_t collisionContext = 0;
        std::map<std::uint64_t, std::string> collisionResults;
        std::string collisionError;
        // The serial of the last panel command the tick applied. The panel
        // compares it with what it queued before reading the scene as final.
        std::uint64_t completedCommand = 0;

        // The selection as a document, for blueprints and API copies.
        MapDocument selectedDocument;
        std::shared_ptr<const EditorLayers> layers = std::make_shared<const EditorLayers>();
        std::vector<DocumentWorldLayer> worldLayers;

        // What a loaded document named that this level or build does not
        // have; carried into the next save rather than lost. Shared: it only
        // changes on a load.
        std::shared_ptr<const MapDocument> unresolvedDocument;

        bool cameraHeld = false;
        nlohmann::json mapTravel = {{"active", false}, {"status", "idle"}};
        bool offline    = false;
        KCDC::Features::Camera::CameraPose camera {};
        float cameraSpeed        = 12.0F;
        float cameraRequestedFov = 60.0F;

        std::vector<SceneRow> rows;
        std::vector<EffectRow> effectRows;
        std::vector<LightRow> lightRows;
        std::size_t liveEmitters = 0; // everything VfxService is playing, not only the editor's
        std::vector<WorldRow> worldRows;
        std::vector<Shared::Game::LevelEditDefinition> worldDocument; // the level edits a save writes
        SelectionInfo selection;

        /** Selected besides `selection`, which the gizmo sits on; boxes for the outline. */
        std::vector<SelectionRef> extraSelection;
        std::vector<SDK::AABB> extraBounds;

        /** `selection` and `extraSelection` together, for a membership test per panel row. */
        SelectionSet selectedRefs;
        std::string status;

        HoverInfo hover;
        HoverInfo cameraGround;

        // The selection brush: the surface under the cursor, resolved every tick while the brush is the tool.
        bool brushHit = false;
        SDK::Vec3 brushPoint {};
        SDK::Vec3 brushNormal {0.0F, 0.0F, 1.0F};
        EditorGroundResult groundResult;
        bool patrolPreviewActive = false;
        nlohmann::json patrolProgress;
        std::string patrolPreviewStatus;
        std::vector<glm::vec3> patrolPath;

        // The placement brush.
        int armedModel     = AssetLibrary::kNone; // a mesh or effect row, per `armedIsEffect`
        bool armedIsEffect = false;
        bool armedOnGround = false;
        bool armedSized    = false;
        SDK::Vec3 armedSize {}; // at scale 1

        // The library's turntable preview.
        int previewModel     = AssetLibrary::kNone;
        bool previewIsEffect = false;
        bool previewSized    = false;
        SDK::Vec3 previewSize {};
        float armedYaw   = 0.0F;
        float armedScale = 1.0F;

        // The level browser's last listing, shared rather than copied every tick.
        std::shared_ptr<const std::vector<LevelRow>> levelRows = std::make_shared<const std::vector<LevelRow>>();
        bool levelTruncated                                    = false;

        /** The fly keys as the layout labels them, for the help panel. */
        std::string flyKeys;

        std::string undoLabel;
        std::string redoLabel;
        std::shared_ptr<const AuthoredHistoryState> authoredRestore;
        std::uint64_t authoredRestoreSerial = 0;

        bool hasPlayer = false;
        SDK::Vec3 playerPosition {};
        std::string level;
    };

    /**
     * The runtime world builder, opened with F7 or from script (`MapEditor`).
     *
     * `Update` runs on the game thread and owns the camera, scene, selection,
     * history and every engine query; `Render` runs on the render thread,
     * draws the published snapshot and queues commands back. The engine
     * systems are game-thread only and ImGui runs in the present hook, so the
     * two halves share nothing else.
     */
    class MapEditor final {
      public:
        void SetOpen(bool open);

        /** Opens the project at `path` once the panel next draws. */
        void RequestProject(const std::string &path);

        /** Starts an empty project on the loaded level once the panel next draws. */
        void RequestNewProject();

        /** The player asked to leave the offline World Builder, after any save prompt. Clears it. */
        bool ConsumeExitRequest() {
            return _exitRequested.exchange(false, std::memory_order_acq_rel);
        }

        bool IsOpen() const {
            return _open.load(std::memory_order_acquire);
        }

        /** Game thread; driven from CCryAction::PostUpdate. */
        void Update();
        void ObserveMapPointer(bool down, int x, int y) { _mapTravel.Pointer(down, x, y); }
        void ResetMapPointer() { _mapTravel.ClearPointer(); }

        /** Render thread; replayed inside the ImGui frame. */
        void Render();

        /** Hands the camera back and forgets the scene. Game thread. */
        void Shutdown();

        /** Drops every placed object and releases the camera. Game thread. */
        void ClearAndRelease();

        /** Level edge, off the game thread: only raises a flag for the tick. */
        void NativeContextChanged();

      private:
        friend class EditorMcp;
        friend class AuthoringMcp;
        friend class ProjectMcp;
        enum class CommandKind : std::uint8_t {
            Mcp,
            OpenNativeMap,
            CancelMapTravel,
            Layer,
            RecordAuthored,
            SyncAreaCollision,
            PlaceArmed, // the brush's mesh or effect where its preview stands
            Arm,
            Disarm,
            Preview,
            PatrolPreview,
            ProbeGround,
            SetEffectTuning,
            PlaceLight,
            SetLightTuning,
            DeleteSelection,
            DuplicateSelection,
            ConvertSelection, // hide the selected level objects and place editable copies where they stood
            ReplaceWorld,     // stand mesh `model` in level object `id`'s place; empty takes the swap back
            Placement,
            RotateArmed,
            ScaleArmed,
            Transform,
            SetPhysics,
            SetSettings, // a placed door's, smart object's or item's `settings`
            Rebuild,
            Duplicate,
            Remove,
            Undo,
            Redo,
            Clear,
            Select,
            PickAt,
            Focus,
            DropToGround,
            Import,
            HideWorld,
            RestoreWorld,
            RestoreAllWorld,
            Browse,
            SelectBrowsed,
            CameraSpeed,
            CameraFov,
            CameraPose,
            CameraView,
            CameraToPlayer,
            PlayerToCamera,
            CameraDolly,
            CameraGrab,
            CameraPan,
            CameraOrbit,
            ReplaceDocument, // clear everything, then import `document`, with no undo across it
            GroupSelection,  // name the selection's group `group`; empty ungroups
            SelectRegion,    // a selection tool: `selection` says where, what and how
        };

        /** One queued panel action; each kind reads the fields it needs. */
        struct Command {
            std::shared_ptr<McpJob> mcp;
            std::shared_ptr<const AuthoredHistoryState> authoredBefore;
            std::shared_ptr<const AuthoredHistoryState> authoredAfter;
            CommandKind kind            = CommandKind::PlaceArmed;
            std::uint64_t serial        = 0; // stamped by Queue; see EditorSnapshot::completedCommand
            std::uint64_t mcpGeneration = 0; // nonzero only for MCP-origin commands; game thread rejects stale sessions
            std::string group;
            std::string model; // ReplaceWorld: a prop catalog or streamed mesh path
            nlohmann::json settings; // SetSettings
            nlohmann::json layerAction;
            std::shared_ptr<const Shared::Game::PatrolRouteDefinition> patrolRoute;
            std::string previewRole;
            std::vector<SelectionRef> groundIgnore;
            std::vector<std::string> groundIgnoreAreas;

            // Which list `id` indexes.
            SelectionKind target = SelectionKind::Placed;
            std::uint64_t id     = 0;
            SDK::Vec3 position {};
            SDK::Quat rotation {0.0F, 0.0F, 0.0F, 1.0F};
            SDK::Vec3 scale {1.0F, 1.0F, 1.0F};
            SDK::Vec3 direction {0.0F, 1.0F, 0.0F}; // where a camera pose faces
            KCDC::Common::Spawn::PropPhysics physics = KCDC::Common::Spawn::PropPhysics::Static;

            // Screen point in NDC, -1..1.
            float ndcX = 0.0F;
            float ndcY = 0.0F;

            float value  = 0.0F; // a distance, a speed, an angle or a factor
            float value2 = 0.0F;
            bool enable  = false; // on/off, additive selection, or group transform
            bool effect  = false; // Arm and Preview: the row is an effect, not a mesh

            Shared::Game::VfxTuning tuning;
            Shared::Game::LightTuning lightTuning;

            /** Shared by every frame of one drag, making it one undo step. */
            std::uint32_t gesture = 0;

            // A level browser row; the node is only valid for its level.
            SDK::IRenderNode *node = nullptr;
            SDK::EntityId entityId = 0;
            WorldPickKind pickKind = WorldPickKind::None;

            PlacementOptions placement;
            std::shared_ptr<const MapDocument> document;
            std::shared_ptr<const SelectionQuery> selection;
        };

        // --- Game thread ---

        void DriveCamera(float deltaSeconds);
        void ReadFlyInput(KCDC::Features::Camera::FlyInput &fly) const;
        bool ReadLookDelta(float &yawDelta, float &pitchDelta) const;
        void DrainCommands();
        void Apply(const Command &command);

        /** The first solid surface through a screen point, or `fallbackDistance` ahead. */
        bool ResolveAim(float ndcX, float ndcY, float fallbackDistance, SDK::Vec3 &position, SDK::Vec3 &normal, bool &hitSurface) const;

        /** What a click along `heading` from the editor camera lands on: at most one of light, effect, placed and world is set. */
        struct PickResult {
            std::uint64_t light  = 0;
            std::uint64_t effect = 0;
            std::uint64_t placed = 0;
            WorldPick world;    // the level's own -- or terrain, or a solid nothing can select -- when it is nearer than the editor's
            SDK::Vec3 point {}; // where the ray meets what was picked
            int inspected = 0;  // level objects the octree weighed
        };
        PickResult PickThrough(const SDK::Vec3 &heading, PickEffort effort);

        /** Re-reads what the cursor rests on. Throttled; game thread. */
        void RefreshHover();

        /** The library preview: a mesh spinning, or an effect playing, in front of the camera. */
        void SetPreview(int index, bool effect);

        void UpdatePreview(float deltaSeconds);

        /** Additive toggles `ref` in or out of the group; otherwise replaces it. */
        void SelectRef(const SelectionRef &ref, bool additive);

        std::vector<SelectionRef> SelectionRefs() const;

        /** Empty when the object has gone. */
        std::optional<RefPose> DescribeRef(const SelectionRef &ref) const;

        /** Picks up a mesh or an effect row: a live preview follows the cursor. */
        void Arm(int index, bool effect);
        void Disarm();

        void UpdateBrush();
        void HideBrush();

        /** The placement tuning for an effect, with a repeat for one-shots and the brush's size. */
        Shared::Game::VfxTuning EffectTuningFor(int effectIndex, float scale) const;

        /** Rolls the random yaw and scale for the next placement. */
        void RollBrush();

        void PlaceArmed();

        /** Adds the placement of a scene object or an effect to `step`, for undo. */
        void AppendPlaced(HistoryStep &step, SelectionKind kind, std::uint64_t id) const;

        void ImportDocument(const MapDocument &document, std::shared_ptr<const AuthoredHistoryState> authoredBefore = {}, std::shared_ptr<const AuthoredHistoryState> authoredAfter = {});
        void ClearUnresolved();

        /** Copies a multi-selection as one block beside itself; each copied group becomes a new group. */
        void DuplicateTogether(const std::vector<SelectionRef> &selection, HistoryStep &step, std::vector<SelectionRef> &copies);

        /** The shipped catalog mesh a level object can be copied as, spelled as the catalog spells it; empty when the catalog has no such mesh. */
        static std::string CatalogModelOf(const WorldEdit &edit);

        /** Places a static prop of a level object's mesh at its pose plus `offset`, on its layer, and records it in `step`. 0 when its mesh is not in the catalog. */
        std::uint64_t CopyLevelObject(std::uint64_t worldId, const SDK::Vec3 &offset, HistoryStep &step);

        /** `stem`, or the first of `stem-2`, `stem-3`... not in `taken`. */
        static std::string UniqueGroup(const std::string &stem, const std::unordered_set<std::string> &taken);

        static std::string LayerKey(const SelectionRef &ref);
        bool CanEdit(const SelectionRef &ref) const;
        bool ApplyLayerAction(const nlohmann::json &action, std::string &error);
        void RenderLayersTab(const EditorSnapshot &snapshot);
        void UpdateLayerRendering();
        void PublishSnapshot();

        void BuildSelection(SelectionInfo &selection) const;

        void ClearSelection();

        /** Replaces the selection with `refs`, the first as the primary. */
        void SelectOnly(const std::vector<SelectionRef> &refs);

        /**
         * After undo or redo: selects what the step changed that is still
         * there and editable, so an object that is hard to click is in hand
         * again. Leaves the selection alone when none of it is.
         */
        void SelectTouched(const std::vector<SelectionRef> &touched);

        /** The primary and everything beside it, without copying them. */
        std::size_t SelectionSize() const {
            return (_selectedKind != SelectionKind::None ? 1 : 0) + _extra.Size();
        }

        /** Adds `ref`; false when it was already selected. */
        bool AddToSelection(const SelectionRef &ref);

        /** Takes `ref` out, handing the gizmo on if it was the primary; false when it was not selected. */
        bool RemoveFromSelection(const SelectionRef &ref);

        /**
         * Runs one selection tool against the editor's own objects and the
         * level, then joins the result to the selection as the query's mode
         * says. Level objects are adopted as they are selected. Game thread.
         */
        SelectionOutcome RunSelection(const SelectionQuery &query);

        /** What Select similar matches for `ref`: the same mesh, or for an entity without one the same class. False when there is neither. */
        bool SimilarFilter(const SelectionRef &ref, SelectionFilter &filter, glm::vec3 &position) const;

        /** Whether nothing solid stands between the camera and `point`. A hit inside the object's own box, when given, is the object itself. */
        bool IsInView(const glm::vec3 &point, const SDK::AABB *bounds) const;

        /**
         * Lets go of level objects a selection tool adopted that are out of
         * the selection with nothing changed. Run when the next region
         * selection starts, so an ID a tool handed out stays good until then.
         */
        void ReleaseUntouchedAdoptions();
        void SetStatus(std::string status);

        // --- Render thread ---

        std::shared_ptr<const AuthoredHistoryState> CaptureAuthoredHistory() const;
        std::array<std::uint64_t, 7> AuthoredHistoryRevision() const;
        void ResetAuthoredHistoryBaseline();
        void PublishAreaCollision(const EditorSnapshot &snapshot);
        bool RestoreAreaCollision();
        void SynchronizeAuthoredHistory(const EditorSnapshot &snapshot);
        bool RecordAuthoredChanges(const std::string &label = {}, bool finishGesture = false);

        void RenderToolbar(const EditorSnapshot &snapshot);
        void RenderHelp(const EditorSnapshot &snapshot, float toolbarHeight);
        void RenderSidebar(const EditorSnapshot &snapshot);
        void RenderLibraryTab(const EditorSnapshot &snapshot);
        void RenderLightsTab(const EditorSnapshot &snapshot);
        void RenderLightTuning(const EditorSnapshot &snapshot);
        void RenderLightMarkers(const EditorSnapshot &snapshot);
        void RenderEffectsTab(const EditorSnapshot &snapshot);
        void RenderEffectList(const EditorSnapshot &snapshot);
        void RenderEffectTuning(const EditorSnapshot &snapshot);
        void RenderEffectMarkers(const EditorSnapshot &snapshot);
        void RenderPlacementOptions();
        void RenderSceneTab(const EditorSnapshot &snapshot);
        void RenderLevelEdits(const EditorSnapshot &snapshot);
        void RenderNearbyLevelObjects(const EditorSnapshot &snapshot);
        void RenderFiles(const EditorSnapshot &snapshot);
        void RenderDirectoryPicker();
        void BrowseDirectory(const std::string &path);
        void RenderBlueprints(const EditorSnapshot &snapshot);

        /** Per frame: answers queued project requests, the leave prompt, and the throttled unsaved check and autosave. */
        void UpdateProject(const EditorSnapshot &snapshot);
        void UpdateProjectBaseline(const EditorSnapshot &snapshot);

        /** Whether every command the panel queued has been applied, so the snapshot shows the scene as it now is. */
        bool IsSettled(const EditorSnapshot &snapshot) const {
            return _baselineAfter == 0 && _authoredHistoryPendingRestore == 0 && !_groundSnap.Pending() && snapshot.completedCommand >= _nextCommand.load(std::memory_order_acquire);
        }

        /** Compared now, on this thread: for the prompts, which must not act on a stale answer. */
        bool IsProjectDirty(const EditorSnapshot &snapshot) const;
        void MarkProjectSaved(nlohmann::json project);
        void RememberProject(const std::string &path);
        bool SaveProject(const EditorSnapshot &snapshot, bool saveAs);
        void OpenProject(const std::string &path, const EditorSnapshot &snapshot);
        void NewProject(const EditorSnapshot &snapshot);
        void ReplaceProject(const MapDocument &document);
        MapDocument SelectedDocument(const EditorSnapshot &snapshot) const;
        void CopySelection(const EditorSnapshot &snapshot);
        void RenderLevelTab(const EditorSnapshot &snapshot);
        void RenderObjectTab(const EditorSnapshot &snapshot);

        /** The Object tab's swap for a level object: search the asset library for a mesh to stand in its place, or give it back. */
        void RenderWorldSwap(const SelectionInfo &selection);

        /** A placed door's lock, a smart object's name, an item's stack: what the export carries for it. */
        void RenderPlacedSettings(const SelectionInfo &selection);

        /** The toolbar's Selection popup: brush size, reach, the shared filter, Select similar and a count of what is selected. */
        void RenderSelectionSettings(const EditorSnapshot &snapshot);

        /** The Keys tab: every remappable action, presets, conflicts, and capturing a new chord. */
        void RenderKeysTab();

        /** Loads `editor.settings.json` on the first frame, then writes it when the keys or tool settings change. */
        void SyncUserSettings();
        EditorUserSettings CaptureUserSettings() const;
        void ApplyUserSettings(const EditorUserSettings &settings);

        /** The filter the Selection popup's fields describe. */
        SelectionFilter CurrentFilter() const;

        /** Queues one selection tool's query with the shared filter and Visible only. */
        void QueueSelection(SelectionQuery query, SelectionMode mode);

        /** The mode the held modifiers ask for: Ctrl adds, Shift takes out, otherwise the result replaces the selection. */
        static SelectionMode ModifierSelectionMode();

        /** Queues one brush dab at a screen pixel. */
        void QueueBrushSample(const ImVec2 &pixel);

        /**
         * The left button in the viewport: a click picks, a drag selects by
         * box or lasso, or paints with the brush when it is on. Draws what the
         * drag covers. True when it took the button.
         */
        bool HandleSelectionTools(const EditorSnapshot &snapshot, bool overWorld);
        void RenderCameraTab(const EditorSnapshot &snapshot);
        void RenderGizmo(const EditorSnapshot &snapshot);
        void RenderSelectionOutline(const EditorSnapshot &snapshot, std::vector<Gizmos::Drawing> &lines);
        void RenderViewportInput(const EditorSnapshot &snapshot);
        void RenderHoverReadout(const EditorSnapshot &snapshot);
        void RenderBrushReadout(const EditorSnapshot &snapshot);

        /** Reseeds the transform drafts, unless a drag of this object is in flight. */
        void SyncEditDraft(const EditorSnapshot &snapshot);

        void QueueTransform(const SelectionInfo &selection, std::uint32_t gesture);

        std::uint32_t NextGesture();

        void QueueLook(const EditorSnapshot &snapshot, const SDK::Vec3 &direction);

        MapDocument BuildDocument(const EditorSnapshot &snapshot) const;

        /** Parses and queues a map, replacing the scene first if asked. */

        /** Loads a saved map by name, or says why not. */

        void Queue(Command command);

        /** Whether a move of `id` is still waiting for the game thread. */
        bool HasPendingTransform(std::uint64_t id) const;

        std::shared_ptr<const EditorSnapshot> ReadSnapshot() const;

        // --- Shared ---

        EditorMcp _mcp;

        std::atomic_bool _open           = false;
        std::atomic_bool _closeRequested = false;
        std::atomic_bool _exitRequested  = false;

        // Only the render thread knows whether the pointer is over a panel.
        std::atomic_bool _pointerOverUi    = false;
        std::atomic_bool _keyboardCaptured = false;

        // Flying: the tick sets it, the panel hides the cursor.
        std::atomic_bool _looking = false;
        bool _rightMouseHeld      = false; // Game thread; tracks a focused RMB gesture.
        bool _lookRotated         = false; // Sticky until release, including rotation back to the original view.

        std::atomic_bool _levelChanged = false;

        // The cursor in NDC, written by the panel, traced by the tick.
        std::atomic<float> _cursorNdcX     = 0.0F;
        std::atomic<float> _cursorNdcY     = 0.0F;
        std::atomic_bool _cursorOverWorld  = false;
        std::atomic_bool _patrolTabVisible = false;

        mutable std::mutex _commandMutex;
        std::vector<Command> _commands;

        // Asked for from the game thread (the menu), answered by the panel.
        std::string _requestedProject;
        bool _requestedNewProject = false;

        // Swapped whole by the tick, so the panel never copies it.
        mutable std::mutex _snapshotMutex;
        std::shared_ptr<const EditorSnapshot> _snapshot = std::make_shared<const EditorSnapshot>();

        // --- Game thread only ---

        KCDC::Features::Camera::AnchorCamera _camera;
        EditorMapTravel _mapTravel;
        EditorLayers _layers;
        std::shared_ptr<const EditorLayers> _layersPublished = std::make_shared<const EditorLayers>();
        EditorScene _scene;
        KCDC::Features::Area::AreaCollisionService _areaCollision;
        std::atomic<std::uint64_t> _collisionContext {1};
        std::map<std::uint64_t, std::string> _collisionResults;
        std::string _collisionError;
        Patrol::PatrolPreview _patrolPreview;
        MapDocument _unresolvedDocument;
        std::shared_ptr<const MapDocument> _unresolvedPublished;
        EditorEffects _effects;
        EditorLights _lights;
        WorldObjects _world;
        EditorHistory _history;
        std::uint64_t _completedCommand = 0;

        // The primary selection; the gizmo sits on it.
        SelectionKind _selectedKind = SelectionKind::None;
        std::uint64_t _selected     = 0;

        // Ctrl+clicked or region-selected beside it; they follow the primary.
        SelectionSet _extra;

        // Level objects a selection tool adopted that nothing has changed yet.
        // Thousands can pass through a brush stroke; the next region
        // selection lets go of those that left the selection untouched, so
        // the edit list stays the objects the user actually edited.
        std::unordered_set<std::uint64_t> _transientAdoptions;

        // The library turntable.
        int _previewModel             = AssetLibrary::kNone;
        bool _previewIsEffect         = false;
        std::uint32_t _previewGhost   = 0;
        std::uint32_t _previewEmitter = 0;
        float _previewYaw             = 0.0F;
        bool _previewSized            = false;
        SDK::Vec3 _previewSize {};
        SDK::Vec3 _previewCentre {}; // box centre relative to the mesh origin
        std::string _status = "Ready";
        std::chrono::steady_clock::time_point _lastTick {};

        HoverInfo _hover;
        EditorGroundResult _groundResult; // game thread
        EditorGroundSnap _groundSnap;     // render thread
        std::chrono::steady_clock::time_point _lastHover {};

        // The placement brush, and the pose a click places at.
        int _armedModel             = AssetLibrary::kNone;
        bool _armedIsEffect         = false;
        bool _armedFixedScale       = false; // a door, smart object or item: built at its own size
        std::uint32_t _armedGhost   = 0;
        std::uint32_t _armedEmitter = 0;
        PlacementOptions _placement;
        float _armedYaw   = 0.0F; // degrees
        float _armedScale = 1.0F;
        SDK::Vec3 _armedPosition {};
        SDK::Quat _armedRotation {0.0F, 0.0F, 0.0F, 1.0F};
        bool _armedOnGround = false;
        bool _armedShown    = false;
        bool _armedSized    = false;
        SDK::Vec3 _armedSize {};
        std::mt19937 _random {0x6B636463U};

        // Orbit pivot and pan depth, settled when the drag begins.
        SDK::Vec3 _grabPivot {};
        float _grabDistance = 10.0F;

        std::shared_ptr<const std::vector<LevelRow>> _levelRows = std::make_shared<const std::vector<LevelRow>>();
        bool _levelTruncated                                    = false;

        // Whether the editor holds the controls lock and the HUD suppression.
        bool _controlsHeld = false;

        // --- Render thread only ---
        std::uint64_t _areaCollisionRevision = 0;
        std::uint64_t _areaCollisionContext  = 0;

        float _sidebarWidth = 400.0F;
        char _layerName[129] {};
        std::string _deleteLayer;

        AssetLibrary _library {AssetLibraryCatalog::Props()};

        // The level's areas and the ones drawn for scripts; data only, so it lives here with the panel.
        EditorAreas _areas;
        EditorPatrols _patrols;
        AssetLibrary _effectLibrary {AssetLibraryCatalog::Effects()};

        // Turntable debounce, so sweeping down the list builds no mesh per row.
        int _frameHover                      = AssetLibrary::kNone; // the library row under the mouse this frame
        bool _frameHoverEffect               = false;
        int _previewSent                     = AssetLibrary::kNone;
        bool _mcpPreviewPinned               = false;
        std::uint64_t _mcpDispatchGeneration = 0; // render-owned scope around one MCP adapter dispatch
        bool _previewSentEffect              = false;
        int _previewHover                    = AssetLibrary::kNone;
        bool _previewHoverEffect             = false;
        std::chrono::steady_clock::time_point _previewHoverSince {};

        // The last save/load/copy message; the status line belongs to the tick.
        std::string _notice;

        PlacementOptions _placementDraft;
        PlacementOptions _placementSent;
        bool _placementEverSent = false;

        int _operation     = 0; // 0 move, 1 rotate, 2 scale

        // Selection tools, render thread. The fields are the Selection popup's; the filter is built from them per query.
        bool _brushSelect = false; // a drag paints with the brush rather than drawing a box or lasso
        bool _dragLasso   = false; // a drag draws a lasso rather than a box

        // Key bindings and the settings file they are saved in. Render thread.
        EditorKeymap _keymap;
        int _capturingAction     = -1; // the EditorAction waiting for its new chord, or -1
        int _capturedFrame       = -1; // the frame a chord was captured on, whose key press is the Keys tab's alone
        bool _userSettingsLoaded = false;
        nlohmann::json _savedUserSettings;
        std::chrono::steady_clock::time_point _lastSettingsCheck {};
        std::uint8_t _filterKinds = SelectableKinds::All;
        char _filterText[128]     = {};
        // The settings being edited in the Object tab, synced from the selection whenever no field is held.
        nlohmann::json _settingsDraft;
        std::uint64_t _settingsDraftId = 0;
        char _settingsName[65]         = {};
        char _settingsKey[37]          = {};

        std::string _swapMatchedQuery;
        std::vector<int> _swapMatches;
        char _swapQuery[128]      = {}; // the Object tab's search for a mesh to swap a level object for
        char _filterMesh[256]     = {};
        char _filterClass[96]     = {};
        char _filterGroup[96]     = {};
        std::string _filterLayer;
        int _filterPhysics   = -1; // -1 any, else a KCDC::Common::Spawn::PropPhysics
        bool _filterHidden   = false;
        bool _visibleOnly    = false;
        float _brushRadius   = 3.0F;
        float _selectReach   = SelectionRegion::kDefaultReach;
        float _similarRadius = 50.0F;
        bool _leftPressed    = false; // the left button went down over the world and is still held
        bool _regionDragging = false; // ... and has moved far enough to be a box or lasso
        ImVec2 _regionStart {};
        std::vector<ImVec2> _regionOutline; // screen pixels while a rectangle or lasso is dragged
        bool _brushPainting      = false;   // ... or a brush stroke
        SelectionMode _brushMode = SelectionMode::Add;
        ImVec2 _lastBrushMouse {};       // where the stroke last dabbed
        float _brushScreenRadius = 0.0F; // the circle's radius on screen when it was last drawn, in pixels
        // Where the render thread wants the brush aimed; the tick resolves it into the snapshot.
        std::atomic<bool> _brushAimWanted {false};
        std::atomic<float> _brushNdcX {0.0F};
        std::atomic<float> _brushNdcY {0.0F};
        bool _uniformScale = true;
        EditorViewport _viewport;

        // The conditions the project is previewed under: owned here, with
        // the engine reconciled against it once a frame.
        EditorEnvironment _environment;

        bool _showHelp = true;

        char _sceneFilter[64]        = {};
        char _levelFilter[64]        = {};
        char _levelEditFilter[64]    = {};
        bool _includeMovedLevelEdits = false;
        float _levelRadius           = 40.0F;
        bool _levelEntities          = true;
        bool _levelGeometry          = true;

        // The serial the next queued command takes; written here, read by IsSettled.
        std::atomic<std::uint64_t> _nextCommand = 0;

        // The project: where it lives, and what it looked like when last
        // saved or opened. A null baseline is a project never saved, which is
        // always unsaved -- a recovered autosave, for one.
        char _mapName[64] = "untitled";
        char _exportDirectory[1024] {};
        char _directoryInput[1024] {};
        std::string _browsedDirectory;
        std::vector<std::string> _directoryEntries;
        std::string _directoryError;
        std::string _projectPath;
        std::shared_ptr<const nlohmann::json> _savedProject;
        std::uint64_t _projectGeneration = 0; // bumped on every new baseline, so a check started before it is ignored
        std::uint64_t _baselineAfter     = 0; // nonzero while a replaced project waits for the tick to apply it
        bool _projectInitialized         = false;
        bool _projectDirty               = false;
        bool _recoveredProject           = false;
        std::vector<std::string> _recentProjects;

        // The unsaved check and autosave run off this thread, at most one at a time.
        struct ProjectCheck {
            std::uint64_t generation = 0;
            bool dirty               = false;
            bool autosaved           = false;
            std::string error;
        };
        std::future<ProjectCheck> _projectCheck;
        std::chrono::steady_clock::time_point _lastProjectCheck {};
        std::chrono::steady_clock::time_point _lastAutosave {};
        bool _recoveryExists = false;

        // Actions waiting on the unsaved-changes prompt.
        std::string _pendingOpen;
        bool _pendingNew  = false;
        bool _pendingExit = false;

        char _groupName[96] = "Blueprint";
        std::string _blueprintPath;

        // The tuning inspector's draft, committed when a field is released:
        // every change restarts the emitter.
        Shared::Game::LightTuning _lightDraft;
        std::uint64_t _lightDraftId = 0;
        Shared::Game::VfxTuning _tuningDraft;
        std::uint64_t _tuningDraftId = 0;

        std::shared_ptr<const AuthoredHistoryState> _authoredHistoryBaseline;
        nlohmann::json _authoredHistoryContent;
        std::array<std::uint64_t, 7> _authoredHistoryRevision {};
        bool _authoredHistoryBaselineValid           = false;
        std::uint64_t _authoredHistoryRestoreSeen    = 0;
        std::uint64_t _authoredHistoryPendingRestore = 0;

        std::vector<DocumentView> _views;
        std::uint64_t _viewsRevision = 0;
        char _viewName[48]           = {};

        // The transform draft, held so a drag is not reset by a stale snapshot.
        std::uint64_t _editId = 0;
        SDK::Vec3 _editPosition {};
        SDK::Quat _editRotation {0.0F, 0.0F, 0.0F, 1.0F};
        SDK::Vec3 _editScale {1.0F, 1.0F, 1.0F};

        std::uint32_t _gestureCounter = 0;
        std::uint32_t _gizmoGesture   = 0; // non-zero while a gizmo drag is on
        std::uint32_t _fieldGesture   = 0; // the inspector field being dragged

        bool _orbiting = false;
        bool _panning  = false;

        // ImGuizmo::IsOver answers for the last gizmo drawn, even when none is now.
        bool _gizmoDrawn = false;

        // Whether the native overlay holds this editor's lines, so closing clears them once.
        bool _overlayPublished = false;
    };
} // namespace KCDC::Features::Prop
