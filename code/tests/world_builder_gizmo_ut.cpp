#include "features/prop/prop_editor_area_geometry.h"
#include "features/prop/prop_editor_gizmo.h"

#include <stdexcept>

namespace {
    void Require(bool condition, const char *message) {
        if (!condition) {
            throw std::runtime_error(message);
        }
    }
} // namespace

void TestWorldBuilderAreaGeometry() {
    {
        KCDC::Shared::Game::AreaShape grounded;
        grounded.position = {0, 0, 10};
        grounded.type     = KCDC::Shared::Game::AreaShapeType::Sphere;
        grounded.radius   = 4;
        Require(KCDC::Features::Prop::AreaGroundFloor(grounded) == 6, "Ground snapping must use a sphere's bottom, not its centre");
        grounded.type     = KCDC::Shared::Game::AreaShapeType::Box;
        grounded.boxMin   = {-2, -1, -3};
        grounded.boxMax   = {2, 1, 3};
        grounded.rotation = glm::angleAxis(glm::radians(90.0F), glm::vec3(0, 1, 0));
        Require(std::fabs(KCDC::Features::Prop::AreaGroundFloor(grounded) - 8) < 0.0001F, "Tilted box ground offset must follow its rotated bottom");
        grounded.type     = KCDC::Shared::Game::AreaShapeType::Shape;
        grounded.rotation = glm::quat(1, 0, 0, 0);
        grounded.points   = {{0, 0, -2}, {1, 0, 0}, {0, 1, 1}};
        grounded.height   = 0;
        Require(KCDC::Features::Prop::AreaGroundFloor(grounded) == 8, "An unbounded polygon grounds its drawn floor, not its containment bounds");
    }
    KCDC::Shared::Game::AreaShape shape;
    shape.type          = KCDC::Shared::Game::AreaShapeType::Shape;
    shape.position      = {100, 200, 10};
    shape.rotation      = glm::angleAxis(glm::radians(90.0F), glm::vec3(0, 0, 1));
    shape.points        = {{-2, -2, 0}, {2, -2, 0}, {2, 2, 0}, {-2, 2, 0}};
    shape.height        = 4;
    const auto original = shape;
    const KCDC::Features::Prop::AreaHandle corner {KCDC::Features::Prop::AreaHandle::Kind::Point, 1};
    const auto target = KCDC::Features::Prop::AreaHandlePosition(shape, corner) + glm::vec3(3, 0, 1);
    Require(KCDC::Features::Prop::MoveAreaHandle(shape, corner, target), "Rotated point edit failed");
    Require(glm::length(shape.WorldPoints()[1] - target) < 0.0001F, "Point movement did not convert world to local space");
    Require(shape.points[0] == original.points[0] && shape.points[2] == original.points[2] && shape.points[3] == original.points[3], "Point edit moved other corners");
    Require(shape.position == original.position && shape.rotation == original.rotation, "Point edit moved the area pivot");

    shape = original;
    const KCDC::Features::Prop::AreaHandle edge {KCDC::Features::Prop::AreaHandle::Kind::Edge, 3};
    const auto before = shape.WorldPoints();
    Require(KCDC::Features::Prop::MoveAreaHandle(shape, edge, KCDC::Features::Prop::AreaHandlePosition(shape, edge) + glm::vec3(1, 2, 0)), "Closing edge edit failed");
    const auto after = shape.WorldPoints();
    Require(glm::length(after[3] - before[3] - glm::vec3(1, 2, 0)) < 0.0001F && glm::length(after[0] - before[0] - glm::vec3(1, 2, 0)) < 0.0001F, "Closing edge must move its last and first points together");
    Require(shape.points[1] == original.points[1] && shape.points[2] == original.points[2], "Edge edit moved unrelated points");
    const KCDC::Features::Prop::AreaHandle height {KCDC::Features::Prop::AreaHandle::Kind::Height};
    Require(KCDC::Features::Prop::MoveAreaHandle(shape, height, KCDC::Features::Prop::AreaHandlePosition(shape, height) + glm::vec3(0, 0, 3)) && shape.height == 7, "Height handle did not move the ceiling");
    const auto saved = KCDC::Shared::Game::AreaDefinitionToJson({"edited", "Edited", shape});
    KCDC::Shared::Game::AreaDefinition parsed;
    std::string error;
    Require(KCDC::Shared::Game::AreaDefinitionFromJson(saved, parsed, error) && parsed.shape.points == shape.points, "Edited geometry did not survive serialization");
    Require(!KCDC::Features::Prop::MoveAreaHandle(shape, {KCDC::Features::Prop::AreaHandle::Kind::Point, 99}, {}), "Stale point handle accepted");

    auto split = original;
    Require(KCDC::Features::Prop::SplitAreaEdge(split, 3, 0.25F), "Closing edge insertion failed");
    Require(split.points.size() == 5 && split.points[4] == glm::vec3(-2, 1, 0), "Inserted point was not at the requested fraction of the closing edge");
    Require(std::equal(original.points.begin(), original.points.end(), split.points.begin()), "Edge insertion changed existing points");
    Require(!KCDC::Features::Prop::SplitAreaEdge(split, 0, 0) && !KCDC::Features::Prop::SplitAreaEdge(split, 0, 1), "Insertion created a duplicate endpoint");
    Require(!KCDC::Features::Prop::SplitAreaEdge(split, -1) && !KCDC::Features::Prop::SplitAreaEdge(split, 5), "Stale edge accepted");
    split.points.resize(KCDC::Shared::Game::AreaShape::kMaxPoints);
    Require(!KCDC::Features::Prop::SplitAreaEdge(split, 0), "Edge insertion exceeded the point limit");

    using KCDC::Features::Prop::AreaHandle;
    using KCDC::Features::Prop::AreaPointDeletion;
    using KCDC::Features::Prop::DeleteAreaHandlePoints;
    auto trimmed = original;
    Require(DeleteAreaHandlePoints(trimmed, corner) == AreaPointDeletion::PointsRemoved, "Deleting a quadrilateral corner should keep the area");
    Require(trimmed.points == std::vector<glm::vec3> {original.points[0], original.points[2], original.points[3]}, "Point deletion removed an unrelated corner");
    Require(trimmed.position == original.position && trimmed.rotation == original.rotation && trimmed.height == original.height, "Point deletion changed the area transform");
    const auto triangle = trimmed.points;
    Require(DeleteAreaHandlePoints(trimmed, {AreaHandle::Kind::Point, 0}) == AreaPointDeletion::RemoveArea && trimmed.points == triangle, "Triangle deletion must request removal of the whole area");
    trimmed = original;
    Require(DeleteAreaHandlePoints(trimmed, edge) == AreaPointDeletion::RemoveArea && trimmed.points == original.points, "Deleting two quadrilateral endpoints must remove the area");
    trimmed.points      = {{-2, -2, 0}, {2, -2, 0}, {3, 0, 0}, {2, 2, 0}, {-2, 2, 0}};
    const auto pentagon = trimmed;
    Require(DeleteAreaHandlePoints(trimmed, {AreaHandle::Kind::Edge, 1}) == AreaPointDeletion::PointsRemoved, "Pentagon edge deletion should keep a triangle");
    Require(trimmed.points == std::vector<glm::vec3> {pentagon.points[0], pentagon.points[3], pentagon.points[4]}, "Edge deletion removed the wrong endpoints");
    trimmed = pentagon;
    Require(DeleteAreaHandlePoints(trimmed, {AreaHandle::Kind::Edge, 4}) == AreaPointDeletion::PointsRemoved, "Closing edge deletion failed");
    Require(trimmed.points == std::vector<glm::vec3> {pentagon.points[1], pentagon.points[2], pentagon.points[3]}, "Closing edge deletion must remove the first and last points");
    Require(DeleteAreaHandlePoints(trimmed, {AreaHandle::Kind::Point, 99}) == AreaPointDeletion::Unchanged, "Stale point selection deleted the area");
    Require(DeleteAreaHandlePoints(trimmed, {AreaHandle::Kind::Edge, -1}) == AreaPointDeletion::Unchanged, "Stale edge selection deleted the area");
    Require(DeleteAreaHandlePoints(trimmed, height) == AreaPointDeletion::Unchanged, "Height handle deleted polygon corners");

    auto scaled        = original;
    scaled.points[0].z = 1;
    KCDC::Features::Prop::ScaleAreaShape(scaled, glm::vec3(2));
    Require(scaled.points[0] == glm::vec3(-4, -4, 2) && scaled.height == 8, "Selection scale lost polygon heights");
    Require(KCDC::Features::Prop::UniformAreaScale(glm::vec3(1, 1.5F, 1)) == 1.5F, "Selection did not derive uniform scale from the dragged axis");

    shape.type   = KCDC::Shared::Game::AreaShapeType::Sphere;
    shape.radius = 4;
    const KCDC::Features::Prop::AreaHandle radius {KCDC::Features::Prop::AreaHandle::Kind::Radius, 1};
    Require(KCDC::Features::Prop::MoveAreaHandle(shape, radius, shape.position + glm::vec3(8, 0, 0)) && shape.radius == 8, "Sphere handle failed to resize");
    Require(KCDC::Features::Prop::MoveAreaHandle(shape, radius, shape.position - glm::vec3(8, 0, 0)) && shape.radius > 0, "Dragging radius through centre made it invalid");
    Require(KCDC::Features::Prop::MoveAreaHandle(shape, radius, shape.position + glm::vec3(100000, 0, 0)) && shape.IsValid(), "Radius reached the excluded maximum extent");

    shape.type   = KCDC::Shared::Game::AreaShapeType::Box;
    shape.boxMin = {-2, -3, -4};
    shape.boxMax = {2, 3, 4};
    const KCDC::Features::Prop::AreaHandle face {KCDC::Features::Prop::AreaHandle::Kind::BoxFace, 1};
    Require(KCDC::Features::Prop::MoveAreaHandle(shape, face, shape.position + shape.rotation * glm::vec3(5, 0, 0)), "Rotated face edit failed");
    Require(std::fabs(shape.boxMax.x - 5) < 0.0001F && shape.boxMin == glm::vec3(-2, -3, -4), "Box edit moved the opposite face");
    Require(KCDC::Features::Prop::MoveAreaHandle(shape, face, shape.position + shape.rotation * glm::vec3(-5, 0, 0)) && shape.IsValid(), "Crossed box faces became invalid");

    KCDC::Shared::Game::LevelArea source {"test", 1, KCDC::Shared::Game::LevelAreaClass::TriggerArea, "House", "crime;private", "", 5, 0, 4, 0, 0, 0, 0, {100, 200, 10}, {110, 210, 16}};
    const std::vector<KCDC::Shared::Game::LevelAreaPoint> points {{100, 200, 11}, {110, 200, 10}, {110, 210, 10}, {100, 210, 10}};
    Require(KCDC::Features::Prop::EditableLevelArea(source, points, "house_copy", parsed), "Native polygon conversion failed");
    Require(parsed.id == "house_copy" && parsed.name == "House" && parsed.labels == std::vector<std::string> {"crime", "private"}, "Native copy lost identity or labels");
    Require(parsed.shape.position.z == 10 && parsed.shape.height == 6 && parsed.shape.Contains({105, 205, 15.5F}, parsed.shape.WorldPoints()), "Native copy lost the catalog's vertical bounds");
    Require(!parsed.shape.Contains({105, 205, 16.5F}, parsed.shape.WorldPoints()), "Native copy extended past the ceiling");
    source.areaClass = KCDC::Shared::Game::LevelAreaClass::AreaUnion;
    Require(!KCDC::Features::Prop::EditableLevelArea(source, points, "union", parsed), "Union was invented as a single polygon");
}

