#pragma once

#include "../../cryengine/audio/i_audio_interfaces_common_data.h"
#include "../../cryengine/system/s_system_global_environment.h"
#include "../../patterns.h"
#include "../../types.h"
#include "../entitymodule/c_actor.h"
#include "../shared/c_model_property.h"
#include "../shared/c_signal.h"
#include "c_dice_game_model.h"
#include "c_die_set.h"
#include "c_minigame.h"
#include "e_minigame_type.h"

#include <utils/hooking/hooking.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <utility>

namespace SDK::wh::playermodule {
    /**
     * The dice table's outer lifecycle, the dword at `C_Dice+0x498`, driven
     * by `UpdateGameplayState` (0x182E86D08). `IsPlaying` (slot 47) is
     * `Playing` and gates every input callback.
     */
    enum class E_DicePhase : std::uint32_t {
        Idle = 0,

        /** `StartGame` ran; the entry delay counts down. */
        Entering = 1,

        /** `InitializeDiceSession` ran: table scene, actors seated, actions installed. */
        Seated = 2,

        /** The dice and badge selection screen is up. */
        Selecting = 3,

        /** Selection closed; the game model is built on the next tick. */
        Setup = 4,

        Playing = 5,

        /** `BeginDiceExit`; the table is handed back. */
        Exiting = 6,

        Finishing = 7,
    };

    /**
     * The dice minigame: 0x5B8 bytes, kind 7 of `C_PlayerManager::CreateMinigame`,
     * constructor 0x182E7EC5C, one per actor. The local player is always its
     * actor (`C_Minigame+0x18`) and seat 0; the opponent is an actor bound
     * by `StartGame`.
     *
     * The table entity carries entity links naming its parts --
     * `seat1`, `seat2`, `dice_cup`, `badge_tagpoint_*`, the `die*` tag points
     * -- which `InitializeDiceTableScene` (0x182E8C3E8) resolves into the ids
     * below. `ResetDiceTableTransforms` (0x182E92CBC) snaps the local
     * player's entity onto the first seat tag point and the opponent's onto
     * the second.
     */
    class C_Dice: public C_Minigame {
      public:
        /** The actor's dice minigame, created when missing and `create` is set (`C_Actor::GetMinigame`). */
        static C_Dice *Of(entitymodule::C_Actor *actor, bool create) {
            return static_cast<C_Dice *>(hook::this_call<C_Minigame *>(gPatterns.C_Actor__GetMinigame, actor, E_MinigameType::Dice, create, false));
        }

        /**
         * Starts a game against `opponentEntityId` on `tableEntityId` played to
         * `targetScore` (0x182E97D0C, game type 14). The opponent must resolve
         * as an actor; anything else returns false and leaves nothing running.
         * `opponentBadge` is the badge type and tier the opponent's seat may
         * draw (13 and 3 is none); `disableFarewell` skips the opponent's
         * closing line.
         */
        bool StartGameWithTargetScore(SDK::EntityId tableEntityId, SDK::EntityId opponentEntityId, std::uint32_t targetScore, const std::uint16_t &opponentBadge, bool disableFarewell) {
            return hook::this_call<bool>(gPatterns.C_Dice__StartGameWithTargetScore, this, tableEntityId, opponentEntityId, targetScore, &opponentBadge, disableFarewell);
        }

        /** Re-snaps both actors onto their seat tag points. */
        void ResetDiceTableTransforms() {
            hook::this_call<void>(gPatterns.C_Dice__ResetDiceTableTransforms, this);
        }

        /**
         * Puts the local player on the second seat and the opponent on the
         * first: each of the three tag point pairs -- seats, badges, badge
         * close-ups -- is exchanged, which is everything the scene places by
         * side. Called after `InitializeDiceTableScene` resolved them.
         */
        void SwapSides() {
            std::swap(m_seatTagPoints[0], m_seatTagPoints[1]);
            std::swap(m_badgeTagPoints[0], m_badgeTagPoints[1]);
            std::swap(m_badgeCloseupTagPoints[0], m_badgeCloseupTagPoints[1]);
        }

