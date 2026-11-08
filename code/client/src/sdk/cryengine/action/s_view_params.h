#pragma once

#include "../../types.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    /**
     * Exact camera state exchanged through IView.
     *
     * KCD2 keeps CryEngine's prefix through the FOV at +0x30 and inserts two
     * fields after it, so everything from the view id on sits eight bytes past
     * its stock offset. The FOV offset is called out because an earlier reading
     * put it at +0x38: CView::Update passes CView+0x44 to SetFrustum as the FOV,
     * and UpdateBlending and SavePrevious both pair +0x30 with previousFov.
     *
     * The inserted pair feeds CCamera+0x48/+0x4C, the adapt-FOV-to-aspect flag
     * and its reference aspect. Defaults are the constructor's (0x1807F24CC).
     */
    struct SViewParams {
        Vec3 position{}; // 0x00
        Quat rotation{0.0F, 0.0F, 0.0F, 1.0F}; // 0x0C
        Quat localRotationLast{0.0F, 0.0F, 0.0F, 1.0F}; // 0x1C
        float nearPlane = 0.0F; // 0x2C
        float fov = 0.0F; // 0x30, radians; below 0.001 the engine substitutes its own
        bool adaptFovToAspectRatio = false; // 0x34, KCD2 extension -> CCamera+0x48
        std::byte padding0035[0x3]{};
        float referenceAspectRatio = 1.0F; // 0x38, KCD2 extension -> CCamera+0x4C
        std::uint8_t viewId = 0; // 0x3C
        bool groundOnly = false; // 0x3D
        std::byte padding003E[0x2]{};
        float shakingRatio = 0.0F; // 0x40
        Quat currentShakeRotation{0.0F, 0.0F, 0.0F, 1.0F}; // 0x44
        Vec3 currentShakeShift{}; // 0x54
        EntityId targetId = 0; // 0x60
        Vec3 targetPosition{}; // 0x64
        float frameTime = 0.0F; // 0x70
        float angleVelocity = 0.0F; // 0x74
        float velocity = 0.0F; // 0x78
        float distance = 0.0F; // 0x7C
        bool blend = true; // 0x80
        std::byte padding0081[0x3]{};
        float blendPositionSpeed = 5.0F; // 0x84
        float blendRotationSpeed = 10.0F; // 0x88
        float blendFovSpeed = 5.0F; // 0x8C
        Vec3 blendPositionOffset{}; // 0x90
        Quat blendRotationOffset{0.0F, 0.0F, 0.0F, 1.0F}; // 0x9C
        float blendFovOffset = 0.0F; // 0xAC
        bool justActivated = false; // 0xB0
        std::uint8_t previousViewId = 0; // 0xB1
        std::byte padding00B2[0x2]{};
        Vec3 previousPosition{}; // 0xB4
        Quat previousRotation{0.0F, 0.0F, 0.0F, 1.0F}; // 0xC0
        float previousFov = 0.0F; // 0xD0
    };

    static_assert(offsetof(SViewParams, nearPlane) == 0x2C, "SViewParams near-plane offset mismatch");
    static_assert(offsetof(SViewParams, fov) == 0x30, "SViewParams FOV offset mismatch");
    static_assert(offsetof(SViewParams, adaptFovToAspectRatio) == 0x34, "SViewParams adapt-FOV offset mismatch");
    static_assert(offsetof(SViewParams, referenceAspectRatio) == 0x38, "SViewParams reference-aspect offset mismatch");
    static_assert(offsetof(SViewParams, viewId) == 0x3C, "SViewParams view-id offset mismatch");
    static_assert(offsetof(SViewParams, currentShakeRotation) == 0x44, "SViewParams shake-rotation offset mismatch");
    static_assert(offsetof(SViewParams, targetId) == 0x60, "SViewParams target-id offset mismatch");
    static_assert(offsetof(SViewParams, blendPositionSpeed) == 0x84, "SViewParams blend-speed offset mismatch");
    static_assert(offsetof(SViewParams, previousPosition) == 0xB4, "SViewParams previous-position offset mismatch");
    static_assert(offsetof(SViewParams, previousFov) == 0xD0, "SViewParams previous-FOV offset mismatch");
    static_assert(sizeof(SViewParams) == 0xD4, "SViewParams size mismatch");
} // namespace SDK
