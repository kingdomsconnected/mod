#pragma once

#include "c_actor_action_item_handling_anim.h"
#include "sdk/wh/playermodule/c_minigame.h"

namespace SDK::wh::entitymodule {
    /**
     * The actor action a minigame runs under. Factory 0x18061868C allocates
     * 0x70 bytes and stores the minigame at +0x68.
     *
     * `Exit` (slot 39, 0x1819C7EB4) ends a still-active minigame through its
     * slot 6 (`Cancel`) when the action was replaced by another actor action
     * (`C_ActionActor::ExitReplacedAction` 0x18068008C passes exit reason 1,
     * which `C_Action::OnExit` 0x1806801C0 keeps at +0x2A), and through its
     * slot 5 (`Finish`) otherwise.
     */
    class C_ActorActionMinigame: public C_ActorAction<I_ActorAction, framework::C_Action<I_ActorAction>> {
      public:
        SDK::wh::playermodule::C_Minigame *GetMinigame() const {
            return m_minigame;
        }

      private:
        SDK::wh::playermodule::C_Minigame *m_minigame;
    };
    static_assert(sizeof(C_ActorActionMinigame) == 0x70, "C_ActorActionMinigame size mismatch");
} // namespace SDK::wh::entitymodule
