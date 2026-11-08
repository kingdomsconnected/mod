#pragma once

#include "e_quest_progress.h"
#include "e_quest_type.h"

#include "../../types.h"

namespace SDK::wh::questmodule {
    /**
     * Quest-event presentation contract implemented by the native HUD, and the
     * one place the quest system speaks to the player outside the journal.
     *
     * It is a secondary base of `C_UIHudEvents`, at complete object +0x58.
     *
     * Only slot 0 is established (0x180DC855C). It resolves the quest's
     * tracking id from the node name and localizes `uiName` unless the caller
     * says not to -- which is the escape that lets mod-authored text reach the
     * popup unresolved. The remaining four queue deferred events.
     */
    class I_UIHudEventsQuest {
      public:
        void InvokeShowQuestEventRaw(const CryStringT<char> &questName, E_QuestProgress progress, const CryStringT<char> &uiName, bool localizableName, E_QuestType questType) {
            ShowQuestEvent(questName, progress, uiName, localizableName, static_cast<int>(questType));
        }

      private:
        /**
         * `questName` is the quest's node name, not its title: it is what the
         * tracking-id lookup keys on. `uiName` is the title, and
         * `localizableName` false pushes it through as written.
         */
        virtual void ShowQuestEvent(const CryStringT<char> &questName, E_QuestProgress progress, const CryStringT<char> &uiName, bool localizableName, int questType) = 0; // +0x00

        virtual void QuestOpaque01() = 0;
        virtual void QuestOpaque02() = 0;
        virtual void QuestOpaque03() = 0;
        virtual void QuestOpaque04() = 0;
    };

    static_assert(sizeof(I_UIHudEventsQuest) == 0x8, "I_UIHudEventsQuest size mismatch");
} // namespace SDK::wh::questmodule
