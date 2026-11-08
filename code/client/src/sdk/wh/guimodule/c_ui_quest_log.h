#pragma once

#include "../../types.h"
#include "../questmodule/c_quest.h"
#include "../shared/c_signal.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::guimodule {
    /**
     * The journal page of the pause screen, and the tracked-quest register.
     *
     * Embedded in `C_UIApse` at +0x10C8, constructed there by 0x18146C9A5
     * just ahead of the map at +0x1508, which puts its size at 0x440.
     *
     * Only the name table at +0x40 is established, which is all the tracking
     * natives take: four bands of 24 bytes chosen by `E_QuestType`. A quest
     * starts being followed when `AssignTrackingSlot` (0x180DC8E10) writes
     * its name and stops when `EndTrackingQuest` (0x180DC8EA4) removes it;
     * neither is edge-triggered, so the table before and after the call is
     * how a listener tells a transition from a repeat.
     */
    class C_UIQuestLog final {
      public:
        C_UIQuestLog()                                = delete;
        ~C_UIQuestLog()                               = delete;
        C_UIQuestLog(const C_UIQuestLog &)            = delete;
        C_UIQuestLog &operator=(const C_UIQuestLog &) = delete;

        /** Null before the GUI module has built its pages. */
        static C_UIQuestLog *GetInstance();

        bool IsTracking(const SDK::CryStringT<char> &questName) const;

        /** The quest's slot within its type's band, or -1 when it is not being followed. */
        std::int32_t GetTrackingId(const SDK::CryStringT<char> &questName) const;

        /**
         * Claims a slot in the quest's type band for its name, the step the
         * track button and the auto-track take before BeginTrackingQuest.
         * Answers the slot, or -1 when the band is full.
         */
        std::int32_t AssignTrackingSlot(const questmodule::C_Quest *quest);

        /**
         * The game's own start and stop, the same two the track button, the
         * auto-track and the savegame restore go through. Both subscribe or
         * unsubscribe the quest's objectives and raise the log's changed
         * signal, which is what redraws the quest markers. Start writes no
         * name: a quest is followed only once AssignTrackingSlot has given it
         * a slot. Stop removes the name itself.
         */
        void BeginTrackingQuest(questmodule::C_Quest *quest);
        void EndTrackingQuest(questmodule::C_Quest *quest);

      private:
        // Constructor 0x18146D5B4.
        const void *m_pVTable;                 // 0x00
        void *m_ptr0008;                       // 0x08; ctor 0
        const void *m_pVTables0010[2];         // 0x10; second and third C_UIQuestLog tables
        SDK::wh::shared::C_Signal m_signal0020; // 0x20; dispatch unk_185665AC0
        SDK::wh::shared::C_Signal m_signal0030; // 0x30; dispatch unk_185665430
        std::byte m_trackedNames[0x60]; // 0x40, the four bands
        std::byte m_set00A0[0x10];             // 0xA0; std::set<void *>
        std::byte m_map00B0[0x10];             // 0xB0; std::map {head, size}
        std::byte m_questList[0x1E0];          // 0xC0; flash list "ApseQuestLogList" (sub_180BC8484)
        std::byte m_diary[0xD8];               // 0x2A0; C_UIExtendedInfo "ApseQuestLogDiary" (sub_180BC8628)
        std::byte m_modalDialog[0xA8]; // C_UIModalDialogInfo (its sizeof is asserted in c_ui_modal_dialog_info.h), 0x378; C_UIModalDialogInfo "ApseModalDialog"
        void *m_pApse;                         // 0x420; the owning C_UIApse
        std::byte m_map0428[0x10];             // 0x428; std::map {head, size}
        std::uint16_t m_value0438;             // 0x438; ctor 0
        std::byte m_padding043A[0x6];

        static void AssertLayout() {
            static_assert(offsetof(C_UIQuestLog, m_signal0020) == 0x20, "C_UIQuestLog::m_signal0020 offset mismatch");
            static_assert(offsetof(C_UIQuestLog, m_trackedNames) == 0x40, "C_UIQuestLog::m_trackedNames offset mismatch");
            static_assert(offsetof(C_UIQuestLog, m_questList) == 0xC0, "C_UIQuestLog::m_questList offset mismatch");
            static_assert(offsetof(C_UIQuestLog, m_diary) == 0x2A0, "C_UIQuestLog::m_diary offset mismatch");
            static_assert(offsetof(C_UIQuestLog, m_modalDialog) == 0x378, "C_UIQuestLog::m_modalDialog offset mismatch");
            static_assert(offsetof(C_UIQuestLog, m_pApse) == 0x420, "C_UIQuestLog::m_pApse offset mismatch");
            static_assert(offsetof(C_UIQuestLog, m_value0438) == 0x438, "C_UIQuestLog::m_value0438 offset mismatch");
        }
    };

    static_assert(sizeof(C_UIQuestLog) == 0x440, "C_UIQuestLog size mismatch");
} // namespace SDK::wh::guimodule