        C_DieSet *GetDieSet(std::int32_t seat) const { return seat == 0 || seat == 1 ? m_dieSets[seat].get() : nullptr; }

        SDK::EntityId GetCupEntityId() const {
            return m_cupTagPoint;
        }

        SDK::EntityId GetTableEntityId() const {
            return m_tableEntityId;
        }

        SDK::EntityId GetOpponentEntityId() const {
            return m_opponentEntityId;
        }

        entitymodule::C_Actor *GetOpponent() const {
            return m_pOpponent;
        }

        E_DicePhase GetPhase() const {
            return m_phase;
        }

        /** Null until the setup phase builds it; never created from here. */
        C_DiceGameModel *GetGameModel() const {
            return m_gameModel.get();
        }

        /**
         * `InitializeDiceSession` (0x182E93D30) for an opponent the NPC
         * manager never registered, step for step but the last: seed the dice
         * generator, resolve the eight music triggers, lay the table out, put
         * both actors into dice interaction, install both seats' actions and
         * reset the die navigation. The native then has the opponent's C_NPC
         * look at the first seat -- `GetActorAnimationProxy(opponent)` and its
         * slot 36, unchecked -- and keeps the handle at +0x5B0. A body with no
         * C_NPC has nobody to turn, so the handle stays at the constructor's
         * -1, which is what `Finish` (0x182E98785) tests before releasing it.
         */
        void InitializeSessionWithoutOpponentLook() {
            SSystemGlobalEnvironment &env = SSystemGlobalEnvironment::Get();
            const float now               = env.pTimer->GetCurrTime(ETimer::Game);
            std::uint32_t seed            = 0;
            std::memcpy(&seed, &now, sizeof(seed));
            *reinterpret_cast<std::uint64_t *>(gPatterns.playermodule__s_diceLcgState) = seed;

            env.pAudioSystem->GetAudioTriggerId("music_dice_game_start", m_musicTriggers[0]);
            env.pAudioSystem->GetAudioTriggerId("music_dice_game_loss", m_musicTriggers[1]);
            env.pAudioSystem->GetAudioTriggerId("music_dice_game_win", m_musicTriggers[2]);
            env.pAudioSystem->GetAudioTriggerId("music_dice_game_abandon", m_musicTriggers[3]);
            env.pAudioSystem->GetAudioTriggerId("music_dice_game_success", m_musicTriggers[4]);
            env.pAudioSystem->GetAudioTriggerId("music_dice_game_bust", m_musicTriggers[5]);
            env.pAudioSystem->GetAudioTriggerId("music_dice_game_opponent", m_musicTriggers[6]);
            env.pAudioSystem->GetAudioTriggerId("music_dice_game_player", m_musicTriggers[7]);

            hook::this_call<void>(gPatterns.C_Dice__InitializeDiceTableScene, this);
            hook::this_call<void>(gPatterns.C_Dice__SetDiceActorsInteractionMode, this, true);
            hook::this_call<void>(gPatterns.C_Dice__InstallPlayerAction, this, &m_seatActions[0], GetActor());
            hook::this_call<void>(gPatterns.C_Dice__InstallPlayerAction, this, &m_seatActions[1], m_pOpponent);
            m_dieNavigationMode = 0;
        }

      private:
        SDK::wh::shared::C_ModelProperty<SDK::Vec2> m_vec2Property0068; // 0x68; RTTI C_ModelProperty<Vec2_tpl<float>, ..., C_SignalWithNewValueTrait> (ctor 0x182E7EC5C).
        SDK::wh::shared::C_Signal m_signal0098;     // 0x98; dispatch unk_185669410.
        std::byte m_object00A8[0x70];               // 0xA8; built by sub_182E7EB10.
        bool m_flag0118;                            // 0x118; ctor 0.
        std::byte m_padding0119[0x7];
        std::byte m_object0120[0x10];               // 0x120; built by sub_18079A6E8; the six cup audio triggers are added to it.

