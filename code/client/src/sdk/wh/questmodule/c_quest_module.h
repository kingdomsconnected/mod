#pragma once

#include "c_quest_manager.h"

#include "../framework/c_base_module.h"

#include <cstddef>

namespace SDK::wh::questmodule {
    /**
     * The quest system's slot in the global module registry. It owns one
     * thing the mod needs: the manager it allocates and stores at 0x10 during
     * `Init` (0x18182B864).
     */
    class C_QuestModule final: public framework::C_BaseModule {
      public:
        C_QuestManager *GetQuestManager() const {
            return m_pQuestManager;
        }

      private:
        C_QuestManager *m_pQuestManager; // 0x10

        static void AssertLayout() {
            static_assert(offsetof(C_QuestModule, m_pQuestManager) == 0x10, "C_QuestModule::m_pQuestManager offset mismatch");
        }
    };
} // namespace SDK::wh::questmodule
