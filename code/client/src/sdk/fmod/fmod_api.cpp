#include "fmod_api.h"

#include <utils/safe_win32.h>

#include <logging/logger.h>

namespace SDK::Fmod {
    namespace {
        // Records the first missing name, so the log names the entry point.
        template <typename T>
        bool Bind(HMODULE module, const char *name, T &target, const char *&missing) {
            FARPROC address = ::GetProcAddress(module, name);
            if (address == nullptr) {
                if (missing == nullptr) {
                    missing = name;
                }
                return false;
            }

            target = reinterpret_cast<T>(address);
            return true;
        }
    } // namespace

    Api &Api::Get() {
        static Api instance;
        return instance;
    }

    bool Api::Resolve() {
        if (_resolved) {
            return true;
        }

        if (_refused) {
            return false;
        }

        // Loaded by the game, never by the mod: an injected client reaches this
        // before the game's audio system exists, and the answer is "not yet".
        HMODULE core   = ::GetModuleHandleW(L"fmod.dll");
        HMODULE studio = ::GetModuleHandleW(L"fmodstudio.dll");
        if (core == nullptr || studio == nullptr) {
            return false;
        }

        const char *missing = nullptr;
        bool ok             = true;

        ok &= Bind(core, "FMOD_System_GetVersion", System_GetVersion, missing);
        ok &= Bind(core, "FMOD_System_CreateSound", System_CreateSound, missing);
        ok &= Bind(core, "FMOD_System_PlaySound", System_PlaySound, missing);
        ok &= Bind(core, "FMOD_System_CreateChannelGroup", System_CreateChannelGroup, missing);
        ok &= Bind(core, "FMOD_System_GetMasterChannelGroup", System_GetMasterChannelGroup, missing);
        ok &= Bind(core, "FMOD_System_CreateDSP", System_CreateDSP, missing);

        ok &= Bind(core, "FMOD_DSP_Release", DSP_Release, missing);

        ok &= Bind(core, "FMOD_System_GetRecordNumDrivers", System_GetRecordNumDrivers, missing);
        ok &= Bind(core, "FMOD_System_GetRecordDriverInfo", System_GetRecordDriverInfo, missing);
        ok &= Bind(core, "FMOD_System_RecordStart", System_RecordStart, missing);
        ok &= Bind(core, "FMOD_System_RecordStop", System_RecordStop, missing);
        ok &= Bind(core, "FMOD_System_GetRecordPosition", System_GetRecordPosition, missing);

        ok &= Bind(core, "FMOD_Sound_Release", Sound_Release, missing);
        ok &= Bind(core, "FMOD_Sound_Lock", Sound_Lock, missing);
        ok &= Bind(core, "FMOD_Sound_Unlock", Sound_Unlock, missing);

        ok &= Bind(core, "FMOD_Channel_Stop", Channel_Stop, missing);
        ok &= Bind(core, "FMOD_Channel_SetPaused", Channel_SetPaused, missing);
        ok &= Bind(core, "FMOD_Channel_SetVolume", Channel_SetVolume, missing);
        ok &= Bind(core, "FMOD_Channel_SetPriority", Channel_SetPriority, missing);
        ok &= Bind(core, "FMOD_Channel_Set3DAttributes", Channel_Set3DAttributes, missing);
        ok &= Bind(core, "FMOD_Channel_Set3DMinMaxDistance", Channel_Set3DMinMaxDistance, missing);
        ok &= Bind(core, "FMOD_Channel_SetChannelGroup", Channel_SetChannelGroup, missing);
        ok &= Bind(core, "FMOD_Channel_AddDSP", Channel_AddDSP, missing);
        ok &= Bind(core, "FMOD_Channel_RemoveDSP", Channel_RemoveDSP, missing);

        ok &= Bind(core, "FMOD_ChannelGroup_Release", ChannelGroup_Release, missing);
        ok &= Bind(core, "FMOD_ChannelGroup_SetVolume", ChannelGroup_SetVolume, missing);
        ok &= Bind(core, "FMOD_ChannelGroup_AddGroup", ChannelGroup_AddGroup, missing);

        ok &= Bind(studio, "FMOD_Studio_System_FlushCommands", Studio_System_FlushCommands, missing);
        ok &= Bind(studio, "FMOD_Studio_System_GetBus", Studio_System_GetBus, missing);
        ok &= Bind(studio, "FMOD_Studio_Bus_LockChannelGroup", Studio_Bus_LockChannelGroup, missing);
        ok &= Bind(studio, "FMOD_Studio_Bus_UnlockChannelGroup", Studio_Bus_UnlockChannelGroup, missing);
        ok &= Bind(studio, "FMOD_Studio_Bus_GetChannelGroup", Studio_Bus_GetChannelGroup, missing);
        ok &= Bind(studio, "FMOD_Studio_System_LoadBankFile", Studio_System_LoadBankFile, missing);
        ok &= Bind(studio, "FMOD_Studio_Bank_Unload", Studio_Bank_Unload, missing);
        ok &= Bind(studio, "FMOD_Studio_Bank_GetPath", Studio_Bank_GetPath, missing);
        ok &= Bind(studio, "FMOD_Studio_Bank_GetEventCount", Studio_Bank_GetEventCount, missing);
        ok &= Bind(studio, "FMOD_Studio_Bank_GetEventList", Studio_Bank_GetEventList, missing);
        ok &= Bind(studio, "FMOD_Studio_EventDescription_GetPath", Studio_EventDescription_GetPath, missing);
        ok &= Bind(studio, "FMOD_Studio_EventInstance_SetVolume", Studio_EventInstance_SetVolume, missing);

        if (!ok) {
            _refused = true;
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("FMOD: '{}' is not exported; the game's audio DLLs are not the expected build", missing);
            return false;
        }

        _resolved = true;
        return true;
    }
} // namespace SDK::Fmod
