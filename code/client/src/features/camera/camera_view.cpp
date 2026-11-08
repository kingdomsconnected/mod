#include "camera_view.h"

#include "sdk/cryengine/action/c_view.h"
#include "sdk/cryengine/action/c_view_system.h"
#include "sdk/cryengine/action/i_game_framework.h"
#include "sdk/cryengine/renderer/i_renderer.h"
#include "sdk/cryengine/system/c_camera_native.h"
#include "sdk/cryengine/system/i_system.h"

#include <algorithm>
#include <cmath>

namespace KCDC::Features::Camera {
    namespace {
        constexpr float kPi           = 3.14159265358979323846F;
        constexpr float kRadToDegrees = 180.0F / kPi;
        constexpr float kDegreesToRad = kPi / 180.0F;

        SDK::Vec3 Column(const SDK::Matrix34 &matrix, int column) {
            return SDK::Vec3 {matrix.m[0][column], matrix.m[1][column], matrix.m[2][column]};
        }

        SDK::Vec3 Normalized(const SDK::Vec3 &value, const SDK::Vec3 &fallback) {
            const float length = std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
            return length > 1e-6F ? SDK::Vec3 {value.x / length, value.y / length, value.z / length} : fallback;
        }
    } // namespace

    SDK::CView *ActiveView() {
        auto *framework  = SDK::IGameFramework::GetInstance();
        auto *viewSystem = framework != nullptr ? static_cast<SDK::CViewSystem *>(framework->GetIViewSystem()) : nullptr;
        return viewSystem != nullptr ? static_cast<SDK::CView *>(viewSystem->GetActiveView()) : nullptr;
    }

    bool ReadViewPose(ViewPose &pose) {
        const SDK::CView *view = ActiveView();
        if (view == nullptr) {
            return false;
        }

        const SDK::CCamera *camera = view->GetRenderCamera();
        const SDK::Matrix34 &matrix = camera->GetMatrix();

        pose.position = camera->GetPosition();
        pose.right    = Normalized(Column(matrix, 0), SDK::Vec3 {1.0F, 0.0F, 0.0F});
        pose.forward  = Normalized(Column(matrix, 1), SDK::Vec3 {0.0F, 1.0F, 0.0F});
        pose.up       = Normalized(Column(matrix, 2), SDK::Vec3 {0.0F, 0.0F, 1.0F});

        // Read back rather than assumed: Hor+ adaptation and the window's
        // aspect both move the frustum the renderer actually used.
        if (camera->GetFov() > 0.01F) {
            pose.fovDegrees = camera->GetFov() * kRadToDegrees;
        }
        if (camera->GetProjectionRatio() > 0.01F) {
            pose.aspectRatio = camera->GetProjectionRatio();
        }
        return true;
    }

    SDK::Vec3 RayThroughNdc(const ViewPose &pose, float ndcX, float ndcY) {
        const float tanHalf = std::tan(std::clamp(pose.fovDegrees, 5.0F, 170.0F) * kDegreesToRad * 0.5F);
        const float scaleX  = ndcX * tanHalf * std::max(pose.aspectRatio, 0.1F);
        const float scaleY  = ndcY * tanHalf;

        return Normalized(SDK::Vec3 {pose.forward.x + pose.right.x * scaleX + pose.up.x * scaleY, pose.forward.y + pose.right.y * scaleX + pose.up.y * scaleY, pose.forward.z + pose.right.z * scaleX + pose.up.z * scaleY}, pose.forward);
    }

    bool ProjectToScreen(const SDK::Vec3 &point, ScreenPoint &screen) {
        float percentX = 0.0F, percentY = 0.0F, depth = 0.0F;
        screen.onScreen = SDK::IRenderer::GetInstance()->ProjectToScreen(point.x, point.y, point.z, &percentX, &percentY, &depth);

        // The native's false is "behind the eye or past an edge". Past an
        // edge the point still has a place on the frame; behind the eye the
        // projection mirrors through it, so ask the same camera which it is.
        if (!screen.onScreen) {
            const SDK::CCamera &camera = SDK::ISystem::GetInstance()->GetViewCamera();
            const SDK::Vec3 eye        = camera.GetPosition();
            const SDK::Vec3 forward    = Column(camera.GetMatrix(), 1);
            if ((point.x - eye.x) * forward.x + (point.y - eye.y) * forward.y + (point.z - eye.z) * forward.z <= 0.0F) {
                return false;
            }
        }

        screen.x = percentX * 0.01F;
        screen.y = percentY * 0.01F;
        return std::isfinite(screen.x) && std::isfinite(screen.y);
    }
} // namespace KCDC::Features::Camera
