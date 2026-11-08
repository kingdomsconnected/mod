#pragma once

#include "../../types.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    /**
     * Movie-camera request consumed by CViewSystem::SetActiveCamera -- the
     * game's own fixed camera. Naming an entity makes its view active, the
     * blend speeds turn the cut into a glide, and id 0 hands the view back.
     *
     * The mapping is read off SetActiveCamera: +0x04 goes to SViewParams+0x30
     * (the FOV) and the +0x1C/+0x20 pair to +0x34/+0x38.
     */
    struct SCameraParams {
        EntityId cameraEntityId; // 0x00, 0 restores the pre-sequence view
        float fov; // 0x04, radians
        float nearZ; // 0x08, default 0.25f
        float state000C; // 0x0C
        bool justActivated; // 0x10
        bool performBlendOut; // 0x11
        std::byte padding0012[0x2];
        float blendPositionSpeed; // 0x14, non-positive uses CViewSystem+0x94
        float blendRotationSpeed; // 0x18, non-positive uses CViewSystem+0x98
        bool adaptFovToAspectRatio; // 0x1C, copied to SViewParams+0x34
        std::byte padding001D[0x3];
        float referenceAspectRatio; // 0x20, copied to SViewParams+0x38, default 1.0f
    };

    static_assert(offsetof(SCameraParams, fov) == 0x04, "SCameraParams::fov offset mismatch");
    static_assert(offsetof(SCameraParams, nearZ) == 0x08, "SCameraParams::nearZ offset mismatch");
    static_assert(offsetof(SCameraParams, blendPositionSpeed) == 0x14, "SCameraParams::blendPositionSpeed offset mismatch");
    static_assert(offsetof(SCameraParams, referenceAspectRatio) == 0x20, "SCameraParams::referenceAspectRatio offset mismatch");
    static_assert(sizeof(SCameraParams) == 0x24, "SCameraParams size mismatch");
} // namespace SDK
