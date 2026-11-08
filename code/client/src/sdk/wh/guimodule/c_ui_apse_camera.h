#pragma once

#include "../../cryengine/action/s_view_params.h"

namespace SDK::wh::guimodule {
    /**
     * The pause screen's camera, embedded in `C_UIApse` at +0x25C0 (its
     * constructor is 0x18146D1E0). It slides along the row of panels to the
     * anchor of each page (`MoveToView` 0x180C42E18) and is the camera the
     * camera manager selects while the screen is up (`IsActive` 0x1809E5BE8).
     *
     * Only the identity is modelled: the mod reaches it through the pose it
     * hands the camera manager, `GetCameraParams` (0x1814E40A0), which writes
     * its position, an identity rotation -- looking down +Y at the panels --
     * and its FOV in radians into an `SViewParams`.
     */
    class C_UIApseCamera final {
      public:
        C_UIApseCamera()                                  = delete;
        ~C_UIApseCamera()                                 = delete;
        C_UIApseCamera(const C_UIApseCamera &)            = delete;
        C_UIApseCamera &operator=(const C_UIApseCamera &) = delete;
    };
} // namespace SDK::wh::guimodule