        // The local player's seat action, then the opponent's, each filled by
        // `InstallPlayerAction` (0x182E97D68). The slots are 0x90 apart; the
        // type inside them is not modeled.
        std::byte m_seatActions[2][0x90]; // 0x130
        std::byte m_action0250[0x90];               // 0x250; a third 0x90 action slot, same constructor sub_1809CDAD0(this, 2).
        std::byte m_object02E0[0x28];               // 0x2E0; built by sub_1803DF440.
        bool m_flag0308;                            // 0x308; ctor 0.
        std::byte m_opaque0309[0x3B];
        bool m_flag0344;                            // 0x344; ctor 0.
        std::byte m_padding0345[0x3];
        SDK::EntityId m_tableEntityId;             // 0x348
        SDK::EntityId m_opponentEntityId;          // 0x34C
        entitymodule::C_Actor *m_pOpponent;        // 0x350
        SDK::EntityId m_cupTagPoint;               // 0x358
        SDK::EntityId m_seatTagPoints[2];          // 0x35C
        SDK::EntityId m_badgeTagPoints[2];         // 0x364
        SDK::EntityId m_badgeCloseupTagPoints[2];  // 0x36C
        bool m_flag0374;                            // 0x374; ctor 0.
        std::byte m_opaque0375[0x23];
        void *m_ptrs0398[7];                        // 0x398; ctor zeroes 0x398..0x3C8.
        void *m_ptr03D0;                            // 0x3D0; ctor 0.
        std::byte m_opaque03D8[0x28];
        std::byte m_object0400[0x48];               // 0x400; built by sub_182E7EB98.
        void *m_ptr0448;                            // 0x448; ctor 0.
        void *m_ptr0450;                            // 0x450; ctor 0.
        void *m_ptr0458;                            // 0x458; ctor 0.
        bool m_flag0460;                            // 0x460; ctor 0.
        std::byte m_padding0461[0x3];
        std::uint32_t m_dieNavigationMode; // 0x464, `SetDieNavigationMode` (0x182E98EEC)
        std::byte m_opaque0468[0x20];
        void *m_ptr0488;                            // 0x488; ctor 0.
        void *m_ptr0490;                            // 0x490; ctor 0.
        E_DicePhase m_phase; // 0x498
        std::byte m_padding049C[0x4];
        std::shared_ptr<C_DiceGameModel> m_gameModel; // 0x4A0
        std::shared_ptr<C_DieSet> m_dieSets[2];     // 0x4B0, SetupGameModel
        std::byte m_pairs04D0[2][0x10];             // 0x4D0; two more of the same.
        std::byte m_opaque04F0[0x10];
        std::uint32_t m_value0500;                  // 0x500; ctor 0.
        std::byte m_opaque0504[0x14];
        void *m_ptr0518;                            // 0x518; ctor 0.
        std::uint16_t m_value0520;                  // 0x520; ctor 0.
        bool m_flag0522;                            // 0x522; ctor 0.
        std::byte m_padding0523[0x1];
        std::uint32_t m_value0524;                  // 0x524; ctor 0.
        void *m_ptr0528;                            // 0x528; ctor 0.
        void *m_ptr0530;                            // 0x530; ctor 0.
        bool m_flag0538;                            // 0x538; ctor 0.
        std::byte m_padding0539[0x3];

        // start, loss, win, abandon, success, bust, opponent, player.
        AudioControlId m_musicTriggers[8]; // 0x53C
        std::uint8_t m_value055C;                   // 0x55C; ctor 2.
        std::byte m_padding055D[0x3];
        float m_value0560;                          // 0x560; ctor 0.001f (qword write, high dword 0).
        std::uint32_t m_value0564;                  // 0x564; ctor 0.
        std::uint32_t m_value0568;                  // 0x568; ctor 0.
        bool m_flag056C;                            // 0x56C; ctor 0.
        std::byte m_padding056D[0x3];
        std::byte m_object0570[0x20];               // 0x570; built by sub_1808DD5C0 from a game-module table.
        std::uint32_t m_value0590;                  // 0x590; ctor 0x30D0000.
        std::uint32_t m_value0594;                  // 0x594; ctor 14.
        void *m_ptr0598;                            // 0x598; ctor 0.
        bool m_flag05A0;                            // 0x5A0; ctor 0.
        std::byte m_padding05A1[0x7];
        void *m_ptr05A8;                            // 0x5A8; ctor 0.

