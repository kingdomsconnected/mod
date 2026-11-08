#pragma once

#include "../../patterns.h"
#include "i_audio_interfaces_common_data.h"
#include "s_audio_request.h"
#include "i_audio_proxy.h"

#include <utils/hooking/hooking.h>

namespace SDK {
    /**
     * CryEngine's Audio Translation Layer, which every gameplay sound goes
     * through before it reaches FMOD. `CAudioSystem`, vtable 0x183D281E0, 1944
     * bytes, built by `CEngineModule_CrySoundSystem::Initialize` (0x181174234).
     * This is the pre-component ATL.
     *
     * `CWh_FmodStudioWrapper` is the FMOD implementation underneath, which the
     * mod uses for voice because voice is PCM the ATL knows nothing about. A
     * gameplay sound is a trigger, and triggers are here.
     *
     * Unproven slots keep the native order under `Opaque` names. Game thread
     * only.
     */
    class IAudioSystem {
      public:
        virtual ~IAudioSystem() = default; // +0x000

        virtual void Opaque01() = 0;
        virtual void Opaque02() = 0;

        /**
         * +0x018 (0x180763660). Every other method funnels into it. Copies the request before it
         * returns; a blocking request (flag 4) returns once the audio thread has processed it.
         */
        virtual void PushRequest(const SAudioRequest &request) = 0;

        virtual void Opaque04() = 0; // +0x020, AddRequestListener
        virtual void Opaque05() = 0; // +0x028, RemoveRequestListener
        virtual void Opaque06() = 0;

        /** +0x038. False for a name no control file declares, which is the only validation there is. */
        virtual bool GetAudioTriggerId(const char *name, AudioControlId &triggerId) const = 0;

        virtual bool GetAudioRtpcId(const char *name, AudioControlId &rtpcId) const = 0; // +0x040
        virtual bool GetAudioSwitchId(const char *name, AudioControlId &switchId) const = 0; // +0x048
        virtual bool GetAudioSwitchStateId(AudioControlId switchId, const char *name, AudioSwitchStateId &stateId) const = 0; // +0x050
        virtual void Opaque11() = 0;
        virtual bool GetAudioEnvironmentId(const char *name, AudioEnvironmentId &environmentId) const = 0; // +0x060
        virtual void Opaque13() = 0;
        virtual void Opaque14() = 0;
        virtual void Opaque15() = 0;
        virtual void Opaque16() = 0;
        virtual void Opaque17() = 0;

        /** +0x090. Pops a pooled proxy, allocating when the free list is empty. */
        virtual IAudioProxy *GetFreeAudioProxy() = 0;

        /** +0x098. Reached through `IAudioProxy::Release`; nothing should call it directly. */
        virtual void FreeAudioProxy(IAudioProxy *proxy) = 0;

        virtual void Opaque20() = 0;
        virtual void Opaque21() = 0;
        virtual void Opaque22() = 0;
        virtual void Opaque23() = 0;
        virtual void Opaque24() = 0;
        virtual void Opaque25() = 0;

        /** Null until the sound module is up, which for an injected client is long after startup. */
        static IAudioSystem *GetInstance();
    };

    /**
     * Executes a trigger on the ATL's global audio object -- the 2D path the
     * GUI sound controller and the AI sound node take when they have no entity.
     *
     * `playOnlyOnce` consults a process-wide set of ids already played and
     * drops the request if this one is in it. The set is never cleared, so it
     * is a once-per-launch guard rather than a cooldown.
     *
     * The native ignores RCX -- its call site does not even load it -- so the
     * dead first argument is spelled out rather than hidden.
     */
    inline bool ExecuteGlobalAudioTrigger(AudioControlId triggerId, bool playOnlyOnce) {
        void *const unusedThis = nullptr;
        return hook::call<bool>(gPatterns.ExecuteGlobalAudioTrigger180558908, unusedThis, &triggerId, playOnlyOnce);
    }
} // namespace SDK
