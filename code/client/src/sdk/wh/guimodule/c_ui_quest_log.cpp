#include "c_ui_quest_log.h"

#include "c_ui_apse.h"

#include "../../patterns.h"

#include <utils/hooking/hooking.h>

namespace SDK::wh::guimodule {
    C_UIQuestLog *C_UIQuestLog::GetInstance() {
        C_UIApse *apse = C_UIApse::GetInstance();
        return apse != nullptr ? apse->GetQuestLog() : nullptr;
    }

    std::int32_t C_UIQuestLog::GetTrackingId(const SDK::CryStringT<char> &questName) const {
        // The native takes the table, not the log that carries it.
        return hook::call<std::int32_t>(gPatterns.LookupQuestTrackingId, &m_trackedNames, &questName);
    }

    bool C_UIQuestLog::IsTracking(const SDK::CryStringT<char> &questName) const {
        return GetTrackingId(questName) != -1;
    }

    std::int32_t C_UIQuestLog::AssignTrackingSlot(const questmodule::C_Quest *quest) {
        // A free function over the table, like LookupQuestTrackingId.
        return hook::call<std::int32_t>(gPatterns.AssignQuestTrackingSlot, &m_trackedNames, &quest->GetName(), quest->GetType());
    }

    void C_UIQuestLog::BeginTrackingQuest(questmodule::C_Quest *quest) {
        hook::this_call<void>(gPatterns.C_UIQuestLog__BeginTrackingQuest, this, quest);
    }

    void C_UIQuestLog::EndTrackingQuest(questmodule::C_Quest *quest) {
        hook::this_call<void>(gPatterns.C_UIQuestLog__EndTrackingQuest, this, quest);
    }
} // namespace SDK::wh::guimodule