        // The opponent C_NPC's look at the first seat, or -1 for none.
        std::int32_t m_opponentLookHandle; // 0x5B0
        std::byte m_padding05B4[0x4];

        static void AssertLayout() {
            static_assert(offsetof(C_Dice, m_seatActions) == 0x130, "C_Dice::m_seatActions offset mismatch");
            static_assert(offsetof(C_Dice, m_tableEntityId) == 0x348, "C_Dice::m_tableEntityId offset mismatch");
            static_assert(offsetof(C_Dice, m_opponentEntityId) == 0x34C, "C_Dice::m_opponentEntityId offset mismatch");
            static_assert(offsetof(C_Dice, m_pOpponent) == 0x350, "C_Dice::m_pOpponent offset mismatch");
            static_assert(offsetof(C_Dice, m_cupTagPoint) == 0x358, "C_Dice::m_cupTagPoint offset mismatch");
            static_assert(offsetof(C_Dice, m_seatTagPoints) == 0x35C, "C_Dice::m_seatTagPoints offset mismatch");
            static_assert(offsetof(C_Dice, m_badgeTagPoints) == 0x364, "C_Dice::m_badgeTagPoints offset mismatch");
            static_assert(offsetof(C_Dice, m_badgeCloseupTagPoints) == 0x36C, "C_Dice::m_badgeCloseupTagPoints offset mismatch");
            static_assert(offsetof(C_Dice, m_phase) == 0x498, "C_Dice::m_phase offset mismatch");
            static_assert(offsetof(C_Dice, m_dieNavigationMode) == 0x464, "C_Dice::m_dieNavigationMode offset mismatch");
            static_assert(offsetof(C_Dice, m_gameModel) == 0x4A0, "C_Dice::m_gameModel offset mismatch");
            static_assert(offsetof(C_Dice, m_dieSets) == 0x4B0, "C_Dice::m_dieSets offset mismatch");
            static_assert(offsetof(C_Dice, m_musicTriggers) == 0x53C, "C_Dice::m_musicTriggers offset mismatch");
            static_assert(offsetof(C_Dice, m_opponentLookHandle) == 0x5B0, "C_Dice::m_opponentLookHandle offset mismatch");
            static_assert(offsetof(C_Dice, m_vec2Property0068) == 0x68, "C_Dice::m_vec2Property0068 offset mismatch");
            static_assert(offsetof(C_Dice, m_object0120) == 0x120, "C_Dice::m_object0120 offset mismatch");
            static_assert(offsetof(C_Dice, m_action0250) == 0x250, "C_Dice::m_action0250 offset mismatch");
            static_assert(offsetof(C_Dice, m_flag0344) == 0x344, "C_Dice::m_flag0344 offset mismatch");
            static_assert(offsetof(C_Dice, m_ptrs0398) == 0x398, "C_Dice::m_ptrs0398 offset mismatch");
            static_assert(offsetof(C_Dice, m_object0400) == 0x400, "C_Dice::m_object0400 offset mismatch");
            static_assert(offsetof(C_Dice, m_ptr0488) == 0x488, "C_Dice::m_ptr0488 offset mismatch");
            static_assert(offsetof(C_Dice, m_ptr0518) == 0x518, "C_Dice::m_ptr0518 offset mismatch");
            static_assert(offsetof(C_Dice, m_value055C) == 0x55C, "C_Dice::m_value055C offset mismatch");
            static_assert(offsetof(C_Dice, m_object0570) == 0x570, "C_Dice::m_object0570 offset mismatch");
            static_assert(offsetof(C_Dice, m_ptr05A8) == 0x5A8, "C_Dice::m_ptr05A8 offset mismatch");
        }
    };

    static_assert(sizeof(C_Dice) == 0x5B8, "C_Dice size mismatch");
} // namespace SDK::wh::playermodule
