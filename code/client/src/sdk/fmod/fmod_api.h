#pragma once

#include "fmod_types.h"

#include <cstdint>

namespace SDK::Fmod {
    /**
     * The FMOD entry points the mod calls, resolved from the DLLs the game has
     * already loaded.
     *
     * These are exported C functions reached by name through `GetProcAddress`,
     * not addresses recovered by signature scanning, so the pointer *is* the
     * function and its calling convention is the DLL's published ABI -- the
     * same standing as the original a detour hands back.
     *
     * `Resolve` must be checked and is meant to be polled: the audio DLLs are
     * loaded by the game, so before its audio system comes up there is nothing
     * to bind to. Every entry point is non-null once it returns true.
     */
    class Api final {
      public:
        static Api &Get();

        /**
         * Binds every entry point below.
         *
         * The two failure modes are told apart deliberately. "Not loaded yet"
         * is retried silently and forever; a DLL that is loaded but does not
         * export what FMOD 2.02 should is a build the mod cannot drive, and is
         * reported once and then given up on.
         */
        bool Resolve();

        bool IsResolved() const {
            return _resolved;
        }

        // --- Core: System -------------------------------------------------

        FMOD_RESULT(__stdcall *System_GetVersion)(FMOD_SYSTEM *system, unsigned int *version)                                                                           = nullptr;
        FMOD_RESULT(__stdcall *System_CreateSound)(FMOD_SYSTEM *system, const char *nameOrData, std::uint32_t mode, FMOD_CREATESOUNDEXINFO *exinfo, FMOD_SOUND **sound) = nullptr;
        FMOD_RESULT(__stdcall *System_PlaySound)(FMOD_SYSTEM *system, FMOD_SOUND *sound, FMOD_CHANNELGROUP *group, int paused, FMOD_CHANNEL **channel)                  = nullptr;
        FMOD_RESULT(__stdcall *System_CreateChannelGroup)(FMOD_SYSTEM *system, const char *name, FMOD_CHANNELGROUP **group)                                             = nullptr;
        FMOD_RESULT(__stdcall *System_GetMasterChannelGroup)(FMOD_SYSTEM *system, FMOD_CHANNELGROUP **group)                                                            = nullptr;
        FMOD_RESULT(__stdcall *System_CreateDSP)(FMOD_SYSTEM *system, const FMOD_DSP_DESCRIPTION *description, FMOD_DSP **dsp)                                          = nullptr;

        // --- Core: DSP ----------------------------------------------------

        FMOD_RESULT(__stdcall *DSP_Release)(FMOD_DSP *dsp) = nullptr;

        // --- Core: recording ----------------------------------------------

        FMOD_RESULT(__stdcall *System_GetRecordNumDrivers)(FMOD_SYSTEM *system, int *numDrivers, int *numConnected)                                                                                                 = nullptr;
        FMOD_RESULT(__stdcall *System_GetRecordDriverInfo)(FMOD_SYSTEM *system, int id, char *name, int nameLen, FMOD_GUID *guid, int *systemRate, int *speakerMode, int *speakerModeChannels, unsigned int *state) = nullptr;
        FMOD_RESULT(__stdcall *System_RecordStart)(FMOD_SYSTEM *system, int id, FMOD_SOUND *sound, int loop)                                                                                                        = nullptr;
        FMOD_RESULT(__stdcall *System_RecordStop)(FMOD_SYSTEM *system, int id)                                                                                                                                      = nullptr;
        FMOD_RESULT(__stdcall *System_GetRecordPosition)(FMOD_SYSTEM *system, int id, unsigned int *position)                                                                                                       = nullptr;

        // --- Core: Sound --------------------------------------------------

        FMOD_RESULT(__stdcall *Sound_Release)(FMOD_SOUND *sound)                                                                                                          = nullptr;
        FMOD_RESULT(__stdcall *Sound_Lock)(FMOD_SOUND *sound, unsigned int offset, unsigned int length, void **ptr1, void **ptr2, unsigned int *len1, unsigned int *len2) = nullptr;
        FMOD_RESULT(__stdcall *Sound_Unlock)(FMOD_SOUND *sound, void *ptr1, void *ptr2, unsigned int len1, unsigned int len2)                                             = nullptr;

        // --- Core: Channel ------------------------------------------------

