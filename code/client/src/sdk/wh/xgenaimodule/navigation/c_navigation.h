#pragma once

#include "../../../patterns.h"
#include "../c_npc_manager.h"

#include <utils/hooking/hooking.h>

#include <cstddef>

namespace SDK::wh::xgenaimodule::navigation {
    /**
     * XGenAI's navigation: the navmesh, its tile cache and the NPCs' movement
     * tasks. Built by `0x180D39C78` into `C_XGenAIModule+0x78`.
     *
     * Only the movement-task registry is claimed. The ctor stores it at
     * `+0x168` (an 0x50-byte object built by `0x180D3A630`, with no vtable and
     * so no RTTI name), and slot 10 -- a folded getter answering `this+0x168`
     * -- is how the game's own movement calls reach it (`0x1807D7A7C`,
     * `0x1807D9670`). Never constructed by the mod; only the prefix is
     * asserted.
     */
    class C_Navigation {
      public:
        /**
         * Stops this NPC's AI movement the way the behaviour tree's
         * StopMovement node does (`0x1807D85F4`, which passes false):
         * `StopNpcMovement` cancels the request the NPC's movement task is
         * running and has CryEngine stop the body.
         *
         * The movement system runs a move on its own once a brain has
         * started it, so a brain that is held mid-move leaves the body
         * moving; this is what takes that move back. True instead -- what
         * `C_NPC` slots 84 and 85 pass -- adds a movement-system call by
         * entity id and a flag on the stop request, neither of which the
         * node needs.
         */
        void StopNpcMovement(C_NPC *npc) const {
            hook::call<void>(gPatterns.xgenaimodule__StopNpcMovement, m_pMovementTasks, npc, false);
        }

      private:
        std::byte m_opaque0000[0x168];
        void *m_pMovementTasks; // 0x168, keyed by each NPC's WUID (`FindOrCreateMovementTask` 0x180A00914)

        static void AssertLayout() {
            static_assert(offsetof(C_Navigation, m_pMovementTasks) == 0x168, "C_Navigation::m_pMovementTasks offset mismatch");
        }
    };
} // namespace SDK::wh::xgenaimodule::navigation
