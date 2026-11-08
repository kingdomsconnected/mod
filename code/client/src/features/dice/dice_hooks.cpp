#include <utils/safe_win32.h>

#include <MinHook.h>

#include <utils/hooking/hook_function.h>

#include "core/application.h"
#include "features/dice/dice_module.h"

#include "sdk/patterns.h"
#include "sdk/types.h"
#include "sdk/wh/entitymodule/c_actor.h"
#include "sdk/wh/playermodule/c_dice.h"
#include "sdk/wh/playermodule/c_dice_badge.h"
#include "sdk/wh/playermodule/c_dice_game_model.h"
#include "sdk/wh/playermodule/c_dice_game_player_model.h"
#include "sdk/wh/playermodule/c_simple_dice_ai.h"
#include "sdk/wh/playermodule/i_dice_game_model_delegate.h"
#include "sdk/wh/playermodule/i_dice_game_model_state.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace KCDC::Features::Dice {
    namespace {
        // The native receives its secondary UI interface, not C_UIDice's base.
        using ShowDiceScore_t                   = void(__fastcall *)(void *, int, const SDK::CryStringT<char> &, int, int, int, const SDK::CryStringT<char> &, int, int, int, int, int, const SDK::CryStringT<char> &, int, int);
        ShowDiceScore_t g_showDiceScoreOriginal = nullptr;
        void __fastcall ShowDiceScore_Hook(void *self, int target, const SDK::CryStringT<char> &playerName, int playerCurrent, int playerTotal, int playerSelected, const SDK::CryStringT<char> &playerBadgeName, int playerBadgeCurrent, int playerBadgeTotal, int opponentCurrent,
            int opponentTotal, int opponentSelected, const SDK::CryStringT<char> &opponentBadgeName, int opponentBadgeCurrent, int opponentBadgeTotal) {
            g_showDiceScoreOriginal(self, target, playerName, playerCurrent, playerTotal, playerSelected, playerBadgeName, playerBadgeCurrent, playerBadgeTotal, opponentCurrent, opponentTotal, opponentSelected, opponentBadgeName, opponentBadgeCurrent, opponentBadgeTotal);
            if (KCDC::Core::gApplication != nullptr) {
                DiceModule::Get().ApplyScoreNames();
            }
        }

        using RollDice_t                 = void(__fastcall *)(SDK::wh::playermodule::C_DiceGamePlayerModel *, bool);
        using TransitionToRolling_t      = void(__fastcall *)(SDK::wh::playermodule::I_DiceGameModelDelegate *, SDK::wh::playermodule::C_DiceGamePlayerModel *);
        using TransitionToEnding_t       = void(__fastcall *)(SDK::wh::playermodule::I_DiceGameModelDelegate *, SDK::wh::playermodule::C_DiceGamePlayerModel *, std::uint32_t);
        using TryHandleGameState_t       = bool(__fastcall *)(SDK::wh::playermodule::C_SimpleDiceAI *, SDK::wh::playermodule::C_DiceGameModel *, SDK::wh::playermodule::C_DiceGamePlayerModel *, SDK::wh::playermodule::I_DiceGameModelState *, bool *);
        using DiceInput_t                = void(__fastcall *)(SDK::wh::playermodule::C_Dice *);
        using UpdateNavigationCamera_t   = void(__fastcall *)(SDK::wh::playermodule::C_DieSet *, const SDK::Vec3 &, std::int32_t, SDK::Vec3 &, SDK::Quat &);
        using BindBackingItem_t          = bool(__fastcall *)(SDK::wh::playermodule::C_DiceBadge *, std::uint64_t);
        using ForceMetaroleDialog_t      = void(__fastcall *)(SDK::wh::entitymodule::C_Actor *, const SDK::CryStringT<char> &);

        RollDice_t g_rollDiceOriginal                       = nullptr;
        TransitionToRolling_t g_transitionToRollingOriginal = nullptr;
        TransitionToEnding_t g_transitionToEndingOriginal   = nullptr;
        TryHandleGameState_t g_tryHandleGameStateOriginal   = nullptr;
        DiceInput_t g_onHoldDieRequestedOriginal            = nullptr;
        DiceInput_t g_onScoreRequestedOriginal              = nullptr;
        DiceInput_t g_onPassRequestedOriginal               = nullptr;
        DiceInput_t g_initializeTableSceneOriginal          = nullptr;
        DiceInput_t g_finishOriginal                        = nullptr;
        UpdateNavigationCamera_t g_updateNavigationCameraOriginal = nullptr;
        BindBackingItem_t g_bindBackingItemOriginal         = nullptr;
        ForceMetaroleDialog_t g_forceMetaroleDialogOriginal = nullptr;

        /**
         * Every face a dice game shows is decided here. In a match the seat's
         * dealt faces are lent as the throw's override, the luck passes off,
         * for this call only; the game's own buffer comes back untouched.
         */
        void __fastcall RollDice_Hook(SDK::wh::playermodule::C_DiceGamePlayerModel *self, bool selectedOnly) {
            std::vector<std::int32_t> *faces = KCDC::Core::gApplication != nullptr ? DiceModule::Get().TakeThrow(self, selectedOnly) : nullptr;
            if (faces == nullptr) {
                g_rollDiceOriginal(self, selectedOnly);
                return;
            }
            const SDK::wh::playermodule::C_DiceGamePlayerModel::S_ThrowOverride saved = self->LendThrowOverride(faces->data(), faces->size());
            g_rollDiceOriginal(self, selectedOnly);
            self->RestoreThrowOverride(saved);
        }

        /**
         * Hands a seat its throw: after a scored selection, after a change of
         * player, and for the game's opening throw, which the server picks.
         */
        void __fastcall TransitionToRolling_Hook(SDK::wh::playermodule::I_DiceGameModelDelegate *delegate, SDK::wh::playermodule::C_DiceGamePlayerModel *player) {
            if (KCDC::Core::gApplication != nullptr) {
                player = DiceModule::Get().RedirectThrow(delegate, player);
            }
            g_transitionToRollingOriginal(delegate, player);
        }

        void __fastcall UpdateNavigationCamera_Hook(SDK::wh::playermodule::C_DieSet *self, const SDK::Vec3 &playerPosition, std::int32_t selectedDieIndex, SDK::Vec3 &position, SDK::Quat &rotation) {
            g_updateNavigationCameraOriginal(self, playerPosition, selectedDieIndex, position, rotation);
            if (KCDC::Core::gApplication != nullptr) {
                DiceModule::Get().OnNavigationCameraUpdated(self, position, rotation);
            }
        }

        /** Every end of a game passes here: a banked win, a give-up, a forced end. */
        void __fastcall TransitionToEnding_Hook(SDK::wh::playermodule::I_DiceGameModelDelegate *delegate, SDK::wh::playermodule::C_DiceGamePlayerModel *player, std::uint32_t result) {
            g_transitionToEndingOriginal(delegate, player, result);
            if (KCDC::Core::gApplication != nullptr) {
                DiceModule::Get().OnNativeEnding(delegate, player, result);
            }
        }

        /** The opponent seat's AI, asked about each state. In a match it never decides anything. */
        bool __fastcall TryHandleGameState_Hook(SDK::wh::playermodule::C_SimpleDiceAI *self, SDK::wh::playermodule::C_DiceGameModel *model, SDK::wh::playermodule::C_DiceGamePlayerModel *player, SDK::wh::playermodule::I_DiceGameModelState *state, bool *handled) {
            if (KCDC::Core::gApplication != nullptr) {
                if (const std::optional<bool> answer = DiceModule::Get().AnswerForAI(model, player, state, handled)) {
                    return *answer;
                }
            }
            return g_tryHandleGameStateOriginal(self, model, player, state, handled);
        }

        /** The local player's `dice_hold` key. Frozen while a move waits on the server. */
        void __fastcall OnHoldDieRequested_Hook(SDK::wh::playermodule::C_Dice *self) {
            if (KCDC::Core::gApplication != nullptr && !DiceModule::Get().AllowsHold(self)) {
                return;
            }
            g_onHoldDieRequestedOriginal(self);
        }

        /** The local player's `dice_score` key: in a match it goes to the server and is played when committed. */
        void __fastcall OnScoreRequested_Hook(SDK::wh::playermodule::C_Dice *self) {
            if (KCDC::Core::gApplication != nullptr && !DiceModule::Get().AllowsMove(self, Shared::Game::Dice::MoveKind::Continue)) {
                return;
            }
            g_onScoreRequestedOriginal(self);
        }

        /** The local player's `dice_pass` key, likewise. */
        void __fastcall OnPassRequested_Hook(SDK::wh::playermodule::C_Dice *self) {
            if (KCDC::Core::gApplication != nullptr && !DiceModule::Get().AllowsMove(self, Shared::Game::Dice::MoveKind::Pass)) {
                return;
            }
            g_onPassRequestedOriginal(self);
        }

        /** The table's seat and badge tag points have just been resolved, and both actors snapped to them. */
        void __fastcall InitializeTableScene_Hook(SDK::wh::playermodule::C_Dice *self) {
            g_initializeTableSceneOriginal(self);
            if (KCDC::Core::gApplication != nullptr) {
                DiceModule::Get().OnTableSceneInitialized(self);
            }
        }

        /** The native end of a dice session (slot 5), however it ended. */
        void __fastcall Finish_Hook(SDK::wh::playermodule::C_Dice *self) {
            g_finishOriginal(self);
            if (KCDC::Core::gApplication != nullptr) {
                DiceModule::Get().OnNativeFinished(self);
            }
        }

        /**
         * A badge becomes one only once its item is bound. In a match none is,
         * so its type reads as none to every badge path; false is what the
         * native answers for an item it cannot use.
         */
        bool __fastcall BindBackingItem_Hook(SDK::wh::playermodule::C_DiceBadge *self, std::uint64_t itemId) {
            if (KCDC::Core::gApplication != nullptr && DiceModule::Get().RefusesBadge()) {
                return false;
            }
            return g_bindBackingItemOriginal(self, itemId);
        }

        /** The table voices its NPC opponent's commentary; a player's body says nothing for them. */
        void __fastcall ForceMetaroleDialog_Hook(SDK::wh::entitymodule::C_Actor *actor, const SDK::CryStringT<char> &line) {
            if (KCDC::Core::gApplication != nullptr && DiceModule::Get().SilencesBark(actor)) {
                return;
            }
            g_forceMetaroleDialogOriginal(actor, line);
        }
    } // namespace

    void DiceModule::Install() {
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_UIDice__ShowDiceScore), reinterpret_cast<LPVOID>(&ShowDiceScore_Hook), reinterpret_cast<LPVOID *>(&g_showDiceScoreOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_DieSet__UpdateDieNavigationCameraTransform), reinterpret_cast<LPVOID>(&UpdateNavigationCamera_Hook), reinterpret_cast<LPVOID *>(&g_updateNavigationCameraOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_DiceGamePlayerModel__RollDice), reinterpret_cast<LPVOID>(&RollDice_Hook), reinterpret_cast<LPVOID *>(&g_rollDiceOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.playermodule__TransitionToRollingState), reinterpret_cast<LPVOID>(&TransitionToRolling_Hook), reinterpret_cast<LPVOID *>(&g_transitionToRollingOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.playermodule__TransitionToEndingState), reinterpret_cast<LPVOID>(&TransitionToEnding_Hook), reinterpret_cast<LPVOID *>(&g_transitionToEndingOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_SimpleDiceAI__TryHandleGameState), reinterpret_cast<LPVOID>(&TryHandleGameState_Hook), reinterpret_cast<LPVOID *>(&g_tryHandleGameStateOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Dice__OnHoldDieRequested), reinterpret_cast<LPVOID>(&OnHoldDieRequested_Hook), reinterpret_cast<LPVOID *>(&g_onHoldDieRequestedOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Dice__OnScoreRequested), reinterpret_cast<LPVOID>(&OnScoreRequested_Hook), reinterpret_cast<LPVOID *>(&g_onScoreRequestedOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Dice__OnPassRequested), reinterpret_cast<LPVOID>(&OnPassRequested_Hook), reinterpret_cast<LPVOID *>(&g_onPassRequestedOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Dice__InitializeDiceTableScene), reinterpret_cast<LPVOID>(&InitializeTableScene_Hook), reinterpret_cast<LPVOID *>(&g_initializeTableSceneOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_Dice__Finish), reinterpret_cast<LPVOID>(&Finish_Hook), reinterpret_cast<LPVOID *>(&g_finishOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_DiceBadge__BindBackingItemById), reinterpret_cast<LPVOID>(&BindBackingItem_Hook), reinterpret_cast<LPVOID *>(&g_bindBackingItemOriginal));
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.ForceMetaroleDialog), reinterpret_cast<LPVOID>(&ForceMetaroleDialog_Hook), reinterpret_cast<LPVOID *>(&g_forceMetaroleDialogOriginal));
    }
} // namespace KCDC::Features::Dice

static InitFunction init(
    []() {
        KCDC::Features::Dice::DiceModule::Install();
    },
    "Dice");
