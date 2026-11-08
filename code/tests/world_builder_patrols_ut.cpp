#include "features/prop/prop_editor.h"
#include "features/prop/prop_editor_gizmo.h"
#include <stdexcept>
namespace {
    void Require(bool condition, const char *message) {
        if (!condition) {
            throw std::runtime_error(message);
        }
    }

    class PatrolEditorTest final {
      public:
        KCDC::Features::Prop::EditorPatrols editor;
        KCDC::Features::Prop::EditorSnapshot snapshot;
        bool groundEnabled = false;
        std::string clipboard;

        PatrolEditorTest() {
            ImGui::CreateContext();
            ImGui::GetPlatformIO().Platform_ClipboardUserData  = &clipboard;
            ImGui::GetPlatformIO().Platform_SetClipboardTextFn = [](ImGuiContext *, const char *text) { *static_cast<std::string *>(ImGui::GetPlatformIO().Platform_ClipboardUserData) = text; };
            auto &io                        = ImGui::GetIO();
            io.IniFilename                  = nullptr;
            io.DisplaySize                  = ImVec2(800, 600);
            io.DeltaTime                    = 1.0F / 60.0F;
            io.ConfigInputTrickleEventQueue = false;
            unsigned char *pixels           = nullptr;
            int width = 0, height = 0;
            io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
            ImGuizmo::SetImGuiContext(ImGui::GetCurrentContext());
            snapshot.level               = "patrol-editor-test";
            snapshot.cameraHeld          = true;
            snapshot.camera.position     = {0, 0, 20};
            snapshot.camera.pitchDegrees = -45;
            snapshot.camera.aspectRatio  = 800.0F / 600.0F;
        }

        ~PatrolEditorTest() {
            KCDC::Features::Prop::FinishReleasedGizmoDrag(true);
            ImGui::DestroyContext();
        }

        ImVec2 Screen(const glm::vec3 &point) const {
            float x = 0, y = 0;
            Require(KCDC::Features::Camera::ProjectToNdc(snapshot.camera, {point.x, point.y, point.z}, x, y), "Test point was behind the camera");
            return {(x + 1) * 400, (1 - y) * 300};
        }

        bool Frame(const ImVec2 &mouse, bool down = false, bool shift = false, bool pick = false, bool interactive = false) {
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
            ImGui::Begin("Patrols", nullptr, (interactive ? 0 : ImGuiWindowFlags_NoInputs) | ImGuiWindowFlags_NoSavedSettings);
            std::string notice;
            editor.RenderTab(snapshot, notice);
            ImGui::End();
            std::vector<KCDC::Features::Gizmos::Drawing> lines;
            editor.RenderOverlay(snapshot, lines);
            float view[16], projection[16];
            KCDC::Features::Camera::BuildViewMatrix(snapshot.camera, view);
            KCDC::Features::Camera::BuildProjectionMatrix(snapshot.camera, 0.1F, 10000, projection);
            const auto ground = [&](std::string_view, glm::vec3 &position, bool moved, float, bool) {
                if (!groundEnabled || !moved || !io.KeyShift || !ImGuizmo::IsUsingAny()) {
                    return false;
                }
                position.z = 2;
                return true;
            };
            const bool drawn = editor.RenderGizmo(view, projection, nullptr, ground);
            if (pick) {
                Require(editor.Pick(mouse, snapshot), "Patrols tab did not consume selection");
            }
            ImGui::Render();
            return drawn;
        }
    };
} // namespace

void TestWorldBuilderGroundSnap() {
    using namespace KCDC::Features::Prop;
    EditorGroundSnap snap;
    EditorGroundResult result;
    glm::vec3 point(1, 2, 10);
    std::uint64_t id = 0;
    glm::vec3 queried;
    const auto probe = [&](std::uint64_t request, const glm::vec3 &position) {
        id      = request;
        queried = position;
    };
    const auto update = [&](const char *key, bool moved, bool dragging, bool shift, float foot = 0) { return snap.Update(key, point, moved, dragging, shift, foot, result, probe); };
    Require(!update("point", true, true, true) && snap.Pending() && queried == point, "Shift translation queues a ground probe");
    const auto first = id;
    point.x          = 4;
    update("point", true, true, true);
    Require(id == first, "Only one raycast outstanding");
    result = {first, {1, 2, 3}, true};
    Require(update("point", false, true, true) && point == glm::vec3(4, 2, 3) && id != first, "Ground reply keeps latest horizontal drag and probes it next");
    result = {id, {4, 2, 5}, true};
    Require(update("point", false, false, false, 2) && point.z == 7 && !snap.Pending(), "Mouse-up settles the final reply with object foot offset");
    update("point", true, true, true);
    result = {id, {4, 2, 90}, true};
    Require(!update("different", false, false, false) && point.z == 7 && !snap.Pending(), "Selection changes discard late ground replies");
    update("different", true, true, true);
    result = {id, {4, 2, 90}, true};
    Require(!update("different", true, true, false) && point.z == 7 && !snap.Pending(), "Releasing Shift during a drag cancels snapping");
    update("different", true, true, true);
    result  = {id, {4, 2, 90}, true};
    point.z = 11;
    Require(!update("different", false, false, false) && point.z == 11, "Numeric edit supersedes pending ground probe");
    update("different", true, true, true);
    result = {id, {4, 2, 90}, false};
    Require(!update("different", false, false, false) && point.z == 11 && !snap.Pending(), "Missing ground leaves position intact");
    update("different", true, true, true);
    update("different", false, false, false);
    result = {id, {4, 2, 90}, true};
    Require(!update("different", true, true, true) && point.z == 11, "New drag discards the previous gesture ground reply");
    snap.BeginFrame();
    snap.EndFrame();
    Require(!snap.Pending(), "Closing or switching gizmo cancels pending snap");
    snap.Update("created-area", point, false, false, false, 4, result, probe, true);
    Require(snap.Pending() && queried == point, "Grounded creation queues a probe without a gizmo drag");
    result = {id, {point.x, point.y, 3}, true};
    Require(snap.Update("created-area", point, false, false, false, 4, result, probe) && point.z == 7 && !snap.Pending(), "Creation applies its bottom offset after releasing Shift");
}

