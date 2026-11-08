#pragma once

#include "sdk/types.h"

namespace KCDC::Features::Camera {
    /**
     * Where the free camera is and what it can see from there. Yaw and
     * pitch in degrees rather than a quaternion: that is what flying with a
     * mouse produces, and it keeps the horizon level for free.
     */
    struct CameraPose {
        SDK::Vec3 position {};
        float yawDegrees   = 0.0F;
        float pitchDegrees = 0.0F;

        /**
         * Vertical field of view the renderer settled on. Read back rather
         * than assumed: Hor+ adaptation and the window aspect both move it,
         * and a gizmo drawn against the wrong frustum misses its object.
         */
        float fovDegrees = 60.0F;

        /** Width over height, as `CCamera::GetProjectionRatio` reports it. */
        float aspectRatio = 16.0F / 9.0F;
    };

    /** The view direction, i.e. column 1 of a CryEngine camera matrix. */
    SDK::Vec3 PoseForward(const CameraPose &pose);

    /** Column 0 of that matrix: level with the horizon, so roll stays zero. */
    SDK::Vec3 PoseRight(const CameraPose &pose);

    /** Column 2: right x forward, so the three make a right-handed frame. */
    SDK::Vec3 PoseUp(const CameraPose &pose);

    /** The rotation `IEntity::SetRotation` takes to face the pose's way. */
    SDK::Quat PoseRotation(const CameraPose &pose);

    /** Seeds a pose's angles from a direction, leaving position and lens. */
    void PoseLookAlong(CameraPose &pose, const SDK::Vec3 &direction);

    /**
     * Column-major 4x4 matrices in the convention ImGuizmo reads: the eye
     * looks down -Z, and the projection maps depth into -1..1.
     */
    void BuildViewMatrix(const CameraPose &pose, float view[16]);
    void BuildProjectionMatrix(const CameraPose &pose, float nearPlane, float farPlane, float projection[16]);

    /**
     * The world direction through a point on the near plane, given in
     * normalized device coordinates: x and y both run -1 at the left and
     * bottom to +1 at the right and top.
     */
    SDK::Vec3 RayThroughNdc(const CameraPose &pose, float ndcX, float ndcY);

    /** Where a world point lands on screen, and whether it is in front. */
    bool ProjectToNdc(const CameraPose &pose, const SDK::Vec3 &point, float &ndcX, float &ndcY);

    /** One frame of fly input, already resolved from keys and mouse. */
    struct FlyInput {
        float forward    = 0.0F; // +1 ahead, -1 back
        float strafe     = 0.0F; // +1 right
        float lift       = 0.0F; // +1 up, along world Z rather than the view's
        float yawDelta   = 0.0F; // degrees this frame
        float pitchDelta = 0.0F; // degrees this frame
        bool boost       = false;
        bool crawl       = false;
    };

    /**
     * A free camera that flies through the world without colliding with it.
     *
     * The engine has no free camera the mod can drive -- C_CameraDebug's free
     * mode reads the game's own mouse -- so this is an ordinary entity with
     * no geometry and no physics, made the active view through
     * CViewSystem::SetActiveCamera. CView::Update reads its world transform
     * every frame, and nothing is ever asked to sweep: that is the noclip.
     *
     * There is one active view, and `Apply` re-takes it whenever it is not
     * ours -- which is what keeps the camera through a dialogue or a save
     * load, and what would make two of these fight every frame. `Take`
     * refuses while another instance holds it.
     *
     * Game thread only.
     */
    class AnchorCamera final {
      public:
        /**
         * Seeds the pose from the live render camera and takes the view.
         * Refuses while another instance holds it, and while there is no
         * level for the anchor to be spawned into.
         */
        bool Take();

        /** Hands the view back and drops the anchor. Safe when not held. */
        void Release();

        /**
         * Takes the view back if something else made another view active.
         * For a camera that stands still: one being flown re-takes it in
         * every `Apply` anyway.
         */
        void Reclaim();

        bool IsActive() const {
            return _anchorEntityId != 0;
        }

        /** The anchor entity, or 0 while the game still owns the view. */
        SDK::EntityId GetAnchorEntityId() const {
            return _anchorEntityId;
        }

        /** Moves and turns the camera, then writes it to the anchor. */
        void Fly(const FlyInput &input, float deltaSeconds);

        /** Puts the camera `distance` back from `target`, still facing it. */
        void Focus(const SDK::Vec3 &target, float distance);

        const CameraPose &GetPose() const {
            return _pose;
        }

        void SetPosition(const SDK::Vec3 &position);

        /** Places and aims the camera in one go, as the view cube does. */
        void SetPose(const SDK::Vec3 &position, const SDK::Vec3 &forward);

        /** Aims the camera by its two angles, in degrees. Pitch is clamped. */
        void SetRotation(float yawDegrees, float pitchDegrees);

        /** Metres per second at rest, before boost or crawl scales it. */
        float GetSpeed() const {
            return _speed;
        }
        void SetSpeed(float metresPerSecond);

        void SetFieldOfView(float degrees);

        /** What the view was asked for, rather than what came back. */
        float GetRequestedFieldOfView() const;

      private:
        /** Writes the pose to the anchor entity and the live view params. */
        void Apply();

        /** Makes the anchor's view the active one. Idempotent. */
        void Activate();

        /** Whether the active view is still the one riding the anchor. */
        bool OwnsActiveView() const;

        /** Reads back the lens the engine is actually rendering with. */
        void RefreshLens();

        SDK::EntityId _anchorEntityId = 0;
        CameraPose _pose {};
        float _speed = 12.0F;

        // What the view is asked for, as opposed to what came back. Keeping
        // them apart stops an adapted FOV feeding back as the next request.
        float _requestedFovDegrees = 60.0F;

        // Whoever has the engine's one active view, or null. There can be
        // one flying camera, which is this class's own fact about itself.
        static AnchorCamera *_holder;
    };
} // namespace KCDC::Features::Camera
