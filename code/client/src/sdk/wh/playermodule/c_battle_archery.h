#pragma once

#include "../../patterns.h"

#include "../entitymodule/c_actor.h"
#include "c_battlement.h"
#include "c_minigame.h"
#include "e_minigame_type.h"

#include <utils/hooking/hooking.h>

#include <cstddef>
#include <cstdint>

namespace SDK::wh::playermodule {
    /**
     * Kind 10 of `C_PlayerManager::CreateMinigame` (primary vtable
     * 0x183F67A88): a body at a `C_BattlementSlot`, leaning out of a window
     * or over a battlement, or shooting from it with a bow.
     *
     * `Start` binds the slot and sets the state `DetermineBattleArcheryMode`
     * (0x182E58858) picks: 1, the lean, for a body with no bow ready. The
     * lean is three `C_CallbackAction`s on the body's own controller --
     * `WallLookIn`, the looping `WallLook` once it ends, and `WallLookOut`
     * from `BeginExit` -- while `UpdateArcheryTransition` (0x182E5A5F8, slot
     * 20) slides the entity onto the slot's exit transform and, after an
     * exit, back, then calls `Finish`. Every input, camera and action-map
     * effect is behind `C_Actor::IsPlayerControlled`.
     *
     * `C_Player::Update` starts it for the player on its own: every update
     * it hands the body to `C_BattlementManager::TryStartBattleArchery`
     * (0x180DCB804), which starts the lean at the first nearby slot the body
     * faces (`C_BattlementSlot::CanBeUsedBy`, 0x182E58120).
     */
    class C_BattleArchery: public C_Minigame {
      public:
        /** The actor's battle archery, created when missing and `create` is set (`C_Actor::GetMinigame`). */
        static C_BattleArchery *Of(entitymodule::C_Actor *actor, bool create) {
            return static_cast<C_BattleArchery *>(hook::this_call<C_Minigame *>(gPatterns.C_Actor__GetMinigame, actor, E_MinigameType::BattleArchery, create, false));
        }

        /**
         * Binds `slot` and starts (0x182E5AF34), as
         * `C_BattlementManager::StartBattleArcheryAtSlot` (0x182E5B5C8) does
         * after finding or creating the minigame. 0 when it started; 2 when
         * the interaction could not be prepared; 1 when an archery mode
         * found no aim.
         */
        std::uint8_t Start(C_BattlementSlot *slot, std::int32_t startMode) {
            return hook::this_call<std::uint8_t>(gPatterns.C_BattleArchery__Start, this, slot, startMode);
        }

        /** The slot `Start` bound, until `Finish` lets it go. */
        C_BattlementSlot *GetSlot() const {
            return m_slot;
        }

        /** Leaning out (state 1), from `Start` until `BeginExit` sets state 0 and queues `WallLookOut`. */
        bool IsLeaning() const {
            return m_state == kLeaning;
        }

        /**
         * In the slot and not yet sent out: leaning (1) or, once a bow is drawn
         * in it, shooting (2 and 3). `BeginExit` sets state 0 from 1 and 2;
         * from 3 it calls `Finish` once the actor can leave the aim, and
         * until then only marks the exit pending.
         */
        bool IsInSlot() const {
            return m_state != kNone;
        }

      private:
        static constexpr std::int32_t kNone    = 0;
        static constexpr std::int32_t kLeaning = 1;

        std::byte m_opaque0068[0x10];
        C_BattlementSlot *m_slot;    // 0x78
        std::byte m_opaque0080[0x4]; // 0x80; 1 sliding, 2 in place, 3 finished
        std::int32_t m_state;        // 0x84; 0 none, 1 lean, 2 and 3 archery

        static void AssertLayout() {
            static_assert(offsetof(C_BattleArchery, m_slot) == 0x78, "C_BattleArchery::m_slot offset mismatch");
            static_assert(offsetof(C_BattleArchery, m_state) == 0x84, "C_BattleArchery::m_state offset mismatch");
        }
    };
} // namespace SDK::wh::playermodule
