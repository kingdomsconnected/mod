#pragma once

#include "features/prop/prop_editor_areas.h"
#include "features/prop/prop_editor_gizmo.h"
#include "features/prop/prop_editor_history.h"
#include "features/prop/prop_editor_patrols.h"

namespace {
    struct AuthoringContext {
        AuthoringContext() {
            ImGui::CreateContext();
            ImGui::GetIO().IniFilename = nullptr;
            ImGuizmo::SetImGuiContext(ImGui::GetCurrentContext());
        }
        ~AuthoringContext() {
            KCDC::Features::Prop::FinishReleasedGizmoDrag(true);
            ImGui::DestroyContext();
        }
    };

    KCDC::Shared::Game::AreaDefinition AuthoredBox(const std::string &id) {
        KCDC::Shared::Game::AreaDefinition area;
        area.id = id;
        area.name = id;
        area.shape.type = KCDC::Shared::Game::AreaShapeType::Box;
        area.shape.boxMin = {-1, -1, -1};
        area.shape.boxMax = {1, 1, 1};
        return area;
    }

    KCDC::Shared::Game::PatrolRouteDefinition AuthoredRoute(const std::string &id) {
        KCDC::Shared::Game::PatrolRouteDefinition route;
        route.id = id;
        route.points.push_back({{1, 2, 3}});
        return route;
    }
} // namespace