void TestWorldBuilderPatrolEditor() {
    TestWorldBuilderGroundSnap();
    PatrolEditorTest test;
    KCDC::Shared::Game::PatrolRouteDefinition route;
    route.id     = "guard";
    route.points = {{{-5, 20, 0}}, {{5, 20, 0}}, {{5, 30, 0}}};
    test.editor.Import({route}, true);
    std::shared_ptr<const KCDC::Shared::Game::PatrolRouteDefinition> preview;
    std::string role;
    Require(test.editor.TakePreviewRequest(preview, role) && !preview, "Import must stop preview");
    const auto point = test.Screen(route.points[0].position);
    test.Frame(point, false, false, true);
    Require(test.editor.SelectedDocument().patrolRoutes.size() == 1, "Point selects its route");
    Require(test.Frame(point), "Selected waypoint draws a gizmo");
    test.Frame(point, true);
    Require(ImGuizmo::IsUsingAny(), "Waypoint gizmo starts a drag");
    test.Frame({point.x + 40, point.y}, true);
    Require(test.editor.Authored()[0].points[0].position != route.points[0].position && test.editor.Authored()[0].points[1].position == route.points[1].position, "Drag moves only selected point");
    Require(test.editor.TakePreviewRequest(preview, role) && !preview, "Drag stops preview");
    test.Frame({point.x + 40, point.y}, false);
    test.editor.Import({route}, true);
    test.Frame(point, false, false, true);
    test.groundEnabled = true;
    test.Frame(point, true, true);
    Require(ImGuizmo::IsUsingAny(), "Shift permits starting a waypoint translation");
    test.Frame({point.x + 40, point.y}, true, true);
    Require(test.editor.Authored()[0].points[0].position.z == 2, "Waypoint translation applies ground snap");
    test.groundEnabled = false;
    test.editor.DeleteSelected();
    Require(test.editor.Authored()[0].points.size() == 2 && !ImGuizmo::IsUsingAny(), "Delete removes selected waypoint and releases gizmo");
    test.editor.Import({route}, true);
    const auto middle = test.Screen({0, 20, 0});
    test.Frame(middle, false);
    test.Frame(middle, true, false, true);
    test.Frame(middle, false);
    test.Frame(middle, true, false, true);
    Require(test.editor.Authored()[0].points.size() == 4 && glm::length(test.editor.Authored()[0].points[1].position - glm::vec3(0, 20, 0)) < 0.01F, "Double click splits edge at cursor");
    test.Frame(middle, false);
    test.editor.DuplicateSelected();
    Require(test.editor.Authored().size() == 2 && test.editor.Authored()[0].id != test.editor.Authored()[1].id, "Duplicate has unique id");
    test.editor.Import({route}, false);
    Require(test.editor.Authored().size() == 3 && test.editor.Authored()[2].id != "guard", "Append remaps duplicate id");
    test.editor.Import({route}, true);
    test.Frame(point, false, false, true);
    test.editor.ClearPoint();
    test.editor.DeleteSelected();
    Require(test.editor.Authored().empty(), "Route selection deletes whole route");
    route.points.resize(1);
    test.editor.Import({route}, true);
    test.Frame(point, false, false, true);
    test.editor.DeleteSelected();
    Require(test.editor.Authored().empty(), "Deleting last waypoint removes route");
    test.snapshot.hasPlayer          = true;
    test.snapshot.playerPosition     = {500, 500, 500};
    test.snapshot.cameraGround.valid = true;
    test.snapshot.cameraGround.point = {1, 12, 3};
    test.Frame({30, 35}, false, false, false, true);
    test.Frame({30, 35}, true, false, false, true);
    test.Frame({30, 35}, false, false, false, true);
    Require(test.editor.Authored().size() == 1, "New route button creates a route");
    test.Frame({50, 178}, false, false, false, true);
    test.Frame({50, 178}, true, false, false, true);
    test.Frame({50, 178}, false, false, false, true);
    Require(test.clipboard.find("PatrolRoute.create(") != std::string::npos && test.clipboard.find("pingPong") != std::string::npos, "Copy API call exports the selected patrol route to clipboard");
    Require(test.editor.Authored()[0].mode == KCDC::Shared::Game::PatrolMode::PingPong, "New editor route defaults to ping pong");
    Require(test.editor.Authored()[0].points[0].position == glm::vec3(1, 12, 3), "New route uses camera ground hit, not player position");
}
