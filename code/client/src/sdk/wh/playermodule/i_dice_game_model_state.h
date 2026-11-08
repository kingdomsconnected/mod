#pragma once

#include "c_dice_game_player_model.h"
#include "i_dice_game_model_delegate.h"

namespace SDK::wh::playermodule {
    /**
     * One state of a dice turn -- Rolling, PlayerTurn, Holding, Scoring,
     * Passing, Bust, PlayerChange, Ending and the badge states -- as the
     * game model holds it at +0x108. Slots proven against
     * `C_PlayerTurnDiceGameState`'s vtable (0x183F711E0) and the callers named
     * on each; every state keeps its player at +0x18, which slot 4 returns.
     *
     * Transitions never happen inside a slot: each queues the next state on
     * the delegate (`SetGameState`, 0x182E953C8), so calling one is safe from
     * the game thread at any point the engine itself would.
     */
    class I_DiceGameModelState {
      public:
        virtual void Opaque0() = 0;
        virtual void Opaque1() = 0;
        virtual void Opaque2() = 0;
        virtual void Opaque3() = 0;

        /** The seat this state is about (0x1809DD2D0, `+0x18`). */
        virtual C_DiceGamePlayerModel *GetPlayer() = 0; // +0x20

        virtual void Opaque5()  = 0;
        virtual void Opaque6()  = 0;
        virtual void Opaque7()  = 0;
        virtual void Opaque8()  = 0;
        virtual void Opaque9()  = 0;

        /** Ends the game with `player` having given up, result 3 (0x182E8BC24). `C_Dice::RequestExit` calls it in play. */
        virtual bool RequestGiveUp(I_DiceGameModelDelegate *delegate, C_DiceGamePlayerModel *player) = 0; // +0x50

        /** Ends the game with `player` the winner, result 1 (0x182E9A9C0). */
        virtual bool EndWithWin(I_DiceGameModelDelegate *delegate, C_DiceGamePlayerModel *player) = 0; // +0x58

        virtual void Opaque12() = 0;
        virtual void Opaque13() = 0;
        virtual void Opaque14() = 0;
        virtual void Opaque15() = 0;
        virtual void Opaque16() = 0;
        virtual void Opaque17() = 0;
        virtual void Opaque18() = 0;
        virtual void Opaque19() = 0;
        virtual void Opaque20() = 0;
        virtual void Opaque21() = 0;

        /**
         * The current seat banks its turn: scores what it holds first, then
         * hands over (`C_PlayerTurnDiceGameState::RequestPass`, 0x182E98384).
         * `consumeBadge` asks for a badge first; false always here.
         */
        virtual bool RequestPass(I_DiceGameModelDelegate *delegate, bool consumeBadge) = 0; // +0xB0

        virtual void Opaque23() = 0;
        virtual void Opaque24() = 0;
        virtual void Opaque25() = 0;
        virtual void Opaque26() = 0;
        virtual void Opaque27() = 0;
        virtual void Opaque28() = 0;
        virtual void Opaque29() = 0;
        virtual void Opaque30() = 0;

        /** True for PlayerTurn: the seat may hold dice, score or pass. `C_SimpleDiceAI::TryHandleGameState` tests it first. */
        virtual bool IsPlayerTurn() = 0; // +0xF8

        /** True for Holding, the beat between a hold request and PlayerTurn again. */
        virtual bool IsHolding() = 0; // +0x100
    };
} // namespace SDK::wh::playermodule