MODULE(world_builder_authoring, {
    IT("preserves collision through authored mutations and rejects invalid blocking shapes atomically", {
        AuthoringContext context;
        KCDC::Features::Prop::EditorAreas areas;
        auto area      = AuthoredBox("wall");
        area.collision = true;
        std::string error;
        EQUALS(areas.Create(area, error), true);
        std::string copy;
        EQUALS(areas.Duplicate("wall", copy, error), true);
        EQUALS(areas.Find(copy)->collision, true);
        area.shape.type     = KCDC::Shared::Game::AreaShapeType::Shape;
        area.shape.points   = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
        area.shape.height   = 0;
        const auto revision = areas.ContentRevision();
        EQUALS(areas.Update("wall", area, error), false);
        EQUALS(areas.ContentRevision(), revision);
        EQUALS(areas.Find("wall")->shape.type, KCDC::Shared::Game::AreaShapeType::Box);
        area           = *areas.Find("wall");
        area.collision = false;
        EQUALS(areas.ReplaceState({area}, {}, {"wall"}, error), true);
        EQUALS(areas.Find("wall")->collision, false);
    });

    IT("keeps collision plates and thickness through duplication and refuses walls the shape cannot hold", {
        AuthoringContext context;
        KCDC::Features::Prop::EditorAreas areas;
        auto area             = AuthoredBox("arena");
        area.collision        = true;
        area.collisionOptions = {false, true, 0.4F};
        std::string error;
        EQUALS(areas.Create(area, error), true);
        std::string copy;
        EQUALS(areas.Duplicate("arena", copy, error), true);
        EQUALS(areas.Find(copy)->collisionOptions == area.collisionOptions, true);
        const auto revision             = areas.ContentRevision();
        area.collisionOptions.thickness = 50.0F;
        EQUALS(areas.Update("arena", area, error), false);
        EQUALS(error.find("thickness") != std::string::npos, true);
        EQUALS(areas.ContentRevision(), revision);
        EQUALS(areas.Find("arena")->collisionOptions.thickness, 0.4F);
    });

    IT("tracks authored content and selection without treating idle or preview as edits", {
        AuthoringContext context;
        KCDC::Features::Prop::EditorAreas areas;
        KCDC::Features::Prop::EditorPatrols patrols;
        std::string error;
        const auto areaIdle   = areas.ContentRevision();
        const auto patrolIdle = patrols.ContentRevision();
        areas.BeginFrame();
        patrols.BeginFrame();
        areas.Import({}, false);
        patrols.Import({}, false);
        EQUALS(areas.ContentRevision(), areaIdle);
        EQUALS(patrols.ContentRevision(), patrolIdle);

        auto area = AuthoredBox("market");
        EQUALS(areas.Create(area, error), true);
        const auto areaCreated = areas.ContentRevision();
        EQUALS(areaCreated > areaIdle, true);
        EQUALS(areas.Create(area, error), false);
        EQUALS(areas.ContentRevision(), areaCreated);
        const auto areaSelection = areas.SelectionRevision();
        EQUALS(areas.Select({"market"}, error), true);
        EQUALS(areas.SelectionRevision() > areaSelection, true);
        EQUALS(areas.ContentRevision(), areaCreated);
        area.name = "Market square";
        EQUALS(areas.Update("market", area, error), true);
        EQUALS(areas.ContentRevision() > areaCreated, true);
        const auto areaUpdated = areas.ContentRevision();
        EQUALS(areas.SetGroup({"market"}, "town", error), true);
        EQUALS(areas.ContentRevision() > areaUpdated, true);
        const auto areaGrouped = areas.ContentRevision();
        areas.Import({AuthoredBox("gate")}, false);
        EQUALS(areas.ContentRevision() > areaGrouped, true);
        const auto areaImported = areas.ContentRevision();
        EQUALS(areas.Remove("gate", error), true);
        EQUALS(areas.ContentRevision() > areaImported, true);
        const auto areaRemoved = areas.ContentRevision();
        EQUALS(areas.ReplaceState({area}, {}, {"market"}, error), true);
        EQUALS(areas.ContentRevision() > areaRemoved, true);
        const auto areaReplaced = areas.ContentRevision();
        EQUALS(areas.ReplaceState({area, area}, {}, {}, error), false);
        EQUALS(areas.ContentRevision(), areaReplaced);

        auto route = AuthoredRoute("watch");
        EQUALS(patrols.Create(route, error), true);
        const auto routeCreated = patrols.ContentRevision();
        EQUALS(routeCreated > patrolIdle, true);
        EQUALS(patrols.Create(route, error), false);
        EQUALS(patrols.ContentRevision(), routeCreated);
        const auto routeSelection = patrols.SelectionRevision();
        EQUALS(patrols.Select("watch", error), true);
        EQUALS(patrols.SelectionRevision() > routeSelection, true);
        EQUALS(patrols.ContentRevision(), routeCreated);
        EQUALS(patrols.RequestPreview("watch", "guard", error), true);
        patrols.StopPreview();
        EQUALS(patrols.ContentRevision(), routeCreated);
        route.points.front().position.x = 9;
        EQUALS(patrols.Update("watch", route, error), true);
        EQUALS(patrols.ContentRevision() > routeCreated, true);
        const auto routeUpdated = patrols.ContentRevision();
        patrols.Import({AuthoredRoute("gate")}, false);
        EQUALS(patrols.ContentRevision() > routeUpdated, true);
        const auto routeImported = patrols.ContentRevision();
        EQUALS(patrols.Remove("gate", error), true);
        EQUALS(patrols.ContentRevision() > routeImported, true);
        const auto routeRemoved = patrols.ContentRevision();
        EQUALS(patrols.ReplaceState({route}, {"watch"}, error), true);
        EQUALS(patrols.ContentRevision() > routeRemoved, true);
        const auto routeReplaced = patrols.ContentRevision();
        EQUALS(patrols.ReplaceState({route, route}, {}, error), false);
        EQUALS(patrols.ContentRevision(), routeReplaced);
    });

    IT("rejects missing IDs, invalid geometry and duplicate areas without changing the document", {
        AuthoringContext context;
        KCDC::Features::Prop::EditorAreas areas;
        std::string error;
        auto area = AuthoredBox("market");
        EQUALS(areas.Create(area, error), true);
        EQUALS(areas.Create(area, error), false);
        EQUALS(areas.Update("missing", area, error), false);
        EQUALS(areas.Remove("missing", error), false);
        area.shape.boxMax = area.shape.boxMin;
        EQUALS(areas.Update("market", area, error), false);
        area = AuthoredBox("another");
        area.shape.rotation = glm::quat(0, 0, 0, 0);
        EQUALS(areas.Create(area, error), false);
        area = AuthoredBox("another");
        area.metadataJson = "[]";
        EQUALS(areas.Create(area, error), false);
        EQUALS(areas.Authored().size(), std::size_t(1));
        EQUALS(areas.Find("market")->shape.IsValid(), true);
    });

    IT("restores exact area IDs and validates groups and selections atomically", {
        AuthoringContext context;
        KCDC::Features::Prop::EditorAreas areas;
        std::string error;
        const auto first = AuthoredBox("market"), second = AuthoredBox("gate");
        EQUALS(areas.ReplaceState({first, second}, {{"market", "town"}}, {"market", "gate"}, error), true);
        EQUALS(areas.ReplaceState({first, first}, {}, {}, error), false);
        EQUALS(areas.ReplaceState({first}, {{"missing", "town"}}, {}, error), false);
        EQUALS(areas.ReplaceState({first}, {}, {"missing"}, error), false);
        EQUALS(areas.Authored().size(), std::size_t(2));
        EQUALS(areas.SetGroup({"market", "missing"}, "bad", error), false);
        EQUALS(areas.Groups().at("market"), std::string("town"));
        EQUALS(areas.Select({"missing"}, error), false);
        EQUALS(areas.SelectedIds().size(), std::size_t(2));
        EQUALS(areas.Remove("market", error), true);
        EQUALS(areas.SelectedIds().front(), std::string("gate"));
        EQUALS(areas.ReplaceState({second, first}, {{"market", "town"}}, {"market"}, error), true);
        EQUALS(areas.Authored()[1].id, std::string("market"));
        EQUALS(areas.SelectedIds().front(), std::string("market"));
        auto renamed = first;
        renamed.id = "square";
        EQUALS(areas.Update("market", renamed, error), true);
        EQUALS(areas.SelectedIds().front(), std::string("square"));
        EQUALS(areas.Groups().at("square"), std::string("town"));
        std::string duplicate;
        EQUALS(areas.Duplicate("square", duplicate, error), true);
        EQUALS(duplicate != "square", true);
        EQUALS(areas.Groups().at(duplicate), std::string("town"));
    });

    IT("rejects invalid route replacements and restores route identities and selection", {
        AuthoringContext context;
        KCDC::Features::Prop::EditorPatrols patrols;
        std::string error;
        auto first = AuthoredRoute("watch"), second = AuthoredRoute("gate");
        EQUALS(patrols.ReplaceState({first, second}, {"gate"}, error), true);
        EQUALS(patrols.Create(first, error), false);
        EQUALS(patrols.Update("missing", first, error), false);
        EQUALS(patrols.Remove("missing", error), false);
        EQUALS(patrols.ReplaceState({first, first}, {}, error), false);
        EQUALS(patrols.ReplaceState({first}, {"missing"}, error), false);
        auto invalid = first;
        invalid.points.clear();
        EQUALS(patrols.Update("watch", invalid, error), false);
        EQUALS(patrols.Authored().size(), std::size_t(2));
        EQUALS(patrols.Remove("watch", error), true);
        EQUALS(patrols.SelectedIds().front(), std::string("gate"));
        EQUALS(patrols.ReplaceState({first, second}, {"watch"}, error), true);
        EQUALS(patrols.Authored().front().id, std::string("watch"));
        first.id = "nightwatch";
        EQUALS(patrols.Update("watch", first, error), true);
        EQUALS(patrols.SelectedIds().front(), std::string("nightwatch"));
        std::string duplicate;
        EQUALS(patrols.Duplicate("nightwatch", duplicate, error), true);
        EQUALS(duplicate != "nightwatch", true);
    });

    IT("validates preview requests and queues stop after an authored route changes", {
        AuthoringContext context;
        KCDC::Features::Prop::EditorPatrols patrols;
        std::string error;
        auto route = AuthoredRoute("watch");
        EQUALS(patrols.Create(route, error), true);
        EQUALS(patrols.RequestPreview("missing", "guard", error), false);
        EQUALS(patrols.RequestPreview("watch", "not-a-role", error), false);
        EQUALS(patrols.RequestPreview("watch", "guard", error), true);
        std::shared_ptr<const KCDC::Shared::Game::PatrolRouteDefinition> preview;
        std::string role;
        EQUALS(patrols.TakePreviewRequest(preview, role), true);
        EQUALS(preview != nullptr, true);
        EQUALS(preview->id, std::string("watch"));
        EQUALS(role, std::string("guard"));
        route.points.front().position.x = 20;
        EQUALS(patrols.Update("watch", route, error), true);
        EQUALS(patrols.TakePreviewRequest(preview, role), true);
        EQUALS(preview == nullptr, true);
        EQUALS(patrols.TakePreviewRequest(preview, role), false);
    });
    IT("orders authored and native actions in one timeline without changing native identities", {
        using namespace KCDC::Features::Prop;
        auto before = std::make_shared<AuthoredHistoryState>();
        before->document.areas.push_back(AuthoredBox("market"));
        before->selectedAreas = {"market"};
        auto after = std::make_shared<AuthoredHistoryState>(*before);
        after->document.areas.front().shape.position.x = 10;
        EditorHistory history;
        HistoryAction authored;
        authored.kind = HistoryAction::Kind::Authored;
        authored.authoredBefore = before;
        authored.authoredAfter = after;
        history.Record({"Move area", 0, {authored}});
        HistoryAction native;
        native.kind = HistoryAction::Kind::Transform;
        native.id = 9007199254740993ULL;
        native.before.position = {1, 2, 3};
        native.after.position = {4, 5, 6};
        history.Record({"Move prop", 0, {native}});
        const auto propUndo = history.UndoStep();
        EQUALS(propUndo.has_value(), true);
        EQUALS(propUndo->actions.front().id, std::uint64_t(9007199254740993ULL));
        EQUALS(history.AuthoredRestoreSerial(), std::uint64_t(0));
        EQUALS(history.NextUndoLabel(), std::string("Move area"));
        const auto areaUndo = history.UndoStep();
        EQUALS(areaUndo.has_value(), true);
        EQUALS(history.AuthoredRestore()->document.areas.front().shape.position.x, 0.0F);
        EQUALS(history.AuthoredRestore()->selectedAreas.front(), std::string("market"));
        EQUALS(history.RedoStep().has_value(), true);
        EQUALS(history.AuthoredRestore()->document.areas.front().shape.position.x, 10.0F);
        history.Record({"Different prop edit", 0, {native}});
        EQUALS(history.RedoStep().has_value(), false);
    });

    IT("keeps a mixed blueprint paste in one step and returned steps own their lifetime", {
        using namespace KCDC::Features::Prop;
        auto before = std::make_shared<AuthoredHistoryState>();
        auto after = std::make_shared<AuthoredHistoryState>();
        after->document.areas.push_back(AuthoredBox("market"));
        after->document.areaGroups["market"] = "blueprint";
        after->document.patrolRoutes.push_back(AuthoredRoute("watch"));
        after->document.views.push_back({"Gate", {1, 2, 3}, 90, -20});
        after->selectedAreas = {"market"};
        after->selectedPatrols = {"watch"};
        HistoryAction authored;
        authored.kind = HistoryAction::Kind::Authored;
        authored.authoredBefore = before;
        authored.authoredAfter = after;
        HistoryAction native;
        native.kind = HistoryAction::Kind::Place;
        native.object.id = 400;
        EditorHistory history;
        history.Record({"Blueprint", 0, {native, authored}});
        auto undone = history.UndoStep();
        EQUALS(undone->actions.size(), std::size_t(2));
        EQUALS(history.AuthoredRestore()->document.areas.empty(), true);
        EQUALS(history.RedoStep().has_value(), true);
        AuthoringContext context;
        EditorAreas areas;
        EditorPatrols patrols;
        std::string error;
        const auto restored = history.AuthoredRestore();
        EQUALS(areas.ReplaceState(restored->document.areas, restored->document.areaGroups, restored->selectedAreas, error), true);
        EQUALS(patrols.ReplaceState(restored->document.patrolRoutes, restored->selectedPatrols, error), true);
        EQUALS(areas.SelectedIds().front(), std::string("market"));
        EQUALS(areas.Groups().at("market"), std::string("blueprint"));
        EQUALS(patrols.SelectedIds().front(), std::string("watch"));
        EQUALS(restored->document.views.front().name, std::string("Gate"));
        const auto serial = history.AuthoredRestoreSerial();
        history.Clear();
        EQUALS(history.AuthoredRestore() == nullptr, true);
        EQUALS(history.AuthoredRestoreSerial() > serial, true);
        EQUALS(undone->actions.front().object.id, std::uint64_t(400));
        EQUALS(undone->actions.back().authoredAfter->document.areas.front().id, std::string("market"));
    });

    IT("does not record an invalid interim shape or discard redo because of it", {
        using namespace KCDC::Features::Prop;
        auto before = std::make_shared<AuthoredHistoryState>();
        before->document.areas.push_back(AuthoredBox("market"));
        auto after = std::make_shared<AuthoredHistoryState>(*before);
        after->document.areas.front().shape.position.x = 10;
        HistoryAction action;
        action.kind = HistoryAction::Kind::Authored;
        action.authoredBefore = before;
        action.authoredAfter = after;
        EditorHistory history;
        history.Record({"Valid area edit", 0, {action}});
        EQUALS(history.UndoStep().has_value(), true);
        auto invalid = std::make_shared<AuthoredHistoryState>(*after);
        invalid->document.areas.front().shape.boxMax = invalid->document.areas.front().shape.boxMin;
        action.authoredAfter = invalid;
        history.Record({"Invalid drag frame", 0, {action}});
        EQUALS(history.NextUndoLabel().empty(), true);
        EQUALS(history.NextRedoLabel(), std::string("Valid area edit"));
        EQUALS(history.RedoStep().has_value(), true);
        auto notAuthored = *before;
        notAuthored.document.objects.emplace_back();
        EQUALS(notAuthored.IsValid(), false);
    });

    IT("coalesces drag steps while retaining the first before-state and last after-state", {
        using namespace KCDC::Features::Prop;
        EditorHistory history;
        HistoryAction action;
        action.kind = HistoryAction::Kind::Transform;
        action.id = 42;
        action.before.position.x = 1;
        action.after.position.x = 2;
        history.Record({"Drag", 9, {action}});
        action.before.position.x = 2;
        action.after.position.x = 3;
        history.Record({"Drag", 9, {action}});
        const auto undone = history.UndoStep();
        EQUALS(undone->actions.front().before.position.x, 1.0F);
        EQUALS(undone->actions.front().after.position.x, 3.0F);
        EQUALS(history.UndoStep().has_value(), false);
    });
});
