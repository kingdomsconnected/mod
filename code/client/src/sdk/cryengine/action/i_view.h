#pragma once

#include "s_view_params.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    struct IEntity;
    struct IGameObject;

    /** Per-camera CryAction view interface. */
    struct IView {
        /** Exact argument block consumed by SetViewShakeEx. */
        struct SShakeParams {
            Ang3 shakeAngle; // 0x00
            Vec3 shakeShift; // 0x0C
            float sustainDuration; // 0x18
            float fadeInDuration; // 0x1C
            float fadeOutDuration; // 0x20
            float frequency; // 0x24
            float randomness; // 0x28
            int shakeId; // 0x2C
            bool flipVector; // 0x30
            bool updateOnly; // 0x31
            bool groundOnly; // 0x32
            bool permanent; // 0x33
            bool smooth; // 0x34
            std::byte padding0035[0x3];
        };

        virtual ~IView() = default; // +0x00
        virtual void Release() = 0; // +0x08
        virtual void Update(float frameTime, bool active) = 0; // +0x10
        virtual void LinkTo(IGameObject *pGameObject) = 0; // +0x18
        virtual void LinkTo(IEntity *pEntity) = 0; // +0x20
        virtual EntityId GetLinkedId() = 0; // +0x28
        virtual void SetCurrentParams(SViewParams &params) = 0; // +0x30
        virtual const SViewParams *GetCurrentParams() = 0; // +0x38
        virtual void SetViewShake(Ang3 shakeAngle, Vec3 shakeShift,
                                  float duration, float frequency,
                                  float randomness, int shakeId,
                                  bool flipVector = true,
                                  bool updateOnly = false,
                                  bool groundOnly = false) = 0; // +0x40
        virtual void SetViewShakeEx(const SShakeParams &params) = 0; // +0x48
        virtual void StopShake(int shakeId) = 0; // +0x50
        virtual void ResetShaking() = 0; // +0x58
        virtual void ResetBlending() = 0; // +0x60
        virtual void SetFrameAdditiveCameraAngles(
            const Ang3 &additiveAngles) = 0; // +0x68
        virtual void SetScale(float scale) = 0; // +0x70
        virtual void SetZoomedScale(float scale) = 0; // +0x78
        virtual void SetActive(bool active) = 0; // +0x80
    };

    static_assert(offsetof(IView::SShakeParams, shakeId) == 0x2C,
                  "IView::SShakeParams shake-id offset mismatch");
    static_assert(offsetof(IView::SShakeParams, permanent) == 0x33,
                  "IView::SShakeParams permanent offset mismatch");
    static_assert(sizeof(IView::SShakeParams) == 0x38,
                  "IView::SShakeParams size mismatch");
    static_assert(sizeof(IView) == 0x8, "IView size mismatch");
} // namespace SDK
