#pragma once

#include "../../patterns.h"
#include "../shared/c_signal.h"
#include "c_dice_game_player_model.h"
#include "i_dice_game_model_delegate.h"
#include "i_dice_game_model_state.h"

#include <utils/hooking/hooking.h>

#include <cstddef>
#include <cstdint>
#include <memory>

namespace SDK::wh::playermodule {
    /**
     * The rules side of a dice game: 0x160 bytes, `MakeDiceGameModel`
     * (0x182E7C610, `make_shared` of 0x170) around constructor 0x182E7F41C,
     * held by `C_Dice+0x4A0`. Two seats, the current state, the target score
     * and the per-seat scoring rules.
     *
     * Every move goes through the current state. The game advances a state
     * either at once or, for a seat with an AI, after a delay through
     * `TryAdvanceGameState` (0x182E98DC4), which offers it to both seats' AIs
     * before the state's own `Advance`.
     */
    class C_DiceGameModel {
      public:
        /** The secondary base every state method takes. */
        I_DiceGameModelDelegate *GetDelegate() {
            return &m_delegate;
        }

        /** Seat 0 is the local player, seat 1 the opponent; anything else is null. */
        C_DiceGamePlayerModel *GetPlayer(std::int32_t seat) const {
            return seat == 0 || seat == 1 ? m_players[seat].get() : nullptr;
        }

        I_DiceGameModelState *GetState() const {
            return m_state.get();
        }

        std::int32_t GetTargetScore() const {
            return m_targetScore;
        }

        /**
         * Asks the current state to hold or release die `dieIndex` of the
         * seat whose turn it is (0x182E97F64). PlayerTurn answers by queueing
         * Holding, which hands back to PlayerTurn on its next advance.
         */
        bool RequestSetDieHeld(std::uint32_t dieIndex, bool held) {
            return hook::this_call<bool>(gPatterns.C_DiceGameModel__RequestSetDieHeld, this, dieIndex, held);
        }

        /** Scores the held dice and throws the rest, when the state allows it (0x182E9848C). */
        bool RequestScoreSelectedDice() {
            return hook::this_call<bool>(gPatterns.C_DiceGameModel__RequestScoreSelectedDice, this);
        }

      private:
        std::byte m_vftable0000[0x8];      // 0x00; C_DiceGameModel vftable (ctor 0x182E7F41C).
        void *m_weakThisPtr;               // 0x08; enable_shared_from_this weak pointer, ctor 0.
        void *m_weakThisRep;               // 0x10; its control block, ctor 0.
        I_DiceGameModelDelegate m_delegate;                    // 0x18
        std::shared_ptr<C_DiceGamePlayerModel> m_players[2];   // 0x20, 0x30
        SDK::wh::shared::C_Signal m_signal0040;  // 0x40; dispatch unk_1855ED778.
        std::byte m_perSeat0050[2][0x40];         // 0x50; two 0x40-byte objects built by sub_180AD945C; type unproven.
        std::byte m_perSeat00D0[2][0x18];         // 0xD0; two 0x18-byte containers (unknown_libname_15); type unproven.
        std::int32_t m_targetScore; // 0x100
        std::byte m_padding0104[0x4];
        std::shared_ptr<I_DiceGameModelState> m_state; // 0x108
        std::byte m_opaque0118[0x8];
        std::uint32_t m_value0120;         // 0x120; ctor 0.
        bool m_flag0124;                   // 0x124; ctor 0.
        std::byte m_padding0125[0x3];
        void *m_ptr0128;                   // 0x128; ctor 0.
        void *m_ptr0130;                   // 0x130; ctor 0.
        std::uint32_t m_value0138;         // 0x138; ctor 0.
        std::int32_t m_idleTimeout;        // 0x13C; ctor copies s_cvDiceIdleTimeout.
        void *m_ptr0140;                   // 0x140; ctor 0.
        void *m_ptr0148;                   // 0x148; ctor 0.
        void *m_ptr0150;                   // 0x150; ctor 0.
        void *m_ptr0158;                   // 0x158; ctor 0.

        static void AssertLayout() {
            static_assert(offsetof(C_DiceGameModel, m_delegate) == 0x18, "C_DiceGameModel::m_delegate offset mismatch");
            static_assert(offsetof(C_DiceGameModel, m_players) == 0x20, "C_DiceGameModel::m_players offset mismatch");
            static_assert(offsetof(C_DiceGameModel, m_targetScore) == 0x100, "C_DiceGameModel::m_targetScore offset mismatch");
            static_assert(offsetof(C_DiceGameModel, m_state) == 0x108, "C_DiceGameModel::m_state offset mismatch");
            static_assert(offsetof(C_DiceGameModel, m_signal0040) == 0x40, "C_DiceGameModel::m_signal0040 offset mismatch");
            static_assert(offsetof(C_DiceGameModel, m_perSeat00D0) == 0xD0, "C_DiceGameModel::m_perSeat00D0 offset mismatch");
            static_assert(offsetof(C_DiceGameModel, m_idleTimeout) == 0x13C, "C_DiceGameModel::m_idleTimeout offset mismatch");
            static_assert(offsetof(C_DiceGameModel, m_ptr0158) == 0x158, "C_DiceGameModel::m_ptr0158 offset mismatch");
        }
    };

    static_assert(sizeof(C_DiceGameModel) == 0x160, "C_DiceGameModel size mismatch");
} // namespace SDK::wh::playermodule
