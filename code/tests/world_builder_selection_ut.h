#pragma once

#include "features/prop/prop_editor_history.h"
#include "features/prop/prop_editor_pick.h"
#include "features/prop/prop_editor_selection.h"
#include "features/prop/prop_editor_support.h"

#include <cctype>
#include <cmath>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

MODULE(world_builder_selection, {
    IT("spells a level mesh as the catalog does and refuses meshes the catalog does not ship", {
        const std::string model {KCDC::Shared::Game::g_PropModels.front().path};
        std::string engine = model;
        for (char &character : engine) {
            character = character == '/' ? '\\' : static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
        }
        EQUALS(KCDC::Features::Prop::CatalogModelForLevelMesh(engine) == model, true);
        EQUALS(KCDC::Features::Prop::CatalogModelForLevelMesh(model) == model, true);
        EQUALS(KCDC::Features::Prop::CatalogModelForLevelMesh("objects/never/shipped_by_the_game.cgf").empty(), true);
        EQUALS(KCDC::Features::Prop::CatalogModelForLevelMesh("").empty(), true);
    });

    IT("steps a level copy along its own X by its width", {
        const SDK::AABB box {SDK::Vec3 {0.0F, 0.0F, 0.0F}, SDK::Vec3 {2.0F, 0.5F, 1.0F}};
        const SDK::Vec3 straight = KCDC::Features::Prop::LevelCopyStep(SDK::Quat {0.0F, 0.0F, 0.0F, 1.0F}, &box);
        EQUALS((std::fabs(straight.x - 2.0F) < 1e-4F && std::fabs(straight.y) < 1e-4F && std::fabs(straight.z) < 1e-4F), true);

        // Turned a quarter about Z: its own X is the world's Y, and the world box is turned with it.
        const SDK::AABB turnedBox {SDK::Vec3 {0.0F, 0.0F, 0.0F}, SDK::Vec3 {0.5F, 2.0F, 1.0F}};
        const SDK::Vec3 turned = KCDC::Features::Prop::LevelCopyStep(SDK::Quat {0.0F, 0.0F, std::sqrt(0.5F), std::sqrt(0.5F)}, &turnedBox);
        EQUALS((std::fabs(turned.x) < 1e-4F && std::fabs(turned.y - 2.0F) < 1e-4F), true);

        // Without a box it still steps clear of itself.
        const SDK::Vec3 unknown = KCDC::Features::Prop::LevelCopyStep(SDK::Quat {0.0F, 0.0F, 0.0F, 1.0F}, nullptr);
        EQUALS(std::fabs(unknown.x - 0.25F) < 1e-4F, true);
    });

    IT("keeps selection order, refuses duplicates and promotes in order", {
        KCDC::Features::Prop::SelectionSet set;
        const KCDC::Features::Prop::SelectionRef a {KCDC::Features::Prop::SelectionKind::Placed, 1};
        const KCDC::Features::Prop::SelectionRef b {KCDC::Features::Prop::SelectionKind::World, 1};
        const KCDC::Features::Prop::SelectionRef c {KCDC::Features::Prop::SelectionKind::Light, 9};
        EQUALS(set.Insert(a), true);
        EQUALS(set.Insert(b), true);
        EQUALS(set.Insert(a), false);
        EQUALS(set.Insert(c), true);
        EQUALS(set.Size(), static_cast<std::size_t>(3));
        EQUALS(set.Contains(b), true);
        EQUALS(set.Erase(b), true);
        EQUALS(set.Erase(b), false);
        EQUALS((set.Items() == std::vector<KCDC::Features::Prop::SelectionRef> {a, c}), true);
        EQUALS((set.PopFront() == a), true);
        EQUALS(set.Contains(a), false);
        const std::vector<KCDC::Features::Prop::SelectionRef> refill {a, b, c, a};
        set.Assign(refill.begin(), refill.end());
        EQUALS(set.Size(), static_cast<std::size_t>(3));
        const std::size_t erased = set.EraseIf([](const KCDC::Features::Prop::SelectionRef &ref) {
            return ref.kind == KCDC::Features::Prop::SelectionKind::World;
        });
        EQUALS(erased, static_cast<std::size_t>(1));
        EQUALS((set.Contains(b) || set.Size() != 2), false);
    });

    IT("filters by kind, mesh, class, layer, group, physics, visibility and search words", {
        KCDC::Features::Prop::SelectionCandidate tree;
        tree.kind     = KCDC::Features::Prop::SelectableKinds::Vegetation;
        tree.name     = "oak_big_01";
        tree.mesh     = "Objects\\Natural\\Trees\\Oak_Big_01.cgf";
        tree.position = {1, 2, 3};
        KCDC::Features::Prop::SelectionFilter filter;
        EQUALS(filter.Matches(tree, filter.Tokens()), true);
        filter.kinds = KCDC::Features::Prop::SelectableKinds::Brush;
        EQUALS(filter.Matches(tree, filter.Tokens()), false);
        filter.kinds = KCDC::Features::Prop::SelectableKinds::Level;
        filter.mesh  = "objects/natural/trees/oak_big_01.cgf";
        EQUALS(filter.Matches(tree, filter.Tokens()), true);
        filter.mesh = "objects/natural/trees/oak_big_02.cgf";
        EQUALS(filter.Matches(tree, filter.Tokens()), false);
        filter.mesh = {};
        filter.text = "oak -birch";
        EQUALS(filter.Matches(tree, filter.Tokens()), true);
        filter.text = "oak -big";
        EQUALS(filter.Matches(tree, filter.Tokens()), false);
        filter.text = {};
        tree.hidden = true;
        EQUALS(filter.Matches(tree, filter.Tokens()), false);
        filter.includeHidden = true;
        EQUALS(filter.Matches(tree, filter.Tokens()), true);
        filter.layer = "ruins";
        EQUALS(filter.Matches(tree, filter.Tokens()), false);

        KCDC::Features::Prop::SelectionCandidate prop;
        prop.kind    = KCDC::Features::Prop::SelectableKinds::Prop;
        prop.group   = "market";
        prop.physics = KCDC::Common::Spawn::PropPhysics::Rigid;
        KCDC::Features::Prop::SelectionFilter props;
        props.group   = "market";
        props.physics = KCDC::Common::Spawn::PropPhysics::Rigid;
        EQUALS(props.Matches(prop, props.Tokens()), true);
        props.physics = KCDC::Common::Spawn::PropPhysics::Static;
        EQUALS(props.Matches(prop, props.Tokens()), false);
        props.physics   = std::nullopt;
        props.className = "AnimDoor";
        EQUALS(props.Matches(prop, props.Tokens()), false);
    });

    IT("reads filters from JSON and rejects unknown kinds and mistyped fields", {
        KCDC::Features::Prop::SelectionFilter filter;
        std::string error;
        EQUALS(KCDC::Features::Prop::SelectionFilterFromJson(nlohmann::json {{"kinds", {"vegetation", "brush"}}, {"mesh", "a.cgf"}, {"physics", "none"}, {"includeHidden", true}}, filter, error), true);
        EQUALS(filter.kinds, static_cast<std::uint8_t>(KCDC::Features::Prop::SelectableKinds::Vegetation | KCDC::Features::Prop::SelectableKinds::Brush));
        EQUALS((filter.physics == KCDC::Common::Spawn::PropPhysics::None && filter.includeHidden && filter.mesh == "a.cgf"), true);
        const nlohmann::json roundTrip = KCDC::Features::Prop::SelectionFilterToJson(filter);
        KCDC::Features::Prop::SelectionFilter read;
        EQUALS(KCDC::Features::Prop::SelectionFilterFromJson(roundTrip, read, error), true);
        EQUALS((read.kinds == filter.kinds && read.physics == filter.physics && read.mesh == filter.mesh), true);
        EQUALS(KCDC::Features::Prop::SelectionFilterFromJson(nlohmann::json(), filter, error), true);
        EQUALS(filter.kinds, KCDC::Features::Prop::SelectableKinds::All);
        const std::vector<nlohmann::json> refused {nlohmann::json {{"kinds", {"tree"}}}, nlohmann::json {{"kinds", nlohmann::json::array()}}, nlohmann::json {{"text", 4}}, nlohmann::json {{"physics", "solid"}}, nlohmann::json {{"includeHidden", "yes"}}, nlohmann::json(3)};
        for (const nlohmann::json &bad : refused) {
            error.clear();
            EQUALS(KCDC::Features::Prop::SelectionFilterFromJson(bad, filter, error), false);
            EQUALS(error.empty(), false);
        }
    });

    IT("tests points against a lasso outline, concave ones included", {
        const std::vector<glm::vec2> notch {{0, 0}, {4, 0}, {4, 1}, {1, 1}, {1, 4}, {0, 4}};
        EQUALS(KCDC::Features::Prop::PointInOutline(notch, {0.5F, 2.0F}), true);
        EQUALS(KCDC::Features::Prop::PointInOutline(notch, {2.0F, 0.5F}), true);
        EQUALS(KCDC::Features::Prop::PointInOutline(notch, {2.0F, 2.0F}), false);
        EQUALS(KCDC::Features::Prop::PointInOutline(notch, {-1.0F, 2.0F}), false);
    });

    IT("contains points in sphere, screen and area regions", {
        KCDC::Features::Camera::CameraPose camera;
        camera.position         = SDK::Vec3 {10.0F, 20.0F, 30.0F};
        const SDK::Vec3 forward = KCDC::Features::Camera::PoseForward(camera);
        const glm::vec3 eye(10.0F, 20.0F, 30.0F);
        const glm::vec3 ahead = eye + glm::vec3(forward.x, forward.y, forward.z) * 20.0F;

        KCDC::Features::Prop::SelectionRegion sphere;
        sphere.centre = {1, 1, 1};
        sphere.radius = 2;
        sphere.Prepare();
        EQUALS(sphere.IsValid(), true);
        EQUALS(sphere.Contains({2, 2, 1}, camera), true);
        EQUALS(sphere.Contains({4, 1, 1}, camera), false);

        KCDC::Features::Prop::SelectionRegion screen;
        screen.kind    = KCDC::Features::Prop::SelectionRegionKind::Screen;
        screen.outline = {{-0.2F, -0.2F}, {0.2F, -0.2F}, {0.2F, 0.2F}, {-0.2F, 0.2F}};
        screen.reach   = 50.0F;
        screen.Prepare();
        EQUALS(screen.IsValid(), true);
        EQUALS(screen.Contains(ahead, camera), true);
        EQUALS(screen.Contains(eye - (ahead - eye), camera), false);        // behind the camera
        EQUALS(screen.Contains(eye + (ahead - eye) * 3.0F, camera), false); // past the reach
        glm::vec3 min {0.0F};
        glm::vec3 max {0.0F};
        screen.QueryBox(camera, min, max);
        EQUALS((glm::all(glm::lessThanEqual(min, ahead)) && glm::all(glm::greaterThanEqual(max, ahead))), true);

        KCDC::Features::Prop::SelectionRegion area;
        area.kind          = KCDC::Features::Prop::SelectionRegionKind::Area;
        area.area.type     = KCDC::Shared::Game::AreaShapeType::Shape;
        area.area.position = {100, 100, 0};
        area.area.points   = {{0, 0, 0}, {10, 0, 0}, {10, 10, 0}, {0, 10, 0}};
        area.Prepare();
        EQUALS(area.IsValid(), true);
        EQUALS(area.Contains({105, 105, 500}, camera), true); // no ceiling: any height
        EQUALS(area.Contains({115, 105, 0}, camera), false);
        area.QueryBox(camera, min, max);
        EQUALS((max.z - min.z <= 2.0F * KCDC::Features::Prop::SelectionRegion::kMaxRadius + 1e-3F), true);

        KCDC::Features::Prop::SelectionRegion line;
        line.kind    = KCDC::Features::Prop::SelectionRegionKind::Screen;
        line.outline = {{0, 0}, {1, 1}};
        EQUALS(line.IsValid(), false);
    });

    IT("covers the whole screen slice out to the reach, off the view's axis too", {
        KCDC::Features::Camera::CameraPose camera;
        camera.position         = SDK::Vec3 {0.0F, 0.0F, 0.0F};
        const SDK::Vec3 forward = KCDC::Features::Camera::PoseForward(camera);
        KCDC::Features::Prop::SelectionRegion screen;
        screen.kind    = KCDC::Features::Prop::SelectionRegionKind::Screen;
        screen.outline = {{-1.0F, -1.0F}, {1.0F, -1.0F}, {1.0F, 1.0F}, {-1.0F, 1.0F}};
        screen.reach   = 150.0F;
        screen.Prepare();
        glm::vec3 min {0.0F};
        glm::vec3 max {0.0F};
        screen.QueryBox(camera, min, max);
        // Straight ahead at 95% of the reach is inside the outline and within reach; the box must hold it.
        const glm::vec3 ahead = glm::vec3(forward.x, forward.y, forward.z) * (0.95F * screen.reach);
        EQUALS(screen.Contains(ahead, camera), true);
        EQUALS((glm::all(glm::lessThanEqual(min, ahead)) && glm::all(glm::greaterThanEqual(max, ahead))), true);
        // And the box never needs more than the reach around the eye.
        EQUALS((glm::all(glm::greaterThanEqual(min, glm::vec3(-screen.reach - 1e-3F))) && glm::all(glm::lessThanEqual(max, glm::vec3(screen.reach + 1e-3F)))), true);
    });

    IT("asks an erase predicate once per member and keeps order and membership in step", {
        KCDC::Features::Prop::SelectionSet set;
        for (std::uint64_t id = 1; id <= 6; ++id) {
            set.Insert({KCDC::Features::Prop::SelectionKind::Placed, id});
        }
        int calls                = 0;
        const std::size_t erased = set.EraseIf([&calls](const KCDC::Features::Prop::SelectionRef &ref) {
            ++calls;
            return ref.id % 2 == 0;
        });
        EQUALS(calls, 6);
        EQUALS(erased, static_cast<std::size_t>(3));
        EQUALS((set.Size() == 3 && set.Contains({KCDC::Features::Prop::SelectionKind::Placed, 1}) && !set.Contains({KCDC::Features::Prop::SelectionKind::Placed, 2})), true);
        EQUALS(set.Erase({KCDC::Features::Prop::SelectionKind::Placed, 3}), true);
        EQUALS((set.Items() == std::vector<KCDC::Features::Prop::SelectionRef> {{KCDC::Features::Prop::SelectionKind::Placed, 1}, {KCDC::Features::Prop::SelectionKind::Placed, 5}}), true);
    });

    IT("keeps level objects that a layer step can still restore", {
        KCDC::Features::Prop::EditorLayers layers;
        std::string error;
        const std::string trees = layers.Create("Trees", error);
        EQUALS(trees.empty(), false);
        auto before = std::make_shared<const KCDC::Features::Prop::EditorLayers>(layers);
        EQUALS(layers.Assign({"world:42", "prop:7"}, trees, error), true);
        KCDC::Features::Prop::HistoryAction action;
        action.kind         = KCDC::Features::Prop::HistoryAction::Kind::Layers;
        action.layersBefore = before;
        action.layersAfter  = std::make_shared<const KCDC::Features::Prop::EditorLayers>(layers);
        KCDC::Features::Prop::HistoryStep step;
        step.label = "Edit layers";
        step.actions.push_back(action);
        KCDC::Features::Prop::EditorHistory history;
        history.Record(step);
        std::unordered_set<std::uint64_t> ids;
        history.CollectWorldIds(ids);
        EQUALS((ids.size() == 1 && ids.contains(42)), true);
    });

    IT("names each object an undo step touched once, in order, and skips layer and area edits", {
        KCDC::Features::Prop::HistoryStep step;
        const auto add = [&step](KCDC::Features::Prop::HistoryAction::Kind kind, std::uint64_t id) {
            KCDC::Features::Prop::HistoryAction action;
            action.kind = kind;
            action.id   = id;
            step.actions.push_back(action);
        };
        add(KCDC::Features::Prop::HistoryAction::Kind::Transform, 7);
        add(KCDC::Features::Prop::HistoryAction::Kind::WorldHide, 3);
        add(KCDC::Features::Prop::HistoryAction::Kind::Layers, 0);
        add(KCDC::Features::Prop::HistoryAction::Kind::LightMove, 2);
        add(KCDC::Features::Prop::HistoryAction::Kind::Transform, 7);
        KCDC::Features::Prop::HistoryAction placed;
        placed.kind      = KCDC::Features::Prop::HistoryAction::Kind::Place;
        placed.object.id = 11;
        step.actions.push_back(placed);
        const std::vector<KCDC::Features::Prop::SelectionRef> expected {{KCDC::Features::Prop::SelectionKind::Placed, 7}, {KCDC::Features::Prop::SelectionKind::World, 3}, {KCDC::Features::Prop::SelectionKind::Light, 2}, {KCDC::Features::Prop::SelectionKind::Placed, 11}};
        EQUALS((KCDC::Features::Prop::EditorHistory::TouchedObjects(step) == expected), true);
    });

    IT("enters a box turned and scaled with its object, not the world box around it", {
        // A plank four metres long and twenty centimetres thick, turned an eighth about Z.
        const SDK::AABB plank {SDK::Vec3 {-2.0F, -0.1F, 0.0F}, SDK::Vec3 {2.0F, 0.1F, 1.0F}};
        const SDK::Matrix34 turned = SDK::Matrix34FromTransform(SDK::Vec3 {0.0F, 10.0F, 0.0F}, SDK::Quat {0.0F, 0.0F, 0.3826834F, 0.9238795F}, SDK::Vec3 {1.0F, 1.0F, 1.0F});
        const SDK::AABB worldBox {SDK::Vec3 {-1.485F, 8.515F, 0.0F}, SDK::Vec3 {1.485F, 11.485F, 1.0F}};
        bool inside = true;

        // Square on: through its thickness, 0.1 * sqrt(2) before its pivot.
        const float square = KCDC::Features::Prop::RayOrientedBoxEntry(SDK::Vec3 {0.0F, 0.0F, 0.5F}, SDK::Vec3 {0.0F, 1.0F, 0.0F}, plank, turned, inside);
        EQUALS((std::fabs(square - 9.8586F) < 1e-3F && !inside), true);

        // Alongside it, a metre off: through the corner of the world box, past the plank.
        const SDK::Vec3 beside {-4.2929F, 4.2929F, 0.5F};
        const SDK::Vec3 along {0.70710678F, 0.70710678F, 0.0F};
        EQUALS(SDK::RayBoxEntryDistance(beside, along, worldBox) > 0.0F, true);
        EQUALS(KCDC::Features::Prop::RayOrientedBoxEntry(beside, along, plank, turned, inside) < 0.0F, true);

        // Stretched along its own X: the answer stays in world metres.
        const SDK::AABB unit {SDK::Vec3 {0.0F, 0.0F, 0.0F}, SDK::Vec3 {1.0F, 1.0F, 1.0F}};
        const SDK::Matrix34 stretched = SDK::Matrix34FromTransform(SDK::Vec3 {0.0F, 5.0F, 0.0F}, SDK::Quat {0.0F, 0.0F, 0.0F, 1.0F}, SDK::Vec3 {2.0F, 1.0F, 1.0F});
        EQUALS(std::fabs(KCDC::Features::Prop::RayOrientedBoxEntry(SDK::Vec3 {1.5F, 0.0F, 0.5F}, SDK::Vec3 {0.0F, 1.0F, 0.0F}, unit, stretched, inside) - 5.0F) < 1e-4F, true);
        EQUALS(KCDC::Features::Prop::RayOrientedBoxEntry(SDK::Vec3 {2.5F, 0.0F, 0.5F}, SDK::Vec3 {0.0F, 1.0F, 0.0F}, unit, stretched, inside) < 0.0F, true);
        EQUALS(std::fabs(KCDC::Features::Prop::OrientedBoxVolume(unit, stretched) - 2.0F) < 1e-4F, true);
        EQUALS(KCDC::Features::Prop::OrientedBoxContains(unit, stretched, SDK::Vec3 {1.9F, 5.5F, 0.5F}, 0.0F), true);
        EQUALS(KCDC::Features::Prop::OrientedBoxContains(unit, stretched, SDK::Vec3 {2.04F, 5.5F, 0.5F}, 0.0F), false);
        EQUALS(KCDC::Features::Prop::OrientedBoxContains(unit, stretched, SDK::Vec3 {2.04F, 5.5F, 0.5F}, 0.05F), true);

        // From inside it says so, and a flattened box is never entered.
        EQUALS((KCDC::Features::Prop::RayOrientedBoxEntry(SDK::Vec3 {1.0F, 5.5F, 0.5F}, SDK::Vec3 {0.0F, 1.0F, 0.0F}, unit, stretched, inside) == 0.0F && inside), true);
        const SDK::Matrix34 flat = SDK::Matrix34FromTransform(SDK::Vec3 {}, SDK::Quat {0.0F, 0.0F, 0.0F, 1.0F}, SDK::Vec3 {1.0F, 0.0F, 1.0F});
        EQUALS(KCDC::Features::Prop::RayOrientedBoxEntry(SDK::Vec3 {0.5F, -3.0F, 0.5F}, SDK::Vec3 {0.0F, 1.0F, 0.0F}, unit, flat, inside) < 0.0F, true);
    });

    IT("selects the object the cursor is on, not one whose box the ray crosses", {
        const SDK::Matrix34 still = SDK::Matrix34FromTransform(SDK::Vec3 {}, SDK::Quat {0.0F, 0.0F, 0.0F, 1.0F}, SDK::Vec3 {1.0F, 1.0F, 1.0F});
        const auto box            = [&still](std::uint64_t id, const SDK::Vec3 &min, const SDK::Vec3 &max, bool solid) {
            return KCDC::Features::Prop::PlacedPickBox {id, SDK::AABB {min, max}, still, solid};
        };
        const auto struck = [](float distance, const SDK::Vec3 &point, std::uint64_t placed) {
            KCDC::Features::Prop::PickSurface surface;
            surface.hit      = true;
            surface.distance = distance;
            surface.point    = point;
            surface.placed   = placed;
            return surface;
        };
        const SDK::Vec3 eye {0.0F, 0.0F, 0.5F};
        const SDK::Vec3 ahead {0.0F, 1.0F, 0.0F};

        // A table struck at five metres, behind the loose box of a solid fence entered at three: the table.
        const std::vector<KCDC::Features::Prop::PlacedPickBox> behindFence {box(2, {-3.0F, 3.0F, 0.0F}, {3.0F, 9.0F, 2.0F}, true), box(1, {-0.5F, 5.0F, 0.0F}, {0.5F, 6.0F, 1.0F}, true)};
        KCDC::Features::Prop::PlacedPick pick = KCDC::Features::Prop::ChoosePlacedPick(behindFence, eye, ahead, 2000.0F, struck(5.0F, {0.0F, 5.0F, 0.5F}, 1));
        EQUALS((pick.id == 1 && pick.method == KCDC::Features::Prop::PlacedPickMethod::Collision && pick.distance == 5.0F), true);

        // The camera stands in a canopy without collision; a candle ahead of the ground is what the click is on.
        const std::vector<KCDC::Features::Prop::PlacedPickBox> underCanopy {box(3, {-4.0F, -4.0F, 0.0F}, {4.0F, 4.0F, 3.0F}, false), box(4, {-0.1F, 6.0F, 0.4F}, {0.1F, 6.2F, 0.6F}, false)};
        pick = KCDC::Features::Prop::ChoosePlacedPick(underCanopy, eye, ahead, 2000.0F, struck(8.0F, {0.0F, 8.0F, 0.5F}, 0));
        EQUALS((pick.id == 4 && pick.method == KCDC::Features::Prop::PlacedPickMethod::Bounds && std::fabs(pick.distance - 6.0F) < 1e-4F), true);

        // Behind what the ray struck it is hidden; with nothing struck it is in reach, up to the range.
        pick = KCDC::Features::Prop::ChoosePlacedPick(underCanopy, eye, ahead, 2000.0F, struck(4.0F, {0.0F, 4.0F, 0.5F}, 0));
        EQUALS((pick.id == 0 && pick.method == KCDC::Features::Prop::PlacedPickMethod::None), true);
        pick = KCDC::Features::Prop::ChoosePlacedPick(underCanopy, eye, ahead, 2000.0F, KCDC::Features::Prop::PickSurface {});
        EQUALS(pick.id, static_cast<std::uint64_t>(4));
        pick = KCDC::Features::Prop::ChoosePlacedPick(underCanopy, eye, ahead, 5.0F, KCDC::Features::Prop::PickSurface {});
        EQUALS(pick.id, static_cast<std::uint64_t>(0));

        // Looking down at a table: a cloth lying on it is taken before it, a canopy around it is not.
        const SDK::Vec3 above {0.0F, 1.0F, 3.5F};
        const SDK::Vec3 down {0.0F, 0.8F, -0.6F};
        const KCDC::Features::Prop::PickSurface tableTop = struck(2.5F / 0.6F, {0.0F, 1.0F + 0.8F * 2.5F / 0.6F, 1.0F}, 1);
        const std::vector<KCDC::Features::Prop::PlacedPickBox> clothOnTable {box(1, {-1.0F, 4.0F, 0.0F}, {1.0F, 6.0F, 1.0F}, true), box(5, {-0.5F, 4.1F, 1.0F}, {0.5F, 4.9F, 1.02F}, false)};
        pick = KCDC::Features::Prop::ChoosePlacedPick(clothOnTable, above, down, 2000.0F, tableTop);
        EQUALS((pick.id == 5 && pick.method == KCDC::Features::Prop::PlacedPickMethod::Bounds), true);
        const std::vector<KCDC::Features::Prop::PlacedPickBox> canopyOverTable {box(6, {-3.0F, 3.0F, 0.0F}, {3.0F, 7.0F, 3.2F}, false), box(1, {-1.0F, 4.0F, 0.0F}, {1.0F, 6.0F, 1.0F}, true)};
        pick = KCDC::Features::Prop::ChoosePlacedPick(canopyOverTable, above, down, 2000.0F, tableTop);
        EQUALS((pick.id == 1 && pick.method == KCDC::Features::Prop::PlacedPickMethod::Collision), true);

        // A tree is solid at the trunk only. Through its crown to ground beyond, the tree; to the ground under it, not.
        const std::vector<KCDC::Features::Prop::PlacedPickBox> tree {box(7, {-2.0F, 8.0F, 0.0F}, {2.0F, 12.0F, 8.0F}, true)};
        pick = KCDC::Features::Prop::ChoosePlacedPick(tree, SDK::Vec3 {0.0F, 0.0F, 5.0F}, ahead, 2000.0F, struck(30.0F, {0.0F, 30.0F, 5.0F}, 0));
        EQUALS((pick.id == 7 && pick.method == KCDC::Features::Prop::PlacedPickMethod::Bounds && std::fabs(pick.distance - 8.0F) < 1e-4F), true);
        const float slope = std::sqrt(181.0F);
        pick              = KCDC::Features::Prop::ChoosePlacedPick(tree, SDK::Vec3 {0.0F, 0.0F, 9.0F}, SDK::Vec3 {0.0F, 10.0F / slope, -9.0F / slope}, 2000.0F, struck(slope, {0.0F, 10.0F, 0.0F}, 0));
        EQUALS(pick.id, static_cast<std::uint64_t>(0));

        // Entered together, the smaller box; apart, the nearer. Either order.
        const KCDC::Features::Prop::PlacedPickBox crate  = box(8, {-1.0F, 5.0F, 0.0F}, {1.0F, 7.0F, 2.0F}, false);
        const KCDC::Features::Prop::PlacedPickBox candle = box(9, {-0.2F, 5.1F, 0.4F}, {0.2F, 5.3F, 0.6F}, false);
        const KCDC::Features::Prop::PlacedPickBox beyond = box(10, {-0.2F, 6.0F, 0.4F}, {0.2F, 6.2F, 0.6F}, false);
        EQUALS(KCDC::Features::Prop::ChoosePlacedPick({crate, candle}, eye, ahead, 2000.0F, KCDC::Features::Prop::PickSurface {}).id, static_cast<std::uint64_t>(9));
        EQUALS(KCDC::Features::Prop::ChoosePlacedPick({candle, crate}, eye, ahead, 2000.0F, KCDC::Features::Prop::PickSurface {}).id, static_cast<std::uint64_t>(9));
        EQUALS(KCDC::Features::Prop::ChoosePlacedPick({crate, beyond}, eye, ahead, 2000.0F, KCDC::Features::Prop::PickSurface {}).id, static_cast<std::uint64_t>(8));
        EQUALS(KCDC::Features::Prop::ChoosePlacedPick({beyond, crate}, eye, ahead, 2000.0F, KCDC::Features::Prop::PickSurface {}).id, static_cast<std::uint64_t>(8));
    });
});
