#include "features/prop/prop_editor.h"
#include "features/prop/prop_editor_gizmo.h"
#include "sdk/cryengine/action/i_game_framework.h"
#include "sdk/cryengine/entitysystem/i_entity_system.h"
#include "sdk/cryengine/renderer/i_renderer.h"
#include "sdk/cryengine/system/i_system.h"
#include "sdk/patterns.h"

#include <ImGuizmo.h>

#include <stdexcept>

// The headless editor uses the real camera and matrix math. Native camera
// ownership is unavailable here and must never be requested by these tests.
namespace SDK {
    IGameFramework *IGameFramework::GetInstance() {
        throw std::runtime_error("Headless area editor requested the game framework");
    }

    IEntitySystem *IEntitySystem::GetInstance() {
        throw std::runtime_error("Headless area editor requested the entity system");
    }

    IRenderer *IRenderer::GetInstance() {
        throw std::runtime_error("Headless area editor requested the renderer");
    }

    ISystem *ISystem::GetInstance() {
        throw std::runtime_error("Headless area editor requested the system");
    }
} // namespace SDK

namespace {
    void Require(bool condition, const char *message) {
        if (!condition) {
            throw std::runtime_error(message);
        }
    }

    class AreaEditorTest final {
      public:
        KCDC::Features::Prop::EditorAreas editor;
        KCDC::Features::Prop::EditorSnapshot snapshot;
        bool groundEnabled = false;

        AreaEditorTest() {
            ImGui::CreateContext();
            auto &io                        = ImGui::GetIO();
            io.IniFilename                  = nullptr;
            io.DisplaySize                  = ImVec2(800, 600);
            io.DeltaTime                    = 1.0F / 60.0F;
            io.ConfigInputTrickleEventQueue = false;
            unsigned char *pixels           = nullptr;
            int width = 0, height = 0;
            io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
            ImGuizmo::SetImGuiContext(ImGui::GetCurrentContext());
            snapshot.level               = "area-editor-test";
            snapshot.cameraHeld          = true;
            snapshot.camera.position     = {0, 0, 20};
            snapshot.camera.pitchDegrees = -45;
            snapshot.camera.aspectRatio  = 800.0F / 600.0F;
        }

        ~AreaEditorTest() {
            KCDC::Features::Prop::FinishReleasedGizmoDrag(true);
            ImGui::DestroyContext();
        }

        ImVec2 Screen(const glm::vec3 &point) const {
            float x = 0, y = 0;
            Require(KCDC::Features::Camera::ProjectToNdc(snapshot.camera, {point.x, point.y, point.z}, x, y), "Test point was behind the camera");
            return {(x + 1) * 400, (1 - y) * 300};
        }

        bool Frame(const ImVec2 &mouse, bool down = false, bool shift = false, bool pick = false, int operation = 0, float gridStep = 0) {
            auto &io = ImGui::GetIO();
            io.AddMousePosEvent(mouse.x, mouse.y);
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, down);
            io.AddKeyEvent(ImGuiMod_Shift, shift);
            ImGui::NewFrame();
            ImGuizmo::BeginFrame();
            ImGuizmo::SetDrawlist(ImGui::GetBackgroundDrawList());
            ImGuizmo::SetOrthographic(false);
            ImGuizmo::SetRect(0, 0, 800, 600);
            editor.BeginFrame();
            ImGui::SetNextWindowPos({0, 0});
            ImGui::SetNextWindowSize({260, 600});
            ImGui::Begin("Areas", nullptr, ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings);
            std::string notice;
            editor.RenderTab(snapshot, notice);
            ImGui::End();
            std::vector<KCDC::Features::Gizmos::Drawing> lines;
            editor.RenderOverlay(snapshot, lines);
            float view[16], projection[16];
            KCDC::Features::Camera::BuildViewMatrix(snapshot.camera, view);
            KCDC::Features::Camera::BuildProjectionMatrix(snapshot.camera, 0.1F, 10000, projection);
            const auto ground = [&](std::string_view, glm::vec3 &position, bool moved, float footOffset, bool request) {
                if (!groundEnabled || (!request && (!moved || !io.KeyShift || !ImGuizmo::IsUsingAny()))) {
                    return false;
                }
                position.z = 2 + footOffset;
                return true;
            };
            const float snap[3] {gridStep, gridStep, gridStep};
            const bool drawn = editor.RenderGizmo(view, projection, operation, false, gridStep > 0 ? snap : nullptr, ground);
            if (pick) {
                Require(editor.Pick(mouse), "Areas tab did not consume selection");
            }
            ImGui::Render();
            return drawn;
        }
    };
} // namespace