        FMOD_RESULT(__stdcall *Channel_Stop)(FMOD_CHANNEL *channel)                                                                      = nullptr;
        FMOD_RESULT(__stdcall *Channel_SetPaused)(FMOD_CHANNEL *channel, int paused)                                                     = nullptr;
        FMOD_RESULT(__stdcall *Channel_SetVolume)(FMOD_CHANNEL *channel, float volume)                                                   = nullptr;
        FMOD_RESULT(__stdcall *Channel_SetPriority)(FMOD_CHANNEL *channel, int priority)                                                 = nullptr;
        FMOD_RESULT(__stdcall *Channel_Set3DAttributes)(FMOD_CHANNEL *channel, const FMOD_VECTOR *position, const FMOD_VECTOR *velocity) = nullptr;
        FMOD_RESULT(__stdcall *Channel_Set3DMinMaxDistance)(FMOD_CHANNEL *channel, float minDistance, float maxDistance)                 = nullptr;
        FMOD_RESULT(__stdcall *Channel_SetChannelGroup)(FMOD_CHANNEL *channel, FMOD_CHANNELGROUP *group)                                 = nullptr;
        FMOD_RESULT(__stdcall *Channel_AddDSP)(FMOD_CHANNEL *channel, int index, FMOD_DSP *dsp)                                          = nullptr;
        FMOD_RESULT(__stdcall *Channel_RemoveDSP)(FMOD_CHANNEL *channel, FMOD_DSP *dsp)                                                  = nullptr;

        // --- Core: ChannelGroup -------------------------------------------

        FMOD_RESULT(__stdcall *ChannelGroup_Release)(FMOD_CHANNELGROUP *group)                                                                       = nullptr;
        FMOD_RESULT(__stdcall *ChannelGroup_SetVolume)(FMOD_CHANNELGROUP *group, float volume)                                                       = nullptr;
        FMOD_RESULT(__stdcall *ChannelGroup_AddGroup)(FMOD_CHANNELGROUP *parent, FMOD_CHANNELGROUP *child, int propagateDspClock, void **connection) = nullptr;

        // --- Studio -------------------------------------------------------

        FMOD_RESULT(__stdcall *Studio_System_FlushCommands)(FMOD_STUDIO_SYSTEM *system)                                   = nullptr;
        FMOD_RESULT(__stdcall *Studio_System_GetBus)(FMOD_STUDIO_SYSTEM *system, const char *path, FMOD_STUDIO_BUS **bus) = nullptr;
        FMOD_RESULT(__stdcall *Studio_Bus_LockChannelGroup)(FMOD_STUDIO_BUS *bus)                                         = nullptr;
        FMOD_RESULT(__stdcall *Studio_Bus_UnlockChannelGroup)(FMOD_STUDIO_BUS *bus)                                       = nullptr;
        FMOD_RESULT(__stdcall *Studio_Bus_GetChannelGroup)(FMOD_STUDIO_BUS *bus, FMOD_CHANNELGROUP **group)               = nullptr;
        FMOD_RESULT(__stdcall *Studio_System_LoadBankFile)(FMOD_STUDIO_SYSTEM *system, const char *filename, std::uint32_t flags, FMOD_STUDIO_BANK **bank) = nullptr;
        FMOD_RESULT(__stdcall *Studio_Bank_Unload)(FMOD_STUDIO_BANK *bank)                                                                               = nullptr;
        FMOD_RESULT(__stdcall *Studio_Bank_GetPath)(FMOD_STUDIO_BANK *bank, char *path, int size, int *retrieved)                                         = nullptr;
        FMOD_RESULT(__stdcall *Studio_Bank_GetEventCount)(FMOD_STUDIO_BANK *bank, int *count)                                                             = nullptr;
        FMOD_RESULT(__stdcall *Studio_Bank_GetEventList)(FMOD_STUDIO_BANK *bank, FMOD_STUDIO_EVENTDESCRIPTION **events, int capacity, int *count)         = nullptr;
        FMOD_RESULT(__stdcall *Studio_EventDescription_GetPath)(FMOD_STUDIO_EVENTDESCRIPTION *event, char *path, int size, int *retrieved)                = nullptr;
        FMOD_RESULT(__stdcall *Studio_EventInstance_SetVolume)(FMOD_STUDIO_EVENTINSTANCE *event, float volume)                                            = nullptr;

      private:
        bool _resolved = false;

        // A binding failure against DLLs that were present. Terminal: retrying
        // cannot change the answer.
        bool _refused = false;
    };
} // namespace SDK::Fmod
