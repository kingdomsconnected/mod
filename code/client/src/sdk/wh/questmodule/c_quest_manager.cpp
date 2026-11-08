#include "c_quest_manager.h"

#include "c_quest_module.h"

#include "../../patterns.h"
#include "../c_wh_global_environment.h"

#include <utils/hooking/hooking.h>

namespace SDK::wh::questmodule {
    C_QuestManager *C_QuestManager::GetInstance() {
        // Every hop is genuinely absent before the game has brought its
        // modules up, which is most of the time a client spends in the menu.
        C_GlobalEnvironment *environment = C_GlobalEnvironment::GetInstance();
        if (environment == nullptr) {
            return nullptr;
        }

        C_QuestModule *questModule = environment->GetQuestModule();
        return questModule != nullptr ? questModule->GetQuestManager() : nullptr;
    }

    void C_QuestManager::CollectActiveQuests(SDK::StdVector<C_Quest *> &result) const {
        hook::this_call<void>(gPatterns.C_QuestManager__CollectActiveQuests, this, &result);
    }

    C_Quest *C_QuestManager::FindQuestByName(const SDK::CryStringT<char> &name) const {
        C_Quest *quest = nullptr;
        hook::this_call<void>(gPatterns.C_QuestManager__FindQuestByName, this, &quest, &name);
        return quest;
    }
} // namespace SDK::wh::questmodule