void TestWorldBuilderAreaEditor() {
    AreaEditorTest test;
    KCDC::Shared::Game::AreaDefinition first;
    first.id = first.name = "first";
    first.shape.type      = KCDC::Shared::Game::AreaShapeType::Shape;
    first.shape.position  = {-4, 20, 0};
    first.shape.points    = {{-2, -2, 0}, {2, -2, 0}, {2, 2, 0}, {-2, 2, 0}};
    auto second           = first;
    second.id = second.name = "second";
    second.shape.position.x = 4;
    const std::vector<KCDC::Shared::Game::AreaDefinition> areas {first, second};
    test.editor.Import(areas, true, {{"first", "group-a"}, {"second", "group-b"}});
    const auto firstAnchor  = test.Screen(first.shape.position);
    const auto secondAnchor = test.Screen(second.shape.position);
    test.Frame(firstAnchor, false, false, true);
    Require(test.editor.SelectedDocument().areas.size() == 1 && test.editor.SelectedDocument().areas[0].id == "first", "Area marker selection failed");
    Require(!test.Frame(secondAnchor, false, true, true), "Shift selection still drew a mouse-blocking gizmo");
    const auto selected = test.editor.SelectedDocument();
    Require(selected.areas.size() == 2 && selected.areaGroups.size() == 2, "Shift selection or group export lost an area");
    test.Frame(secondAnchor, false, true, true);
    Require(test.editor.SelectedDocument().areas.size() == 1 && test.editor.SelectedDocument().areaGroups.size() == 1, "Shift deselection retained an unrelated area or group");

    const auto point = test.Screen(first.shape.position + first.shape.points[0]);
    test.Frame(point, false, false, true);
    Require(test.editor.IsEditingHandle(), "Point marker did not select a handle");
    test.editor.DeleteSelected();
    Require(test.editor.Authored().size() == 2 && test.editor.Authored()[0].shape.points.size() == 3, "Point deletion removed the whole quadrilateral");
    Require(!test.editor.IsEditingHandle(), "Deleted point left a stale handle");
    test.Frame(test.Screen(test.editor.Authored()[0].shape.WorldPoints()[0]), false, false, true);
    test.editor.DeleteSelected();
    Require(test.editor.Authored().size() == 1 && test.editor.Authored()[0].id == "second" && !test.editor.Groups().contains("first"), "Triangle deletion lost the wrong area or retained its group");
    Require(test.editor.SelectedDocument().areas.empty(), "Area deletion left a dangling selection");

    test.editor.Import(areas, true);
    test.Frame(firstAnchor, false, false, true);
    const auto edgePoint = test.Screen(first.shape.position + glm::mix(first.shape.points[0], first.shape.points[1], 0.25F));
    test.Frame(edgePoint, true, false, true);
    test.Frame(edgePoint);
    test.Frame(edgePoint, true);
    Require(test.editor.Authored()[0].shape.points.size() == 5, "Double-clicking an edge did not insert a point");
    const auto inserted = test.Screen(test.editor.Authored()[0].shape.WorldPoints()[1]);
    // ImGui rounds mouse positions to pixels before the editor sees them.
    Require(std::fabs(inserted.x - edgePoint.x) <= 1 && std::fabs(inserted.y - edgePoint.y) <= 1, "Double-click insertion lost the clicked edge position");
    test.Frame(edgePoint);

    const auto closingEdge = test.Screen(first.shape.position + glm::vec3(-2, 0, 0));
    test.Frame(closingEdge, false, false, true);
    test.editor.DeleteSelected();
    Require(test.editor.Authored().size() == 2 && test.editor.Authored()[0].shape.points.size() == 3, "Deleting an edge from the five-point shape removed the wrong selection");
    Require(test.editor.Authored()[0].shape.points.back() == first.shape.points[2], "Closing edge deletion retained its last endpoint");

    test.editor.Import(areas, true);
    Require(test.editor.SelectedDocument().areas.empty() && !test.editor.IsEditingHandle(), "Replacing areas retained selection state");
    test.Frame(firstAnchor, false, false, true);
    test.Frame(secondAnchor, false, true, true);
    const auto pivot = test.Screen({0, 20, 0});
    test.Frame(pivot, false, false, false, 2);
    test.Frame(pivot, true, false, false, 2);
    Require(ImGuizmo::IsUsingAny(), "Scale test did not begin a drag");
    test.Frame({pivot.x + 50, pivot.y}, true, false, false, 2);
    const auto once = test.editor.Authored()[0].shape;
    test.Frame({pivot.x + 100, pivot.y}, true, false, false, 2);
    Require(std::fabs(once.position.x + 6) < 0.001F && std::fabs(test.editor.Authored()[0].shape.position.x + 8) < 0.001F, "Successive scale updates compounded instead of scaling from the drag origin");
    Require(std::fabs(test.editor.Authored()[0].shape.points[0].x + 4) < 0.001F, "Group scale did not resize polygon points");
    test.editor.DuplicateSelected();
    Require(!ImGuizmo::IsUsingAny() && test.editor.Authored().size() == 4 && test.editor.SelectedDocument().areas.size() == 2, "Duplicating during a drag retained the old gizmo or selection");
    test.editor.DeleteSelected();
    Require(test.editor.Authored().size() == 2 && test.editor.Authored()[0].id == "first" && test.editor.Authored()[1].id == "second", "Deleting copied areas removed an original");
    test.editor.Import(areas, true);
    test.Frame(firstAnchor, false, false, true);
    test.groundEnabled = true;
    test.Frame(firstAnchor, true, true);
    Require(ImGuizmo::IsUsingAny(), "Shift permits starting an area translation");
    test.Frame({firstAnchor.x + 40, firstAnchor.y}, true, true);
    Require(test.editor.Authored()[0].shape.position.z == 2, "Whole area translation applies ground snap");
    test.Frame({firstAnchor.x + 40, firstAnchor.y}, false);
    test.editor.Import(areas, true);
    test.Frame(firstAnchor, false, false, true);
    const auto groundPoint = test.Screen(first.shape.position + first.shape.points[0]);
    test.Frame(groundPoint, false, false, true);
    test.Frame(groundPoint, true, true);
    Require(ImGuizmo::IsUsingAny(), "Shift permits starting an area point translation");
    test.Frame({groundPoint.x + 40, groundPoint.y}, true, true);
    Require(test.editor.Authored()[0].shape.WorldPoints()[0].z == 2, "Area point translation applies ground snap");
    test.Frame({groundPoint.x + 40, groundPoint.y});
    first.shape.type     = KCDC::Shared::Game::AreaShapeType::Sphere;
    first.shape.position = {0, 20, 10};
    first.shape.radius   = 4;
    second               = first;
    second.id = second.name = "second";
    second.shape.position = {4, 20, 12};
    test.editor.Import({first, second}, true);
    std::string error;
    Require(test.editor.Select({"first", "second"}, error), "Ground test group selection failed");
    const auto groupPivot = test.Screen({2, 20, 11});
    test.Frame(groupPivot, false, true);
    test.Frame(groupPivot, true, true);
    Require(ImGuizmo::IsUsingAny(), "Ground test group drag did not start");
    test.Frame({groupPivot.x + 40, groupPivot.y}, true, true);
    Require(std::fabs(KCDC::Features::Prop::AreaGroundFloor(test.editor.Authored()[0].shape) - 2) < 0.0001F, "Group ground snap must land the lowest area bottom");
    Require(std::fabs(test.editor.Authored()[1].shape.position.z - test.editor.Authored()[0].shape.position.z - 2) < 0.0001F, "Group ground snap must retain relative heights");
    test.Frame({groupPivot.x + 40, groupPivot.y});
    test.groundEnabled = false;
    test.editor.Import({first}, true);
    Require(test.editor.Select({"first"}, error), "Grid test selection failed");
    const auto gridPivot = test.Screen(first.shape.position);
    test.Frame(gridPivot);
    test.Frame(gridPivot, true, false, false, 0, 1.25F);
    Require(ImGuizmo::IsUsingAny(), "Grid test drag did not start");
    test.Frame({gridPivot.x + 47, gridPivot.y}, true, false, false, 0, 1.25F);
    const auto gridPosition = test.editor.Authored()[0].shape.position;
    Require(gridPosition.x != first.shape.position.x && std::fabs(gridPosition.x / 1.25F - std::round(gridPosition.x / 1.25F)) < 0.0001F, "Area translation must follow the supplied grid increment");
}
