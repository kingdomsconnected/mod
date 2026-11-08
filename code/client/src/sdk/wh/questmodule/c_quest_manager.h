#pragma once

#include "c_quest.h"

#include "../../types.h"
#include "../conceptmodule/c_node_manager.h"

namespace SDK::wh::questmodule {
    /**
     * Every quest the graph has loaded, as weak handles into the concept
     * resource registry.
     *
     * The journal reads it through these two functions and nothing else,
     * which is what makes them the seam a server-authored quest arrives
     * through. KCDC detours them rather than calling them.
     */
    class C_QuestManager final: public conceptmodule::C_NodeManager {
      public:
        /** The one the game built, or null before the quest module is up. */
        static C_QuestManager *GetInstance();

        /** Every quest whose progress port reads other than `None`. `result` is game-owned, so it has to be one the game can free. */
        void CollectActiveQuests(SDK::StdVector<C_Quest *> &result) const;

        /** The quest of that node name, with a reference the caller owns. The native reads through the `CryStringT`, so a bare `char *` matches nothing. */
        C_Quest *FindQuestByName(const SDK::CryStringT<char> &name) const;
    };
} // namespace SDK::wh::questmodule
