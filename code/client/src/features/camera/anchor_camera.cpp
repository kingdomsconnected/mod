#include "anchor_camera.h"

#include "features/camera/camera_view.h"

#include <logging/logger.h>

#include <algorithm>
#include <cmath>

#include "sdk/cryengine/action/c_view.h"
#include "sdk/cryengine/action/c_view_system.h"
#include "sdk/cryengine/action/i_game_framework.h"
#include "sdk/cryengine/action/s_camera_params.h"
#include "sdk/cryengine/entitysystem/i_entity.h"
#include "sdk/cryengine/entitysystem/i_entity_class_registry.h"
#include "sdk/cryengine/entitysystem/i_entity_system.h"
#include "sdk/cryengine/movie/i_movie_user.h"
#include "sdk/cryengine/system/c_camera_native.h"

namespace KCDC::Features::Camera {
    namespace {
        constexpr float kPi           = 3.14159265358979323846F;
        constexpr float kDegreesToRad = kPi / 180.0F;
        constexpr float kRadToDegrees = 180.0F / kPi;

        // Level with the horizon at the poles rather than tumbling past them.
        constexpr float kPitchLimit = 89.0F;

        constexpr float kMinSpeed = 0.05F;
        constexpr float kMaxSpeed = 400.0F;

        // The near plane the anchor view asks for. Small enough to fly right
        // up to a wall and still see a gizmo on something behind it.
        constexpr float kNearPlane = 0.05F;

        SDK::CViewSystem *ViewSystem() {
            auto *framework = SDK::IGameFramework::GetInstance();
            return framework != nullptr ? static_cast<SDK::CViewSystem *>(framework->GetIViewSystem()) : nullptr;
        }

        float Length(const SDK::Vec3 &value) {
            return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
        }

        SDK::Vec3 Normalized(const SDK::Vec3 &value) {
            const float length = Length(value);
            return length > 1e-6F ? SDK::Vec3 {value.x / length, value.y / length, value.z / length} : SDK::Vec3 {0.0F, 1.0F, 0.0F};
        }

        float Dot(const SDK::Vec3 &a, const SDK::Vec3 &b) {
            return a.x * b.x + a.y * b.y + a.z * b.z;
        }

        // Keeps a yaw readable after a few minutes of spinning, and changes
        // nothing about the direction it names.
        float WrapDegrees(float degrees) {
            return degrees > 180.0F || degrees < -180.0F ? degrees - std::floor((degrees + 180.0F) / 360.0F) * 360.0F : degrees;
        }
    } // namespace

    SDK::Vec3 PoseForward(const CameraPose &pose) {
        const float yaw   = pose.yawDegrees * kDegreesToRad;
        const float pitch = pose.pitchDegrees * kDegreesToRad;
        const float flat  = std::cos(pitch);

        // Zero yaw faces +Y, which is where SetRotationXYZ leaves an identity
        // rotation, so the anchor entity and this basis agree by construction.
        return SDK::Vec3 {-std::sin(yaw) * flat, std::cos(yaw) * flat, std::sin(pitch)};
    }

    SDK::Vec3 PoseRight(const CameraPose &pose) {
        const float yaw = pose.yawDegrees * kDegreesToRad;
        return SDK::Vec3 {std::cos(yaw), std::sin(yaw), 0.0F};
    }

    SDK::Vec3 PoseUp(const CameraPose &pose) {
        const SDK::Vec3 forward = PoseForward(pose);
        const SDK::Vec3 right   = PoseRight(pose);
        return SDK::Vec3 {right.y * forward.z - right.z * forward.y, right.z * forward.x - right.x * forward.z, right.x * forward.y - right.y * forward.x};
    }

    SDK::Quat PoseRotation(const CameraPose &pose) {
        return SDK::QuatFromAngles(SDK::Ang3 {pose.pitchDegrees * kDegreesToRad, 0.0F, pose.yawDegrees * kDegreesToRad});
    }

    void PoseLookAlong(CameraPose &pose, const SDK::Vec3 &direction) {
        const SDK::Vec3 forward = Normalized(direction);
        pose.yawDegrees         = std::atan2(-forward.x, forward.y) * kRadToDegrees;
        pose.pitchDegrees       = std::asin(std::clamp(forward.z, -1.0F, 1.0F)) * kRadToDegrees;
    }

