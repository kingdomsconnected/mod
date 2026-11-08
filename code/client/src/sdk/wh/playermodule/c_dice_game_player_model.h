#pragma once

#include "../../types.h"
#include "../entitymodule/c_actor.h"
#include "../shared/c_signal.h"
#include "c_die_model.h"
#include "c_simple_dice_ai.h"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace SDK::wh::playermodule {
    // c_dice_game_model.h includes this header to hold its seats, so the back pointer can only be declared.
    class C_DiceGameModel;

    /**
     * One seat of a dice game: 0x1C8 bytes, built by `MakeDiceGamePlayerModel`
     * (0x182E7C6FC) around constructor 0x182E7F608. Seat 0 is always the
     * local player and seat 1 the opponent (`C_DiceGameModel+0x20`/`+0x30`).
     *
     * `RollDice` (0x182E99004) is the one place faces are decided. On a full
     * throw it first takes the pending override -- one face per die that is
     * not set aside, in set order -- and then runs two luck passes off the
     * dice LCG: `+0x1C0`, the chance to turn one die into a second of the
     * commonest face, and `+0x1C4`, the chance to rescue a bust. Both run
     * after the override and can rewrite a face it wrote.
     */
    class C_DiceGamePlayerModel {
      public:
        /** What a lent throw override replaced, so the native's own state goes back afterwards. */
        struct S_ThrowOverride {
            bool pending                   = false;
            std::int32_t *begin            = nullptr;
            std::int32_t *end              = nullptr;
            std::int32_t *capacity         = nullptr;
            float pairChance               = 0.0F;
            float bustRescueChance         = 0.0F;
        };

        std::int32_t GetTurnScore() const {
            return m_turnScore;
        }

        std::int32_t GetTotalScore() const {
            return m_totalScore;
        }

        /** 0 for the local player's seat, 1 for the opponent's. */
        std::int32_t GetSeat() const {
            return m_seat;
        }

        /** The seat's dice AI, null on the local player's seat. */
        C_SimpleDiceAI *GetAI() const {
            return m_pAI;
        }

        /** The actor `C_Dice::SetupGameModel` bound to the seat (`BindActor`, 0x182E94908). */
        entitymodule::C_Actor *GetActor() const {
            return m_pActor;
        }

        std::size_t GetDieCount() const {
            return m_dice.Size();
        }

        /** The caller has already bounded `index` against `GetDieCount()`. */
        C_DieModel *GetDie(std::size_t index) const {
            return m_dice.begin()[index].get();
        }

        /**
         * Points the throw override at `faces` and switches both luck passes
         * off, for the duration of one `RollDice`, which only reads the
         * buffer. The game's own vector is never assigned or freed, so no
         * allocation crosses the heaps; `RestoreThrowOverride` puts it back.
         */
        S_ThrowOverride LendThrowOverride(std::int32_t *faces, std::size_t count) {
            const S_ThrowOverride saved {m_overrideNextThrow, m_overrideBegin, m_overrideEnd, m_overrideCapacity, m_pairChance, m_bustRescueChance};
            m_overrideNextThrow = true;
            m_overrideBegin     = faces;
            m_overrideEnd       = faces + count;
            m_overrideCapacity  = faces + count;
            m_pairChance        = 0.0F;
            m_bustRescueChance  = 0.0F;
            return saved;
        }

        void RestoreThrowOverride(const S_ThrowOverride &saved) {
            m_overrideNextThrow = saved.pending;
            m_overrideBegin     = saved.begin;
            m_overrideEnd       = saved.end;
            m_overrideCapacity  = saved.capacity;
            m_pairChance        = saved.pairChance;
            m_bustRescueChance  = saved.bustRescueChance;
        }

      private:
        std::byte m_vftable0000[0x8];      // 0x00; C_DiceGamePlayerModel vftable (ctor 0x182E7F608).
        void *m_weakThisPtr;               // 0x08; enable_shared_from_this weak pointer, ctor 0.
        void *m_weakThisRep;               // 0x10; its control block, ctor 0.
        std::int32_t m_turnScore; // 0x18, `SetTurnScore`
        std::byte m_padding001C[0x4];
        SDK::wh::shared::C_Signal m_turnScoreChanged;  // 0x20; dispatch unk_1855ED788.
        std::int32_t m_totalScore; // 0x30, `SetTotalScore`
        std::byte m_padding0034[0x4];
        SDK::wh::shared::C_Signal m_totalScoreChanged; // 0x38; dispatch unk_1855ED788.
        C_SimpleDiceAI *m_pAI; // 0x48, the seat's I_DiceAI
        C_DiceGameModel *m_pGameModel;     // 0x50; the constructor's first argument.
        std::int32_t m_seat; // 0x58
        std::byte m_padding005C[0x4];
        SDK::StackVector<std::shared_ptr<C_DieModel>, 6> m_dice;      // 0x60; ctor reserves 6 and fills via MakeDieModel 0x182E7C808.
        SDK::StackVector<std::shared_ptr<C_DieModel>, 6> m_dice00F0;  // 0xF0; same reserve helper sub_182E798FC, left empty by the ctor.
        void *m_ptr0180;                   // 0x180; ctor 0.
        void *m_ptr0188;                   // 0x188; ctor 0.
        void *m_ptr0190;                   // 0x190; ctor 0.
        bool m_flag0198;                   // 0x198; ctor 0.
        bool m_overrideNextThrow; // 0x199, `C_ScriptBind_Dice::OverrideNextThrow`
        std::byte m_padding019A[0x6];
        std::int32_t *m_overrideBegin;    // 0x1A0
        std::int32_t *m_overrideEnd;      // 0x1A8
        std::int32_t *m_overrideCapacity; // 0x1B0
        entitymodule::C_Actor *m_pActor;  // 0x1B8
        float m_pairChance;               // 0x1C0
        float m_bustRescueChance;         // 0x1C4

        static void AssertLayout() {
            static_assert(offsetof(C_DiceGamePlayerModel, m_turnScore) == 0x18, "C_DiceGamePlayerModel::m_turnScore offset mismatch");
            static_assert(offsetof(C_DiceGamePlayerModel, m_totalScore) == 0x30, "C_DiceGamePlayerModel::m_totalScore offset mismatch");
            static_assert(offsetof(C_DiceGamePlayerModel, m_pAI) == 0x48, "C_DiceGamePlayerModel::m_pAI offset mismatch");
            static_assert(offsetof(C_DiceGamePlayerModel, m_seat) == 0x58, "C_DiceGamePlayerModel::m_seat offset mismatch");
            static_assert(offsetof(C_DiceGamePlayerModel, m_turnScoreChanged) == 0x20, "C_DiceGamePlayerModel::m_turnScoreChanged offset mismatch");
            static_assert(offsetof(C_DiceGamePlayerModel, m_totalScoreChanged) == 0x38, "C_DiceGamePlayerModel::m_totalScoreChanged offset mismatch");
            static_assert(offsetof(C_DiceGamePlayerModel, m_pGameModel) == 0x50, "C_DiceGamePlayerModel::m_pGameModel offset mismatch");
            static_assert(offsetof(C_DiceGamePlayerModel, m_dice) == 0x60, "C_DiceGamePlayerModel::m_dice offset mismatch");
            static_assert(offsetof(C_DiceGamePlayerModel, m_dice) + SDK::StackVector<std::shared_ptr<C_DieModel>, 6>::FirstOffset() == 0xD8, "C_DiceGamePlayerModel dice first offset mismatch");
            static_assert(offsetof(C_DiceGamePlayerModel, m_dice00F0) == 0xF0, "C_DiceGamePlayerModel::m_dice00F0 offset mismatch");
            static_assert(offsetof(C_DiceGamePlayerModel, m_flag0198) == 0x198, "C_DiceGamePlayerModel::m_flag0198 offset mismatch");
            static_assert(offsetof(C_DiceGamePlayerModel, m_overrideNextThrow) == 0x199, "C_DiceGamePlayerModel::m_overrideNextThrow offset mismatch");
            static_assert(offsetof(C_DiceGamePlayerModel, m_overrideBegin) == 0x1A0, "C_DiceGamePlayerModel::m_overrideBegin offset mismatch");
            static_assert(offsetof(C_DiceGamePlayerModel, m_overrideEnd) == 0x1A8, "C_DiceGamePlayerModel::m_overrideEnd offset mismatch");
            static_assert(offsetof(C_DiceGamePlayerModel, m_overrideCapacity) == 0x1B0, "C_DiceGamePlayerModel::m_overrideCapacity offset mismatch");
            static_assert(offsetof(C_DiceGamePlayerModel, m_pActor) == 0x1B8, "C_DiceGamePlayerModel::m_pActor offset mismatch");
            static_assert(offsetof(C_DiceGamePlayerModel, m_pairChance) == 0x1C0, "C_DiceGamePlayerModel::m_pairChance offset mismatch");
            static_assert(offsetof(C_DiceGamePlayerModel, m_bustRescueChance) == 0x1C4, "C_DiceGamePlayerModel::m_bustRescueChance offset mismatch");
        }
    };

    static_assert(sizeof(C_DiceGamePlayerModel) == 0x1C8, "C_DiceGamePlayerModel size mismatch");
} // namespace SDK::wh::playermodule
