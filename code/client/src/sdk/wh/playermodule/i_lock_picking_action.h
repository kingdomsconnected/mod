#pragma once

#include "../../cryengine/action/i_mannequin_action.h"
#include "../../patterns.h"

#include <cstddef>

namespace SDK::wh::playermodule {
    /**
     * The Mannequin actions the lockpicking minigame plays on its player's
     * body, all built by `C_LockPicking::CreateAndQueueAction` (0x1808998E0)
     * and queued on the player's own controller: the idle and the loop it
     * queues ahead of itself, the turn, the turn's success and failure, the
     * failed idle and the three ways out. Each is a `TAction` with the
     * fragment, the lock's one fragment tag (`chest`, `cartChest`,
     * `pillory`, none for a door) and the priority the minigame picked
     * (`ResolveLockPickingActionFragment`, 0x180899B58); what the classes
     * add is the minigame's own callbacks.
     *
     * Only its identity is typed. No derived class overrides
     * `OnFragmentStarted`, and no other vtable holds it, so that slot says
     * whether an action is one of these.
     */
    class I_LockPickingAction {
      public:
        static bool Is(const IAction &action) {
            return action.GetVirtualTable()[kOnFragmentStartedSlot] == gPatterns.I_LockPickingAction__OnFragmentStarted;
        }

      private:
        static constexpr std::size_t kOnFragmentStartedSlot = 14;
    };
} // namespace SDK::wh::playermodule