    void BuildViewMatrix(const CameraPose &pose, float view[16]) {
        const SDK::Vec3 forward = PoseForward(pose);
        const SDK::Vec3 right   = PoseRight(pose);
        const SDK::Vec3 up      = PoseUp(pose);

        // Column-major, and the eye looks down -Z: the third row is the
        // negated view direction, which is the one place this differs from
        // the engine's own camera matrix.
        view[0]  = right.x;
        view[4]  = right.y;
        view[8]  = right.z;
        view[12] = -Dot(right, pose.position);
        view[1]  = up.x;
        view[5]  = up.y;
        view[9]  = up.z;
        view[13] = -Dot(up, pose.position);
        view[2]  = -forward.x;
        view[6]  = -forward.y;
        view[10] = -forward.z;
        view[14] = Dot(forward, pose.position);
        view[3]  = 0.0F;
        view[7]  = 0.0F;
        view[11] = 0.0F;
        view[15] = 1.0F;
    }

    void BuildProjectionMatrix(const CameraPose &pose, float nearPlane, float farPlane, float projection[16]) {
        for (int index = 0; index < 16; ++index) {
            projection[index] = 0.0F;
        }

        const float focal  = 1.0F / std::tan(std::clamp(pose.fovDegrees, 5.0F, 170.0F) * kDegreesToRad * 0.5F);
        const float aspect = std::max(pose.aspectRatio, 0.1F);

        projection[0]  = focal / aspect;
        projection[5]  = focal;
        projection[10] = (farPlane + nearPlane) / (nearPlane - farPlane);
        projection[11] = -1.0F;
        projection[14] = 2.0F * farPlane * nearPlane / (nearPlane - farPlane);
    }

    SDK::Vec3 RayThroughNdc(const CameraPose &pose, float ndcX, float ndcY) {
        const SDK::Vec3 forward = PoseForward(pose);
        const SDK::Vec3 right   = PoseRight(pose);
        const SDK::Vec3 up      = PoseUp(pose);

        const float tanHalf = std::tan(std::clamp(pose.fovDegrees, 5.0F, 170.0F) * kDegreesToRad * 0.5F);
        const float scaleX  = ndcX * tanHalf * std::max(pose.aspectRatio, 0.1F);
        const float scaleY  = ndcY * tanHalf;

        return Normalized(SDK::Vec3 {forward.x + right.x * scaleX + up.x * scaleY, forward.y + right.y * scaleX + up.y * scaleY, forward.z + right.z * scaleX + up.z * scaleY});
    }

    bool ProjectToNdc(const CameraPose &pose, const SDK::Vec3 &point, float &ndcX, float &ndcY) {
        const SDK::Vec3 forward = PoseForward(pose);
        const SDK::Vec3 right   = PoseRight(pose);
        const SDK::Vec3 up      = PoseUp(pose);
        const SDK::Vec3 offset {point.x - pose.position.x, point.y - pose.position.y, point.z - pose.position.z};

        const float depth = Dot(offset, forward);
        if (depth <= kNearPlane) {
            return false;
        }

        const float tanHalf = std::tan(std::clamp(pose.fovDegrees, 5.0F, 170.0F) * kDegreesToRad * 0.5F);
        ndcX                = Dot(offset, right) / (depth * tanHalf * std::max(pose.aspectRatio, 0.1F));
        ndcY                = Dot(offset, up) / (depth * tanHalf);
        return true;
    }

    AnchorCamera *AnchorCamera::_holder = nullptr;

    bool AnchorCamera::Take() {
        if (_anchorEntityId != 0) {
            return true;
        }

        if (_holder != nullptr) {
            return false;
        }

        auto *viewSystem   = ViewSystem();
        auto *entitySystem = SDK::IEntitySystem::GetInstance();
        if (viewSystem == nullptr || entitySystem == nullptr) {
            return false;
        }

        // Start exactly where the player was looking, so taking the camera
        // reads as stopping rather than as a cut.
        if (const SDK::CView *view = KCDC::Features::Camera::ActiveView()) {
            const SDK::CCamera *camera = view->GetRenderCamera();
            _pose.position             = camera->GetPosition();
            PoseLookAlong(_pose, SDK::Vec3 {camera->GetMatrix().m[0][1], camera->GetMatrix().m[1][1], camera->GetMatrix().m[2][1]});
            _pose.fovDegrees     = camera->GetFov() * kRadToDegrees;
            _requestedFovDegrees = camera->GetSourceFov() > 0.01F ? camera->GetSourceFov() * kRadToDegrees : _pose.fovDegrees;
            _pose.aspectRatio    = camera->GetProjectionRatio() > 0.01F ? camera->GetProjectionRatio() : _pose.aspectRatio;
        }

        auto *registry                 = entitySystem->GetClassRegistry();
        SDK::IEntityClass *entityClass = registry != nullptr ? registry->FindClass(SDK::EntityClassNames::Default) : nullptr;
        if (entityClass == nullptr && registry != nullptr) {
            entityClass = registry->FindClass(SDK::EntityClassNames::BasicEntity);
        }
        if (entityClass == nullptr) {
            return false;
        }

        SDK::SEntitySpawnParams spawn {};
        spawn.entityClass = entityClass;
        spawn.name        = "KCDC_AnchorCamera";
        spawn.position    = _pose.position;
        spawn.rotation    = PoseRotation(_pose);

        SDK::IEntity *anchor = entitySystem->SpawnEntity(spawn, true);
        if (anchor == nullptr) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("The free camera could not spawn its anchor");
            return false;
        }
        _anchorEntityId = anchor->GetId();
        _holder         = this;

