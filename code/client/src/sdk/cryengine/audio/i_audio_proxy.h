#pragma once

#include "../../types.h"
#include "i_audio_interfaces_common_data.h"

namespace SDK {
    /**
     * One audio object in the ATL. `CAudioProxy`, vtable 0x183B20940, 120
     * bytes, handed out by `IAudioSystem::GetFreeAudioProxy`.
     *
     * `Initialize` reserves the object id asynchronously, so every call after
     * it is queued on the proxy and replayed when the id lands -- including
     * `Release`, which the ATL defers until the triggers already running have
     * finished. That is what makes initialise-place-execute-release a correct
     * one-shot; the game's material effects do exactly that at 0x18052A624.
     *
     * Unproven slots keep the native order under `Opaque` names. Game thread
     * only.
     */
    class IAudioProxy {
      public:
        virtual ~IAudioProxy() = default; // +0x000

        /** `initAsync` false blocks until the object id lands; the game always passes true. */
        virtual void Initialize(const char *objectName, bool initAsync) = 0; // +0x008

        /** Returns the proxy to the pool. Not a destructor: the ATL object outlives it. */
        virtual void Release() = 0; // +0x010

        virtual void Reset() = 0; // +0x018
        /** +0x020. Plays `<file>.ogg`, a game-root path read through CryPak, from `startTime` seconds (0x180956508). */
        virtual void PlayFile(const char *file, float startTime, const SAudioCallBackInfo &callBackInfo) = 0;

        virtual void StopFile(const char *file) = 0; // +0x028
        virtual void ExecuteTrigger(AudioControlId triggerId, ELipSyncMethod lipSyncMethod, const SAudioCallBackInfo &callBackInfo) = 0; // +0x030
        virtual void StopTrigger(AudioControlId triggerId) = 0; // +0x038
        virtual void StopAllTriggers() = 0; // +0x040
        virtual void SetSwitchState(AudioControlId switchId, AudioSwitchStateId stateId) = 0; // +0x048
        virtual void SetRtpcValue(AudioControlId rtpcId, float value) = 0; // +0x050

        /** +0x058, implemented as a `SetSwitchState` on the "ObstrOcclCalcType" switch. */
        virtual void SetOcclusionType(EAudioOcclusionType occlusionType) = 0;

        /** +0x060. Builds an identity transform around the point, so it drops any orientation already set. */
        virtual void SetPosition(const Vec3 &position) = 0;

        virtual void SetTransformation(const Matrix34 &transformation) = 0; // +0x068
        virtual void SetEnvironmentAmount(AudioEnvironmentId environmentId, float amount) = 0; // +0x070

        /** +0x078. Applies the reverb of every audio area the object stands in, skipping `entityToIgnore`. */
        virtual void SetCurrentEnvironments(EntityId entityToIgnore) = 0;

        virtual void Opaque16() = 0;
        virtual void Opaque17() = 0;
    };
} // namespace SDK
