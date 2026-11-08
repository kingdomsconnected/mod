#pragma once

#include "s_camera_params.h"

#include <cstddef>

namespace SDK {
    struct IAnimSequence;

    /**
     * Listener notified by CViewSystem around movie-camera transitions.
     * The abstract table at 0x183AF1628 contains exactly four entries.
     */
    struct IViewSystemListener {
        virtual ~IViewSystemListener() = default; // +0x00
        virtual bool OnBeginCutScene(IAnimSequence *sequence,
                                     bool resetFx) = 0; // +0x08
        virtual bool OnEndCutScene(IAnimSequence *sequence) = 0; // +0x10
        virtual bool OnCameraChange(const SCameraParams &params) = 0; // +0x18
    };

    static_assert(sizeof(IViewSystemListener) == 0x8,
                  "IViewSystemListener size mismatch");
} // namespace SDK
