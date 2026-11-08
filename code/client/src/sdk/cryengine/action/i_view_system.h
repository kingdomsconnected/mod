#pragma once

#include "i_view.h"
#include "i_view_system_listener.h"

#include <cstddef>

namespace SDK {
    /**
     * CryAction camera-view owner and active-view coordinator.
     *
     * The abstract table at 0x18405D920 contains exactly 24 entries. KCD2
     * shuffles both overload pairs relative to public CryEngine: the numeric
     * view-id overload precedes the IView-pointer overload for RemoveView and
     * SetActiveView. Every other contract below is independently matched to
     * CViewSystem's local implementation and retained in the emitted order.
     */
    struct IViewSystem {
        virtual ~IViewSystem() = default; // +0x00
        virtual IView *CreateView() = 0; // +0x08
        virtual void RemoveView(std::uint32_t viewId) = 0; // +0x10
        virtual void RemoveView(IView *pView) = 0; // +0x18
        virtual void SetActiveView(std::uint32_t viewId) = 0; // +0x20
        virtual void SetActiveView(IView *pView) = 0; // +0x28
        virtual IView *GetView(std::uint32_t viewId) const = 0; // +0x30
        virtual IView *GetActiveView() const = 0; // +0x38
        virtual bool IsClientActorViewActive() const = 0; // +0x40
        virtual std::uint32_t GetViewId(IView *pView) const = 0; // +0x48
        virtual std::uint32_t GetActiveViewId() const = 0; // +0x50
        virtual IView *GetViewByEntityId(EntityId entityId,
                                         bool forceCreate = false) = 0; // +0x58
        virtual bool AddListener(IViewSystemListener *pListener) = 0; // +0x60
        virtual bool RemoveListener(IViewSystemListener *pListener) = 0; // +0x68
        virtual void Serialize(TSerialize serialize) = 0; // +0x70
        virtual void PostSerialize() = 0; // +0x78
        virtual float GetDefaultZNear() const = 0; // +0x80
        virtual void SetBlendParams(float blendPositionSpeed,
                                    float blendRotationSpeed,
                                    bool performBlendOut) = 0; // +0x88
        virtual void SetOverrideCameraRotation(bool overrideRotation,
                                               Quat rotation) = 0; // +0x90
        virtual bool IsPlayingCutScene() const = 0; // +0x98
        virtual void UpdateSoundListeners() = 0; // +0xA0
        virtual void SetDeferredViewSystemUpdate(bool deferred) = 0; // +0xA8
        virtual bool UseDeferredViewSystemUpdate() const = 0; // +0xB0
        virtual void SetControlAudioListeners(bool active) = 0; // +0xB8
    };

    static_assert(sizeof(IViewSystem) == 0x8,
                  "IViewSystem size mismatch");
} // namespace SDK
