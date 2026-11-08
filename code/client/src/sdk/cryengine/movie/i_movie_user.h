#pragma once

#include "../action/s_camera_params.h"

#include <cstddef>

namespace SDK {
    struct IAnimSequence;
    /**
     * Movie-system callbacks implemented by the game camera owner.
     *
     * The abstract table at 0x18405DA68 contains exactly five entries. The
     * KCD2 retains a locally extended SCameraParams contract rather than the
     * smaller public CryEngine structure.
     */
    struct IMovieUser {
        virtual ~IMovieUser() = default; // +0x00
        virtual void SetActiveCamera(const SCameraParams &params) = 0; // +0x08
        virtual void BeginCutScene(IAnimSequence *sequence,
                                   unsigned long flags,
                                   bool resetFx) = 0; // +0x10
        virtual void EndCutScene(IAnimSequence *sequence,
                                 unsigned long flags) = 0; // +0x18
        virtual void SendGlobalEvent(const char *event) = 0; // +0x20
    };

    static_assert(sizeof(IMovieUser) == 0x8,
                  "IMovieUser size mismatch");
} // namespace SDK