void TestWorldBuilderGizmo() {
    ImGui::CreateContext();
    auto &io              = ImGui::GetIO();
    io.IniFilename        = nullptr;
    io.DisplaySize        = ImVec2(800, 600);
    io.DeltaTime          = 1.0F / 60.0F;
    unsigned char *pixels = nullptr;
    int width             = 0;
    int height            = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    ImGuizmo::SetImGuiContext(ImGui::GetCurrentContext());

    const float view[16] {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    const float projection[16] {0.75F, 0, 0, 0, 0, 1, 0, 0, 0, 0, -1.000025F, -1, 0, 0, -0.2000025F, 0};
    float matrix[16] {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, -10, 1};
    const auto begin = [&] {
        ImGui::NewFrame();
        ImGuizmo::BeginFrame();
        ImGuizmo::SetDrawlist(ImGui::GetBackgroundDrawList());
        ImGuizmo::SetOrthographic(false);
        ImGuizmo::SetRect(0, 0, 800, 600);
    };
    const auto manipulate = [&] {
        return ImGuizmo::Manipulate(view, projection, ImGuizmo::TRANSLATE, ImGuizmo::WORLD, matrix, nullptr, nullptr);
    };

    io.AddMousePosEvent(400, 300);
    begin();
    ImGuizmo::SetID(1);
    manipulate();
    ImGui::Render();

    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    begin();
    manipulate();
    Require(ImGuizmo::IsUsingAny(), "Gizmo test did not start a drag");
    ImGui::Render();

    // The selected object streams out: no Manipulate call gets its mouse-up.
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    begin();
    KCDC::Features::Prop::FinishReleasedGizmoDrag();
    Require(!ImGuizmo::IsUsingAny(), "A streamed-out object's drag stayed active");
    ImGui::Render();

    // A pivot behind the camera must not leave a clip rectangle on the shared
    // draw list (fixed in the vendored ImGuizmo).
    begin();
    matrix[14]          = 100000;
    auto *draw          = ImGui::GetBackgroundDrawList();
    const int clipDepth = draw->_ClipRectStack.Size;
    manipulate();
    Require(draw->_ClipRectStack.Size == clipDepth, "Offscreen gizmo leaked a clip rectangle");
    matrix[14] = -10;
    ImGuizmo::SetID(2);
    const int vertices = draw->VtxBuffer.Size;
    manipulate();
    Require(draw->VtxBuffer.Size > vertices, "Nearby gizmo did not draw after the distant object");
    ImGui::Render();

    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    begin();
    manipulate();
    Require(ImGuizmo::IsUsing(), "Nearby gizmo could not start a new drag");
    KCDC::Features::Prop::FinishReleasedGizmoDrag(true);
    Require(!ImGuizmo::IsUsingAny(), "Closing the editor did not release the gizmo");
    ImGui::Render();
    ImGui::DestroyContext();
}
