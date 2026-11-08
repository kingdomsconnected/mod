#pragma once

#include "sdk/cryengine/action/c_view.h"
#include "sdk/types.h"

namespace KCDC::Features::Camera {
    /**
     * Where the game is drawing from this frame.
     *
     * The basis is read off the render camera's matrix rather than rebuilt
     * from a yaw and a pitch, because the columns carry roll and the game
     * rolls its camera. CryEngine's convention: column 0 is right, 1 is
     * forward, 2 is up, 3 is the position.
     */
    struct ViewPose {
        SDK::Vec3 position {0.0F, 0.0F, 0.0F};
        SDK::Vec3 forward {0.0F, 1.0F, 0.0F};
        SDK::Vec3 right {1.0F, 0.0F, 0.0F};
        SDK::Vec3 up {0.0F, 0.0F, 1.0F};

        /** Vertical field of view in degrees, as the renderer settled on it. */
        float fovDegrees = 60.0F;

        /** Width over height, as `CCamera::GetProjectionRatio` reports it. */
        float aspectRatio = 16.0F / 9.0F;
    };

    /**
     * Where a world point lands on the frame the renderer is drawing, as
     * fractions of it: 0 at the left and top, 1 at the right and bottom.
     */
    struct ScreenPoint {
        float x = 0.0F;
        float y = 0.0F;

        /** Inside the frame's edges. Outside them `x`/`y` run past 0..1. */
        bool onScreen = false;
    };

    /** The view the game is drawing through, or null in the main menu and across a level load. Game thread only. */
    SDK::CView *ActiveView();

    /**
     * Reads the active view's render camera. False in the main menu and
     * across a level load. Game thread only.
     */
    bool ReadViewPose(ViewPose &pose);

    /**
     * The world direction through a point on screen in normalized device
     * coordinates: -1 at the left and bottom, +1 at the right and top.
     */
    SDK::Vec3 RayThroughNdc(const ViewPose &pose, float ndcX, float ndcY);

    /**
     * Projects a world point through the renderer's own ProjectToScreen,
     * against the camera the frame is being drawn through. False when the
     * point is behind that camera, where it has no place on the frame. Game
     * thread only.
     */
    bool ProjectToScreen(const SDK::Vec3 &point, ScreenPoint &screen);
} // namespace KCDC::Features::Camera
