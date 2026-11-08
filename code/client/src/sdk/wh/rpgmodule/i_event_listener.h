#pragma once

#include "c_event.h"

namespace SDK::wh::rpgmodule {
    /**
     * Registered through `I_EventManager` slots 2 and 3.
     * `C_EventManager::DispatchEvents` drains the pending queue at the top of
     * the RPG tick and calls slot 0 once per event; nothing else is read off a
     * listener. Delivery is after `ProcessEvent` applied the effects, so a
     * listener observes and cannot refuse.
     */
    class I_EventListener {
      public:
        virtual void OnEvent(C_Event *event) = 0;
    };
} // namespace SDK::wh::rpgmodule
