#include <utils/safe_win32.h>

#include <MinHook.h>

#include <utils/hooking/hook_function.h>

#include "audio_range.h"

#include "sdk/patterns.h"
#include "sdk/wh/fsw_b_event_instance.h"

#include <cstdint>

namespace KCDC::Features::Audio {
    namespace {
        using Start_t              = std::int32_t(__fastcall *)(SDK::wh::FSW_B_EVENT_INSTANCE *);
        using Update3DAttributes_t = bool(__fastcall *)(SDK::wh::FSW_B_EVENT_INSTANCE *);

        Start_t g_startOriginal                           = nullptr;
        Update3DAttributes_t g_update3DAttributesOriginal = nullptr;

        /**
         * Where every FMOD event the game plays is started, the instance
         * already placed. The volume goes on before the start so the opening
         * transient -- the loudest part of a blast -- is already faded. One
         * out of range here is parked as Waiting, with the 0 set now.
         */
        std::int32_t __fastcall Start_Hook(SDK::wh::FSW_B_EVENT_INSTANCE *instance) {
            AudioRange::Get().Attenuate(*instance);
            return g_startOriginal(instance);
        }

        /**
         * Where the wrapper re-places every instance, from
         * `CWh_FmodStudioWrapper::SetListenerAttributes` as the listener moves
         * and as the instance's audio object does. It also starts a Waiting
         * instance once the listener comes in range, without passing Start;
         * that one was fired out of earshot and `Follow` keeps it silent.
         */
        bool __fastcall Update3DAttributes_Hook(SDK::wh::FSW_B_EVENT_INSTANCE *instance) {
            const bool wasWaiting = instance->GetState() == SDK::wh::FSW_B_EVENT_INSTANCE::EState::Waiting;
            const bool result     = g_update3DAttributesOriginal(instance);
            AudioRange::Get().Follow(*instance, wasWaiting);
            return result;
        }
    } // namespace
} // namespace KCDC::Features::Audio

static InitFunction init(
    []() {
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.FSW_B_EVENT_INSTANCE__Start), reinterpret_cast<LPVOID>(&KCDC::Features::Audio::Start_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Audio::g_startOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.FSW_B_EVENT_INSTANCE__Update3DAttributes), reinterpret_cast<LPVOID>(&KCDC::Features::Audio::Update3DAttributes_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::Audio::g_update3DAttributesOriginal));
    },
    "Audio");
