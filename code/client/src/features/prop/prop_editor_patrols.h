#pragma once

#include "features/gizmos/gizmos_geometry.h"
#include "prop_editor_ground_snap.h"
#include "shared/features/patrol/patrol_route.h"
#include <cstdint>
#include <imgui.h>
#include <memory>

namespace KCDC::Features::Prop {
    struct EditorSnapshot;
    struct MapDocument;

    // Authored data lives on the render thread; preview requests cross the
    // editor command queue and never expose the live NPC to this panel.
    class EditorPatrols final {
      public:
        void BeginFrame() {
            _visible = false;
        }
        bool IsTabVisible() const {
            return _visible;
        }
        void RenderTab(const EditorSnapshot &snapshot, std::string &notice);
        void RenderOverlay(const EditorSnapshot &snapshot, std::vector<Gizmos::Drawing> &lines);
        bool RenderGizmo(const float view[16], const float projection[16], const float *snap, GroundSnapFunction ground = {});
        bool Pick(const ImVec2 &mouse, const EditorSnapshot &snapshot);
        void DeleteSelected();
        void DuplicateSelected();
        void ClearPoint();
        MapDocument SelectedDocument() const;
        const std::vector<Shared::Game::PatrolRouteDefinition> &Authored() const {
            return _routes;
        }
        std::uint64_t ContentRevision() const {
            return _contentRevision;
        }
        std::uint64_t SelectionRevision() const {
            return _selectionRevision;
        }
        void Import(const std::vector<Shared::Game::PatrolRouteDefinition> &routes, bool replace);
        bool TakePreviewRequest(std::shared_ptr<const Shared::Game::PatrolRouteDefinition> &route, std::string &role);

        /** Stable-ID authoring API, render thread only. Point edits use a validated replacement route. */
        const Shared::Game::PatrolRouteDefinition *Find(const std::string &id) const;
        bool Create(const Shared::Game::PatrolRouteDefinition &route, std::string &error);
        bool Update(const std::string &id, const Shared::Game::PatrolRouteDefinition &route, std::string &error);
        bool Remove(const std::string &id, std::string &error);
        bool Duplicate(const std::string &id, std::string &createdId, std::string &error);
        bool Select(const std::string &id, std::string &error);
        std::vector<std::string> SelectedIds() const;
        bool ReplaceState(const std::vector<Shared::Game::PatrolRouteDefinition> &routes, const std::vector<std::string> &selectedIds, std::string &error);
        bool RequestPreview(const std::string &id, const std::string &role, std::string &error);
        void StopPreview();

      private:
        struct Marker {
            ImVec2 at;
            int route;
            int point;
        };
        struct Edge {
            ImVec2 from, to;
            int route, point, next;
            float fromDepth, toDepth;
        };
        Shared::Game::PatrolRouteDefinition *Selected();
        std::string UniqueId(const std::string &stem) const;
        void Select(int index);
        void Changed();
        void ContentChanged();
        void AddPoint(const glm::vec3 &position);
        std::vector<Shared::Game::PatrolRouteDefinition> _routes;
        std::uint64_t _contentRevision   = 0;
        std::uint64_t _selectionRevision = 0;
        std::vector<Marker> _markers;
        std::vector<Edge> _edges;
        int _selected        = -1;
        int _point           = -1;
        bool _visible        = false;
        bool _placing        = false;
        bool _previewRequest = false;
        std::shared_ptr<const Shared::Game::PatrolRouteDefinition> _preview;
        std::string _role = "guard";
        char _id[129] {};
        char _name[257] {};
    };
} // namespace KCDC::Features::Prop
