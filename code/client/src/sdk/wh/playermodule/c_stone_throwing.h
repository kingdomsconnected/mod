#pragma once

#include "../../patterns.h"

#include "../../types.h"
#include "../entitymodule/c_actor.h"
#include "c_minigame.h"
#include "c_stone_throwing_pile.h"
#include "e_minigame_type.h"

#include <utils/hooking/hooking.h>

#include <cstddef>
#include <cstdint>

namespace SDK::wh::playermodule {
    /**
     * Throwing a stone from a pile over a battlement: kind 9 of
     * `C_PlayerManager::CreateMinigame`, 0xC8 bytes (constructor 0x182EA67C0),
     * one per actor and created on first use.
     *
     * `StartWithPile` grabs: it checks the pile (`CanUse`), binds the actor
     * to it, and queues the grab fragment, whose end attaches the stone to
     * the right hand and -- on a pile whose path is one node, or for an
     * actor `IsPlayerControlled` denies -- queues the throw at once; otherwise
     * the actor holds the stone until `RequestThrow`. The throw fragment's detach
     * event lets the stone go (`DetachStoneFromHand`, 0x182EA8ED8), as a rigid
     * body pushed along the actor's facing; there is no aim beyond that. The
     * player leaves through `minigame_exit`, which runs `RequestExit` and,
     * after the exit fragment, `Finish`.
     *
     * Every input, camera, save-lock and interaction-prompt effect is behind
     * `C_Actor::IsPlayerControlled`, so the minigame runs on any actor; for a
     * remote player's body the mod answers false at each of those sites
     * (`NativePlayerModule`'s local-player sites), which leaves exactly the
     * animation and the stone.
     */
    class C_StoneThrowing: public C_Minigame {
      public:
        /** The actor's stone throwing, created when missing and `create` is set (`C_Actor::GetMinigame`). */
        static C_StoneThrowing *Of(entitymodule::C_Actor *actor, bool create) {
            return static_cast<C_StoneThrowing *>(hook::this_call<C_Minigame *>(gPatterns.C_Actor__GetMinigame, actor, E_MinigameType::StoneThrowing, create, false));
        }

        /**
         * Grabs a stone from the pile `pileEntityId` (0x182EB14A8). 0 when it
         * started; 1 when the pile is missing or refuses the actor (too far,
         * not facing it, empty); 2 when the interaction could not be prepared.
         */
        std::uint8_t StartWithPile(EntityId pileEntityId) {
            return hook::this_call<std::uint8_t>(gPatterns.C_StoneThrowing__StartWithPile, this, pileEntityId);
        }

        /**
         * Throws the stone in the hands (0x182EB2FDC): the player's `throw`
         * action, or the grab's end itself on a one-node path or for an actor
         * `IsPlayerControlled` denies. Queues the throw fragment, whose detach
         * event lets the stone go.
         */
        void RequestThrow() {
            hook::this_call<void>(gPatterns.C_StoneThrowing__RequestThrow, this);
        }

        /** The pile `StartWithPile` bound, null before the first. */
        C_StoneThrowingPile *GetPile() const {
            return m_pile;
        }

        /** The stone in the hand, 0 when none is. */
        EntityId GetHeldStoneEntityId() const {
            return m_heldStoneEntityId;
        }

      private:
        std::byte m_opaque0068[0x30];
        C_StoneThrowingPile *m_pile;      // 0x98
        std::byte m_opaque00A0[0x8];
        EntityId m_heldStoneEntityId;     // 0xA8
        std::byte m_opaque00AC[0x1C];

        static void AssertLayout() {
            static_assert(offsetof(C_StoneThrowing, m_pile) == 0x98, "C_StoneThrowing::m_pile offset mismatch");
            static_assert(offsetof(C_StoneThrowing, m_heldStoneEntityId) == 0xA8, "C_StoneThrowing::m_heldStoneEntityId offset mismatch");
        }
    };

    static_assert(sizeof(C_StoneThrowing) == 0xC8, "C_StoneThrowing size mismatch");
} // namespace SDK::wh::playermodule
