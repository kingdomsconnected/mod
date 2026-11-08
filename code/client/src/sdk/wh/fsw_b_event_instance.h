#pragma once

#include "../fmod/fmod_types.h"
#include "fsw_event_description.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh {
    /**
     * One playing FMOD event the wrapper owns, `FSW_B_EVENT_INSTANCE`; every live one is the
     * derived `FSW_Wh_EVENT_INSTANCE` (0xD8 bytes, tagged allocation "FSW_EVENT_INSTANCE" in
     * `FSW_EVENT_DESCRIPTION::CreateInstance`, 0x1804806AC), whose own members start at 0xC0.
     *
     * Both natives that decide whether an instance is audible take one of these and nothing else:
     * `Start` (0x1803AF620) and `Update3DAttributes` (0x1804823C4), both hooked; the latter runs for every
     * instance whenever the listener moves (`CWh_FmodStudioWrapper::SetListenerAttributes`) and
     * whenever its audio object does. Neither is called from here, so they have no methods.
     */
    class FSW_B_EVENT_INSTANCE {
      public:
        /** Written by Start and Update3DAttributes; only the values they compare are named. */
        enum class EState : std::int32_t {
            Created  = 0,
            Starting = 1,
            Playing  = 2,
            Stopping = 3,
            Waiting  = 4, // out of range when started, started once the listener comes in
        };

        EState GetState() const {
            return m_state;
        }

        FSW_EVENT_DESCRIPTION *GetDescription() const {
            return m_pDescription;
        }

        SDK::Fmod::FMOD_STUDIO_EVENTINSTANCE *GetFmodInstance() const {
            return m_pFmodInstance;
        }

        /** Where the instance is, already in FMOD's space -- the same space as the wrapper's cached listener. */
        const SDK::Fmod::FMOD_3D_ATTRIBUTES &GetAttributes() const {
            return m_attributes;
        }

      private:
        void *m_pVTable;                                     // 0x00
        EState m_state;                                      // 0x08
        std::byte m_padding000C[0x4];                        // 0x0C
        void *m_pEventData;                                  // 0x10, IATLEventData
        void *m_pAudioObject;                                // 0x18, IATLAudioObjectData
        void *m_pMapHead;                                    // 0x20, std::map head node (0x48-byte nodes) the base ctor (0x1809E91E8) allocates
        std::size_t m_mapSize;                               // 0x28
        FSW_EVENT_DESCRIPTION *m_pDescription;               // 0x30
        void *m_pContext0038;                                // 0x38, the ctor's sixth argument
        SDK::StdString m_name;                               // 0x40, the ctor's string argument
        std::int32_t m_arg0060;                              // 0x60, the ctor's seventh argument
        std::int32_t m_arg0064;                              // 0x64, the ctor's ninth argument
        std::int32_t m_state0068;                            // 0x68, ctor sets -1
        std::byte m_padding006C[0x4];                        // 0x6C
        std::uint64_t m_state0070;                           // 0x70, ctor zeroes
        SDK::Fmod::FMOD_STUDIO_EVENTINSTANCE *m_pFmodInstance; // 0x78
        SDK::Fmod::FMOD_3D_ATTRIBUTES m_attributes;          // 0x80
        std::uint8_t m_flag00B0;                             // 0xB0, ctor writes the word 0x0100: 0 here
        std::uint8_t m_flag00B1;                             // 0xB1, and 1 here
        std::byte m_unset00B2[0xE];                          // 0xB2, not written by the base ctor

        static void AssertLayout() {
            static_assert(offsetof(FSW_B_EVENT_INSTANCE, m_state) == 0x08, "FSW_B_EVENT_INSTANCE::m_state offset mismatch");
            static_assert(offsetof(FSW_B_EVENT_INSTANCE, m_pEventData) == 0x10, "FSW_B_EVENT_INSTANCE::m_pEventData offset mismatch");
            static_assert(offsetof(FSW_B_EVENT_INSTANCE, m_pDescription) == 0x30, "FSW_B_EVENT_INSTANCE::m_pDescription offset mismatch");
            static_assert(offsetof(FSW_B_EVENT_INSTANCE, m_name) == 0x40, "FSW_B_EVENT_INSTANCE::m_name offset mismatch");
            static_assert(offsetof(FSW_B_EVENT_INSTANCE, m_state0068) == 0x68, "FSW_B_EVENT_INSTANCE::m_state0068 offset mismatch");
            static_assert(offsetof(FSW_B_EVENT_INSTANCE, m_pFmodInstance) == 0x78, "FSW_B_EVENT_INSTANCE::m_pFmodInstance offset mismatch");
            static_assert(offsetof(FSW_B_EVENT_INSTANCE, m_flag00B0) == 0xB0, "FSW_B_EVENT_INSTANCE::m_flag00B0 offset mismatch");
            static_assert(offsetof(FSW_B_EVENT_INSTANCE, m_attributes) == 0x80, "FSW_B_EVENT_INSTANCE::m_attributes offset mismatch");
        }
    };

    // The base constructor (0x1809E91E8) writes nothing past 0xB1; the derived one starts at 0xC0.
    static_assert(sizeof(FSW_B_EVENT_INSTANCE) == 0xC0, "FSW_B_EVENT_INSTANCE size mismatch");
} // namespace SDK::wh
