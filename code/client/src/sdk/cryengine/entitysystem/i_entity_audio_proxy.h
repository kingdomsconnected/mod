#pragma once

#include "../audio/i_audio_interfaces_common_data.h"
#include "i_entity.h"
#include "i_entity_proxy.h"

namespace SDK {
    /**
     * An entity's own audio objects. `CEntityAudioProxy`, vtable 0x1840759F0,
     * the `EEntityProxy::Audio` component of a `CEntity`.
     *
     * It owns aux `IAudioProxy` instances glued to the entity's transform, so
     * a trigger started here follows the entity and can be stopped again --
     * the difference from the fire-and-forget one-shot a bare `IAudioProxy`
     * gives. Every verb takes the id of the aux proxy to address;
     * `kAllAudioProxyIds` means all of them.
     *
     * The first 22 slots are `IEntityProxy`; unproven additions keep the
     * native order under `Opaque` names. Game thread only.
     */
    class IEntityAudioProxy: public IEntityProxy {
      public:
        virtual void Opaque22() = 0;
        virtual void Opaque23() = 0;
        virtual void Opaque24() = 0;
        virtual void Opaque25() = 0;
        virtual void Opaque26() = 0;
        virtual void Opaque27() = 0;
        /**
         * +0x0E0. Plays `<file>.ogg`, a game-root path read through CryPak, as a programmer sound
         * on the host event the proxy's `standalone_file_purpose` switch selects. `startTime` is
         * where to begin, in seconds; the game passes 0 or a resume point (0x18387671C).
         */
        virtual void PlayFile(const char *file, float startTime, AudioProxyId audioProxyId, const SAudioCallBackInfo &callBackInfo) = 0;

        virtual void Opaque29() = 0; // +0x0E8, StopFile

        /** +0x0F0. False when the component has no entity, or when `audioProxyId` names one that is not there. */
        virtual bool ExecuteTrigger(AudioControlId triggerId, ELipSyncMethod lipSyncMethod, AudioProxyId audioProxyId, const SAudioCallBackInfo &callBackInfo) = 0;

        virtual void StopTrigger(AudioControlId triggerId, AudioProxyId audioProxyId) = 0; // +0x0F8
        virtual void StopAllTriggers(AudioProxyId audioProxyId) = 0; // +0x100
        virtual void SetSwitchState(AudioControlId switchId, AudioSwitchStateId stateId, AudioProxyId audioProxyId) = 0; // +0x108
        virtual void SetRtpcValue(AudioControlId rtpcId, float value, AudioProxyId audioProxyId) = 0; // +0x110

        virtual void Opaque35() = 0;
        virtual void Opaque36() = 0;
        virtual void Opaque37() = 0;
        virtual void Opaque38() = 0;
        virtual void Opaque39() = 0;
        virtual void Opaque40() = 0;
        virtual void Opaque41() = 0;
        virtual void Opaque42() = 0;

        /**
         * The entity's audio component, created if it has none -- the same two
         * calls as `C_Minigame::GetOrCreateEntityAudioProxy_18089ACCC`. The
         * component belongs to the entity, so the borrowed pointer lives
         * exactly as long as the entity does.
         */
        static IEntityAudioProxy *GetOrCreate(IEntity *entity) {
            if (entity == nullptr) {
                return nullptr;
            }

            IEntityProxy *existing = entity->GetProxy(static_cast<int>(EEntityProxy::Audio));
            if (existing != nullptr) {
                return static_cast<IEntityAudioProxy *>(existing);
            }

            // The share is dropped here and the component stays on the entity. It frees through the control block's virtuals, which run inside the game, so letting it cross back is safe.
            return static_cast<IEntityAudioProxy *>(entity->CreateProxy(static_cast<int>(EEntityProxy::Audio)).get());
        }
    };
} // namespace SDK
