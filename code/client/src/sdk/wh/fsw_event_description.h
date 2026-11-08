#pragma once

#include "../fmod/fmod_types.h"
#include "../types.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh {
    /**
     * One FMOD event the wrapper knows, `FSW_EVENT_DESCRIPTION` (0x60 bytes, tagged allocation in
     * `FSW_BANK::FetchEventDescriptions`, constructed at 0x1803C4738). The wrapper keeps one per
     * event path, so every ATL trigger naming the same event shares it.
     *
     * `m_maxDistance` is the event's audible range as the game manages it: the FMOD event's own
     * maximum distance (20 m when it has none), replaced by the trigger's `distance_culling`
     * attribute when one is given (`SATL_F_TriggerImplData::ApplyDistanceCulling`, 0x1803B2344).
     * It only culls: an instance farther than this from the listener is not started
     * (`FSW_B_EVENT_INSTANCE::Start`, 0x1803AF620) and a playing one is stopped
     * (`FSW_B_EVENT_INSTANCE::Update3DAttributes`, 0x1804823C4). Nothing in the game scales an
     * instance's volume by it.
     */
    class FSW_EVENT_DESCRIPTION {
      public:
        double GetMaxDistance() const {
            return m_maxDistance;
        }

        /** The full `event:/...` path the wrapper indexes it by. */
        const char *GetPath() const {
            return m_path.CStr();
        }

        SDK::Fmod::FMOD_STUDIO_EVENTDESCRIPTION *GetFmodDescription() const {
            return m_pFmodDescription;
        }

      private:
        std::int32_t m_state;                                    // 0x00, 3 for a snapshot, which is never placed in the world
        std::byte m_padding0004[0x4];                            // 0x04
        double m_maxDistance;                                    // 0x08
        std::int32_t m_folderHash;                               // 0x10, of the path up to its last '/'
        std::byte m_padding0014[0x4];                            // 0x14
        void *m_pBank;                                           // 0x18, FSW_BANK
        SDK::StdString m_path;                                   // 0x20
        SDK::Fmod::FMOD_STUDIO_EVENTDESCRIPTION *m_pFmodDescription; // 0x40
        void *m_pInstancesHead;                                  // 0x48, std::list of live instances: sentinel
        std::size_t m_instanceCount;                             // 0x50
        bool m_cullsByDistance;                                  // 0x58, 1 for every event: Start parks an out-of-range instance as Waiting rather than dropping it
        bool m_ignoresDistance;                                  // 0x59, exempts the instance from both culls
        bool m_flag005A;                                         // 0x5A, the ctor (0x1803C4738) writes the dword 0x10001 over 0x58..0x5B, so 1
        std::byte m_padding005B[0x5];                            // 0x5B

        static void AssertLayout() {
            static_assert(offsetof(FSW_EVENT_DESCRIPTION, m_maxDistance) == 0x08, "FSW_EVENT_DESCRIPTION::m_maxDistance offset mismatch");
            static_assert(offsetof(FSW_EVENT_DESCRIPTION, m_pBank) == 0x18, "FSW_EVENT_DESCRIPTION::m_pBank offset mismatch");
            static_assert(offsetof(FSW_EVENT_DESCRIPTION, m_path) == 0x20, "FSW_EVENT_DESCRIPTION::m_path offset mismatch");
            static_assert(offsetof(FSW_EVENT_DESCRIPTION, m_pFmodDescription) == 0x40, "FSW_EVENT_DESCRIPTION::m_pFmodDescription offset mismatch");
            static_assert(offsetof(FSW_EVENT_DESCRIPTION, m_cullsByDistance) == 0x58, "FSW_EVENT_DESCRIPTION::m_cullsByDistance offset mismatch");
            static_assert(offsetof(FSW_EVENT_DESCRIPTION, m_ignoresDistance) == 0x59, "FSW_EVENT_DESCRIPTION::m_ignoresDistance offset mismatch");
            static_assert(offsetof(FSW_EVENT_DESCRIPTION, m_pInstancesHead) == 0x48, "FSW_EVENT_DESCRIPTION::m_pInstancesHead offset mismatch");
            static_assert(offsetof(FSW_EVENT_DESCRIPTION, m_flag005A) == 0x5A, "FSW_EVENT_DESCRIPTION::m_flag005A offset mismatch");
        }
    };

    // `mov edx, 60h` against the tag "FSW_EVENT_DESCRIPTION" in FetchEventDescriptions (0x1803C43C5).
    static_assert(sizeof(FSW_EVENT_DESCRIPTION) == 0x60, "FSW_EVENT_DESCRIPTION size mismatch");
} // namespace SDK::wh
