#pragma once

#include "features/camera/anchor_camera.h"
#include "features/gizmos/gizmos_geometry.h"
#include "prop_editor_area_geometry.h"
#include "prop_editor_ground_snap.h"
#include "prop_editor_viewport.h"
#include "shared/features/area/area_definition.h"
#include "shared/game/door_catalog.h"

#include <imgui.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace KCDC::Features::Prop {
    struct EditorSnapshot;
    struct MapDocument;

    /**
     * The World Builder's areas: the level's own gameplay areas, shown and
     * inspected, and the areas it draws for scripts.
     *
     * Render thread only. An area is data -- the level's come from the
     * compiled area catalog, the drawn ones are `Area.create` definitions --
     * so nothing here touches the engine, and nothing has to cross to the
     * game thread. The server tests them; the editor only shapes them.
     */
    class EditorAreas final {
      public:
        /** Called once per frame before any drawing. */
        void BeginFrame();

        void RenderTab(const EditorSnapshot &snapshot, std::string &notice, const EditorViewport &viewport = {});

        /** Outlines into `lines` for the native renderer, and the clickable markers `Pick` answers from. */
        void RenderOverlay(const EditorSnapshot &snapshot, std::vector<Gizmos::Drawing> &lines);

        /**
         * The gizmo on the selected drawn area, while the Areas tab is open.
         * True when a gizmo was drawn and should own mouse input over its arrows.
         */
        bool RenderGizmo(const float view[16], const float projection[16], int operation, bool localSpace, const float *snap, GroundSnapFunction ground = {}, bool groundPending = false);

        /** Selects an area, corner, edge or size handle from a viewport click. */
        bool Pick(const ImVec2 &mouse);

        bool IsEditingHandle() const {
            return _handle.kind != AreaHandle::Kind::None;
        }
        bool GroundPlacementRequested() const {
            return _groundPlacementRequested;
        }
        void ClearHandle();

        const std::unordered_map<std::string, std::string> &Groups() const {
            return _groups;
        }

        void DuplicateSelected();
        void DeleteSelected();
        MapDocument SelectedDocument() const;

        /** Whether the Areas tab was drawn this frame, which gives areas the viewport's clicks and gizmo. */
        bool IsTabVisible() const {
            return _tabVisible;
        }

        const std::vector<Shared::Game::AreaDefinition> &Authored() const {
            return _authored;
        }
        std::uint64_t ContentRevision() const {
            return _contentRevision;
        }
        std::uint64_t SelectionRevision() const {
            return _selectionRevision;
        }

        /** Takes a map's areas; `replace` drops the ones already drawn first. */
        void Import(const std::vector<Shared::Game::AreaDefinition> &areas, bool replace, const std::unordered_map<std::string, std::string> &groups = {});

        /** Stable-ID authoring API. All calls stay on the render thread. Failed mutations leave state unchanged. */
        static bool ValidateDefinition(const Shared::Game::AreaDefinition &area, std::string &error);
        const Shared::Game::AreaDefinition *Find(const std::string &id) const;
        bool Create(const Shared::Game::AreaDefinition &area, std::string &error);
        bool Update(const std::string &id, const Shared::Game::AreaDefinition &area, std::string &error);
        bool Remove(const std::string &id, std::string &error);
        bool Duplicate(const std::string &id, std::string &createdId, std::string &error);
        bool SetGroup(const std::vector<std::string> &ids, const std::string &group, std::string &error);
        bool Select(const std::vector<std::string> &ids, std::string &error);
        std::vector<std::string> SelectedIds() const;
        /** Restores a complete document atomically, without import's collision renaming. */
        bool ReplaceState(const std::vector<Shared::Game::AreaDefinition> &areas, const std::unordered_map<std::string, std::string> &groups, const std::vector<std::string> &selectedIds, std::string &error);

      private:
        const Shared::Game::AreaDefinition *Selected() const {
            return _selectedAreas.empty() ? nullptr : &_authored[_selectedAreas.back()];
        }

        /** One `crime_door` link of the selected level area, resolved once when it is selected. */
        struct LinkedDoor {
            const Shared::Game::DoorPlacement *placement = nullptr; // null for a door the catalog does not place
            Shared::Game::AreaDoorLink link              = Shared::Game::AreaDoorLink::Basic;
            std::string guid;
        };

        /** A marker drawn this frame: one area's anchor on screen. */
        struct Marker {
            ImVec2 at;
            int levelIndex = -1; // into g_LevelAreas
            int authored   = -1; // into _authored
        };

        struct HandleMarker {
            ImVec2 at;
            AreaHandle handle;
        };

        struct EdgeMarker {
            ImVec2 from;
            ImVec2 to;
            int authored;
            int index       = -1; // polygon edge, or -1 for another outline segment
            float fromDepth = 1.0F;
            float toDepth   = 1.0F;
        };

        void RefreshLevel(const std::string &level);
        void RefreshNearby(const Camera::CameraPose &camera);

        void RenderLevelList();
        void RenderLevelInspector(std::string &notice);
        void CopyLevelArea(int index, std::vector<Shared::Game::AreaDefinition> &copies, std::size_t &skipped) const;
        void RenderAuthoredList(const EditorSnapshot &snapshot, const EditorViewport &viewport);
        void RenderSelection();
        void RenderAuthoredInspector();
        void RenderExport(std::string &notice);

        void DrawLevelArea(std::vector<Gizmos::Drawing> &lines, int levelIndex, ImU32 colour, float thickness);
        void DrawAuthoredArea(std::vector<Gizmos::Drawing> &lines, const Camera::CameraPose &camera, int authored, ImU32 colour, float thickness);
        void DrawHandles(const Camera::CameraPose &camera);
        /** The selected areas' collision meshes as shaded triangles, rebuilt only when they change. */
        void DrawCollisionPreview(std::vector<Gizmos::Drawing> &lines);
        bool PickHandle(const ImVec2 &mouse);
        const EdgeMarker *PickEdge(const ImVec2 &mouse, bool polygonOnly, float &fraction) const;
        bool InsertPointAt(const ImVec2 &mouse);
        bool IsSelected(int index) const;

        /** Whether the class toggles and the text filter let a level area through, in the list and in the world alike. */
        bool IsShown(const Shared::Game::LevelArea &area) const;

        void SelectLevel(int levelIndex);
        void SelectAuthored(int authored, bool toggle = false);
        void SelectHandle(AreaHandle handle);

        /** Seeds the text fields from the selected drawn area. */
        void LoadDrafts();
        void ContentChanged() {
            ++_contentRevision;
        }

        /** A fresh id no drawn area uses. */
        std::string UniqueId(const char *stem) const;

        std::string _level;
        std::vector<std::uint32_t> _levelRows;                         // this level's slice of g_LevelAreas
        std::unordered_map<std::uint64_t, std::uint32_t> _levelByGuid; // the same rows by GUID, for union members

        std::vector<std::uint32_t> _nearby; // nearest first
        SDK::Vec3 _nearbyFrom {};
        float _nearbyRadius = -1.0F;

        std::vector<Shared::Game::AreaDefinition> _authored;
        std::unordered_map<std::string, std::string> _groups;
        std::uint64_t _contentRevision   = 0;
        std::uint64_t _selectionRevision = 0;
        std::string _groupFilter;
        AreaHandle _handle;
        std::vector<HandleMarker> _handleMarkers;
        std::vector<EdgeMarker> _edgeMarkers;
        std::vector<int> _selectedAreas;
        glm::vec3 _gizmoScale {1.0F};
        bool _clickHandled = false;
        bool _groundPlacementRequested = false;
        int _selectedLevel = -1;
        std::vector<LinkedDoor> _selectedDoors;

        bool _showLevel        = true;
        bool _showTriggerAreas = true;
        bool _showUnions       = true;
        bool _showSmartAreas   = true;
        bool _showAuthored     = true;
        bool _showCollision    = true;
        float _radius          = 60.0F;
        char _filter[64]       = {};

        // Text drafts for the selected drawn area, committed when a field is released.
        char _idDraft[64]         = {};
        char _nameDraft[96]       = {};
        char _labelsDraft[256]    = {};
        char _groupDraft[96]      = {};
        char _metadataDraft[4096] = {};
        bool _metadataInvalid     = false;
        std::uint64_t _collisionValidationRevision = ~std::uint64_t(0);
        std::string _collisionValidationId;
        std::string _collisionError;

        // Shading a large sphere's tens of thousands of triangles every frame is not worth it; its outline still shows.
        static constexpr std::size_t kCollisionPreviewTriangles = 16384;
        static constexpr std::uint32_t kCollisionFill           = 0x50FF8C1A; // 0xAARRGGBB
        std::vector<Gizmos::Drawing> _collisionPreview;
        std::uint64_t _collisionPreviewContent   = ~std::uint64_t(0);
        std::uint64_t _collisionPreviewSelection = ~std::uint64_t(0);

        // Where the cursor last rested on the world: the + buttons are clicked with it on the panel.
        SDK::Vec3 _lastWorldPoint {};
        bool _hasWorldPoint = false;

        std::vector<Marker> _markers;
        bool _tabVisible = false;
    };
} // namespace KCDC::Features::Prop