        Activate();
        Apply();
        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("The free camera took the view on anchor entity {}", _anchorEntityId);
        return true;
    }

    void AnchorCamera::Release() {
        if (_anchorEntityId == 0) {
            return;
        }

        if (auto *viewSystem = ViewSystem()) {
            // Entity id 0 is what hands the view back to whatever held it
            // before the sequence started.
            SDK::SCameraParams params {};
            params.justActivated        = true;
            params.referenceAspectRatio = 1.0F;
            static_cast<SDK::IMovieUser *>(viewSystem)->SetActiveCamera(params);

            // A cut, not a glide. Activate never arms the blend-out, but a
            // take made while a sequence already held the view inherits that
            // sequence's. The retail SViewParams::UpdateBlending seeds its
            // rotation offset with a component-wise divide,
            // rotationLast[i] / rotation[i], so any zero component in the
            // resumed view -- a player who has not pitched since spawning
            // has two -- makes the offset NaN, and the slerp that should
            // decay it keeps it NaN for good.
            if (SDK::CView *view = KCDC::Features::Camera::ActiveView(); view != nullptr && view->GetLinkedEntityIdRaw() != _anchorEntityId) {
                view->GetViewParams().blend = false;
            }
        }

        if (auto *entitySystem = SDK::IEntitySystem::GetInstance()) {
            entitySystem->RemoveEntity(_anchorEntityId, false);
        }

        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("The free camera handed the view back");
        _anchorEntityId = 0;
        if (_holder == this) {
            _holder = nullptr;
        }
    }

    void AnchorCamera::Reclaim() {
        if (_anchorEntityId != 0 && !OwnsActiveView()) {
            Apply();
        }
    }

    void AnchorCamera::SetSpeed(float metresPerSecond) {
        _speed = std::clamp(metresPerSecond, kMinSpeed, kMaxSpeed);
    }

    void AnchorCamera::SetFieldOfView(float degrees) {
        _requestedFovDegrees = std::clamp(degrees, 20.0F, 140.0F);
        Apply();
    }

    float AnchorCamera::GetRequestedFieldOfView() const {
        return _requestedFovDegrees;
    }

    void AnchorCamera::SetPosition(const SDK::Vec3 &position) {
        if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z)) {
            return;
        }
        _pose.position = position;
        Apply();
    }

    void AnchorCamera::SetPose(const SDK::Vec3 &position, const SDK::Vec3 &forward) {
        if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z)) {
            return;
        }
        if (Length(forward) < 1e-4F) {
            return;
        }

        _pose.position = position;
        PoseLookAlong(_pose, forward);
        _pose.pitchDegrees = std::clamp(_pose.pitchDegrees, -kPitchLimit, kPitchLimit);
        Apply();
    }

    void AnchorCamera::SetRotation(float yawDegrees, float pitchDegrees) {
        if (!std::isfinite(yawDegrees) || !std::isfinite(pitchDegrees)) {
            return;
        }
        _pose.yawDegrees   = WrapDegrees(yawDegrees);
        _pose.pitchDegrees = std::clamp(pitchDegrees, -kPitchLimit, kPitchLimit);
        Apply();
    }

    void AnchorCamera::Focus(const SDK::Vec3 &target, float distance) {
        const SDK::Vec3 forward = PoseForward(_pose);
        const float back        = std::max(distance, 1.0F);
        _pose.position          = SDK::Vec3 {target.x - forward.x * back, target.y - forward.y * back, target.z - forward.z * back};
        Apply();
    }

    void AnchorCamera::Fly(const FlyInput &input, float deltaSeconds) {
        if (_anchorEntityId == 0) {
            return;
        }

        _pose.yawDegrees += input.yawDelta;
        _pose.pitchDegrees = std::clamp(_pose.pitchDegrees + input.pitchDelta, -kPitchLimit, kPitchLimit);

        _pose.yawDegrees = WrapDegrees(_pose.yawDegrees);

        float speed = _speed;
        if (input.boost) {
            speed *= 6.0F;
        }
        if (input.crawl) {
            speed *= 0.2F;
        }
        const float step = speed * std::clamp(deltaSeconds, 0.0F, 0.1F);

        const SDK::Vec3 forward = PoseForward(_pose);
        const SDK::Vec3 right   = PoseRight(_pose);

        // Lift is along world Z rather than the view's up, which is what makes
        // Space and Ctrl rise and fall rather than drift while looking down.
        _pose.position.x += (forward.x * input.forward + right.x * input.strafe) * step;
        _pose.position.y += (forward.y * input.forward + right.y * input.strafe) * step;
        _pose.position.z += (forward.z * input.forward + right.z * input.strafe + input.lift) * step;

        Apply();
    }

    void AnchorCamera::Activate() {
        auto *viewSystem = ViewSystem();
        if (viewSystem == nullptr || _anchorEntityId == 0) {
            return;
        }

        // No blend-out. The view system latches it from the take that starts
        // the sequence and honours it on every later SetActiveCamera(0) --
        // and the engine sends one of its own each time an entity layer
        // finishes streaming in (CEntityLayer::OnStreamLoadFinished ->
        // PlayOnLoadSequences), which goes on for seconds after the loading
        // screen drops. Each would blend the player's view out of this one
        // through the retail divide Release describes: a NaN view camera, a
        // frozen frame, and the AI LOD queue sorted on NaN distances until
        // its worker reads off the end of it.
        SDK::SCameraParams params {};
        params.cameraEntityId       = _anchorEntityId;
        params.fov                  = _requestedFovDegrees * kDegreesToRad;
        params.nearZ                = kNearPlane;
        params.justActivated        = true;
        params.performBlendOut      = false;
        params.referenceAspectRatio = 1.0F;
        static_cast<SDK::IMovieUser *>(viewSystem)->SetActiveCamera(params);
    }

    bool AnchorCamera::OwnsActiveView() const {
        const SDK::CView *view = KCDC::Features::Camera::ActiveView();
        return view != nullptr && _anchorEntityId != 0 && view->GetLinkedEntityIdRaw() == _anchorEntityId;
    }

    void AnchorCamera::Apply() {
        if (_anchorEntityId == 0) {
            return;
        }

        auto *entitySystem   = SDK::IEntitySystem::GetInstance();
        SDK::IEntity *anchor = entitySystem != nullptr ? entitySystem->GetEntity(_anchorEntityId) : nullptr;
        if (anchor == nullptr) {
            // The level went away underneath us; the view goes with it, and
            // so must the claim on it, or nothing could take the camera again.
            _anchorEntityId = 0;
            if (_holder == this) {
                _holder = nullptr;
            }
            return;
        }

        anchor->SetPos(_pose.position);
        anchor->SetRotation(PoseRotation(_pose));

        // The game hands the view around on its own -- a dialogue, a cutscene,
        // a save load. Asking for it back whenever it is not ours is cheaper
        // than listening for every way it can be taken, and it is what stops
        // the editor from silently ending up as a camera nobody is looking
        // through.
        if (!OwnsActiveView()) {
            Activate();
            if (!OwnsActiveView()) {
                return;
            }
        }

        SDK::CView *view         = KCDC::Features::Camera::ActiveView();
        SDK::SViewParams &params = view->GetViewParams();

        // Blending is what the engine's own camera cuts want and what a camera
        // being flown does not: with it on, the view trails the anchor by a
        // frame and the gizmo lands off the object.
        params.blend = false;
        params.fov   = _requestedFovDegrees * kDegreesToRad;

        RefreshLens();
    }

    void AnchorCamera::RefreshLens() {
        // The frustum the renderer settled on, not the one that was asked
        // for: `Hor+` adaptation and the window's aspect both change it, and
        // a gizmo drawn against the wrong one misses the object it is on.
        const SDK::CView *view = KCDC::Features::Camera::ActiveView();
        if (view == nullptr) {
            return;
        }

        const SDK::CCamera *camera = view->GetRenderCamera();
        if (camera->GetFov() > 0.01F) {
            _pose.fovDegrees = camera->GetFov() * kRadToDegrees;
        }
        if (camera->GetProjectionRatio() > 0.01F) {
            _pose.aspectRatio = camera->GetProjectionRatio();
        }
    }
} // namespace KCDC::Features::Camera
