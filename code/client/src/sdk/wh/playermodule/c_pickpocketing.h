#pragma once

#include "../../patterns.h"
#include "../../types.h"
#include "../framework/wuid.h"
#include "../shared/c_signal.h"
#include "c_minigame.h"

#include <utils/hooking/hooking.h>

#include <cstddef>
#include <cstdint>

namespace SDK::wh::playermodule {
    /**
     * Where a pickpocketing attempt stands, the dword at `C_Pickpocketing+0x78`.
     *
     * Every value is written by the minigame itself and broadcast through
     * `C_Minigame::NotifyStateChanged` (0x1808980D8) as it is written.
     */
    enum class E_PickpocketingState : std::uint32_t {
        /** No attempt; the constructor's value. */
        Idle = 0,

        /** `Start` ran: the thief is holding the charge. */
        Charging = 1,

        /** The charge passed its threshold (`+0x8C`); still charging. */
        Charged = 2,

        /** `OpenLootUI` put the 11-tile grid up. */
        Looting = 3,

        /** `Finish`, or `InterruptPickpocketing`, ended it. */
        Finished = 4,
    };

    /**
     * The taken items `ResolvePickpocketingOutcome` receives are the loot
     * screen's `StackVector<WUID, 11>`, one WUID per item stack the thief took.
     * Eleven is the grid: two tile sets of six and five (`OpenLootUI`). The
     * offsets below are the ones the resolver itself reads.
     */
    static_assert(SDK::StackVector<framework::WUID, 11>::InlineInUseOffset() == 0x58, "StackVector<WUID, 11>::inlineInUse offset mismatch");
    static_assert(SDK::StackVector<framework::WUID, 11>::FirstOffset() == 0x70, "StackVector<WUID, 11>::first offset mismatch");
    static_assert(SDK::StackVector<framework::WUID, 11>::LastOffset() == 0x78, "StackVector<WUID, 11>::last offset mismatch");
    static_assert(sizeof(SDK::StackVector<framework::WUID, 11>) == 0x88, "StackVector<WUID, 11> size mismatch");

    /**
     * The pickpocketing minigame, from the thief's side. 0xF8 bytes: kind 8 of
     * `C_PlayerManager::CreateMinigame`, constructor 0x182EA6024.
     *
     * The victim is the base's interaction target -- `GetVictimActor`
     * (0x182EABE24) resolves exactly that id through the actor system -- and
     * the loot grid is built out of the victim *soul's* inventory, so whatever
     * that inventory holds when `OpenLootUI` runs is what can be stolen.
     */
    class C_Pickpocketing: public C_Minigame {
      public:
        /** The victim's entity id; see the class comment. */
        SDK::EntityId GetVictimEntityId() const {
            return GetInteractionEntityId();
        }

        E_PickpocketingState GetState() const {
            return m_state;
        }

        /**
         * The native end of an attempt, vtable slot 5 (0x182EB2614): closes the
         * charge binding and the loot screen -- whose close destroys the loot
         * callback rather than calling it -- and writes `Finished`. Resolves
         * nothing, so no failure message, no crime message and no transfer.
         *
         * Called through the address rather than the vtable: this type carries
         * no virtuals, and the slot is proven against `UpdateCharge`, the loot
         * callback (0x18203E510) and `FinishAndUnregisterMinigame`, which all
         * reach it at `+0x28`.
         */
        void Finish() {
            hook::this_call<void>(gPatterns.C_Pickpocketing__Finish, this);
        }

      private:
        SDK::wh::shared::C_Signal m_signal0068; // 0x68; ctor 0x182EA6024 dispatch unk_185669430.
        E_PickpocketingState m_state; // 0x78
        std::byte m_padding007C[0x4];
        void *m_ptr0080;            // 0x80; ctor 0.
        float m_value0088;          // 0x88; ctor 1.0f.
        float m_chargeThreshold;    // 0x8C; ctor 0; Charged once passed (see E_PickpocketingState).
        std::byte m_opaque0090[0x8];
        // 0x98 and 0xC0: two wh::framework::T_TimeoutTimer<wh::rpgmodule::T_GameWorldTimePolicy>
        // (RTTI vftable at +0, dword 17 at +8, three zeroed qwords); 0x28 bytes each.
        std::byte m_timer0098[0x28];
        std::byte m_timer00C0[0x28];
        std::byte m_opaque00E8[0x8];
        void *m_ptr00F0;            // 0xF0; ctor 0.

        static void AssertLayout() {
            static_assert(offsetof(C_Pickpocketing, m_state) == 0x78, "C_Pickpocketing::m_state offset mismatch");
            static_assert(offsetof(C_Pickpocketing, m_chargeThreshold) == 0x8C, "C_Pickpocketing::m_chargeThreshold offset mismatch");
            static_assert(offsetof(C_Pickpocketing, m_timer0098) == 0x98, "C_Pickpocketing::m_timer0098 offset mismatch");
            static_assert(offsetof(C_Pickpocketing, m_ptr00F0) == 0xF0, "C_Pickpocketing::m_ptr00F0 offset mismatch");
        }
    };

    static_assert(sizeof(C_Pickpocketing) == 0xF8, "C_Pickpocketing size mismatch");
} // namespace SDK::wh::playermodule
