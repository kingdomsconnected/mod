#pragma once

#include "../../patterns.h"
#include "../../types.h"
#include "../framework/i_event_log_listener.h"
#include "../framework/i_ui_hud_events.h"
#include "../questmodule/e_log_type.h"
#include "../questmodule/i_ui_hud_events_quest.h"
#include "../rpgmodule/i_location_listener.h"
#include "c_ui_flash_base.h"

#include <utils/hooking/hooking.h>

namespace SDK::wh::guimodule {
    /** Concrete native HUD-event, tutorial, and notification presenter. */
    class C_UIHudEvents : public C_UIFlashBase,
                          public questmodule::I_UIHudEventsQuest,
                          public framework::I_UIHudEvents,
                          public framework::I_EventLogListener,
                          public rpgmodule::I_LocationListener {
      public:
        ~C_UIHudEvents() override = default;

        static C_UIHudEvents *GetInstance() {
            auto *hudEvents = framework::I_UIHudEvents::GetInstance();
            if (hudEvents == nullptr) {
                return nullptr;
            }

            return reinterpret_cast<C_UIHudEvents *>(
                reinterpret_cast<unsigned char *>(hudEvents) - 0x60);
        }

        /**
         * The game's own "quest updated" toast for one objective, by the log
         * type its new state carries. `BuildShowObjectiveEvent` (0x180DC6B84)
         * sends the same Flash command after resolving the objective's quest
         * through its parent node, which a runtime-built objective lacks, so
         * this sends it with the quest's values directly. The trailing marker
         * index and identifier stay empty.
         */
        void ShowObjectiveEvent(questmodule::E_LogType logType, const CryStringT<char> &objectiveName, int questType, int trackingId, const CryStringT<char> &questName, const CryStringT<char> &questTitle) {
            const ScopedCryString command("ShowObjectiveEvent");
            const ScopedCryString identifier("");
            const int eventType   = static_cast<int>(logType) + 2;
            const int markerIndex = -1;
            hook::this_call<bool>(gPatterns.C_UIHudEvents__InvokeQuestFlashCommand, this, &command.Get(), &eventType, &objectiveName, &questType, &trackingId, &questName, &questTitle, &markerIndex, &identifier.Get());
        }

      private:
        void ShowQuestEvent(const CryStringT<char> &questName, questmodule::E_QuestProgress progress, const CryStringT<char> &uiName, bool localizableName, int questType) override;
        void QuestOpaque01() override;
        void QuestOpaque02() override;
        void QuestOpaque03() override;
        void QuestOpaque04() override;

        void ShowItemTransfer(const CryGUID *itemClass, int count, unsigned char direction) override;
        void ShowItemCount(const CryGUID *itemClass, int count) override;
        void HideItemCount(const CryGUID *itemClass) override;
        void ShowGeneralNotification(const CryStringT<char> &message) override;
        void ShowLevelUp(const CryStringT<char> &stat, const CryStringT<char> &statName, int type, int level, int perkPoints) override;
        void ShowLevelXpGain(const CryStringT<char> &stat, const CryStringT<char> &statName, int type) override;
        void ShowPerkUsed(const CryStringT<char> &iconName, const CryStringT<char> &uiName) override;
        void ShowPerkGained(const CryStringT<char> &iconName, const CryStringT<char> &uiName) override;
        void ShowComboLearned(const CryStringT<char> &iconName, const CryStringT<char> &uiName) override;
        void ShowReputationChanged(int eventId, const CryStringT<char> &message) override;
        void ShowRandomEventResult(int type, const CryStringT<char> &result, const CryStringT<char> &name) override;
        void ShowDiscoveredCodex(const CryStringT<char> &name, const CryStringT<char> &iconId, const CryGUID *perkId) override;
        void ShowDiscoveredPoi(int kind, const void *name, const void *a3, const void *guid) override;
        void ShowDiscoveredLocation(int kind, const void *name, const void *a3, const void *guid) override;
        void HideCodexActionHint() override;
        void ShowUnlockedShieldPaintingIngredient(unsigned char kind, const CryStringT<char> &name) override;
        void ShowTutorial(const CryStringT<char> &tutorialName) override;
        void HideTutorial(const CryStringT<char> &tutorialName) override;
        void HideCurrentTutorial() override;
        void Opaque19(bool value) override;
        void Opaque20(const void *tutorialName) override;
        void Opaque21() override;
        void ClearNotificationQueue() override;
        void Opaque23() override;
        void Opaque24() override;
        void Opaque25() override;

        void EventLogOpaque00() override;

        void LocationOpaque00() override;
        void LocationOpaque01() override;
        void LocationOpaque02() override;
        void LocationOpaque03() override;
        void LocationOpaque04() override;
        void LocationOpaque05() override;
        void LocationOpaque06() override;
        void LocationOpaque07() override;
        void LocationOpaque08() override;
        void LocationOpaque09() override;

        void *m_pState0078;            // 0x78
        void *m_pEventRecordsBegin;    // 0x80, 0x20-byte elements
        void *m_pEventRecordsEnd;      // 0x88
        void *m_pEventRecordsCapacity; // 0x90
    };

    static_assert(sizeof(C_UIHudEvents) == 0x98,
                  "C_UIHudEvents size mismatch");
} // namespace SDK::wh::guimodule
