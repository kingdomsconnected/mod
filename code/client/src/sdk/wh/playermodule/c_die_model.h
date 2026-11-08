#pragma once

#include "../shared/c_signal.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::playermodule {
    /**
     * Where one die stands in a turn, the byte at `C_DieModel+0x40`
     * (`C_DieModel::SetState`, 0x182E954FC).
     */
    enum class E_DieState : std::uint8_t {
        /** On the table, thrown this time and not picked. `RollDice` rolls these. */
        Free = 0,

        /** Picked by the player; `ScoreSelectedDice` scores exactly these. */
        Held = 1,

        /** Scored earlier in the turn and set aside; never thrown again until reset. */
        Scored = 2,
    };

    /**
     * One die of a player's set: 0xB8 bytes, built by `MakeDieModel`
     * (0x182E7C808, a `make_shared` of 0xC8 with the 0x10 control block in
     * front) around constructor 0x182E7F76C.
     *
     * The face is an index, 0..5 for one to six pips; the scoring rules
     * (`C_DiceGameModel::BuildScoringRules`) are written in the same indices.
     */
    class C_DieModel {
      public:
        /** The die's index in its player's set, 0..5; the id `RequestSetDieHeld` takes. */
        std::uint32_t GetIndex() const {
            return m_index;
        }

        E_DieState GetState() const {
            return m_state;
        }

        /** 0..5, one to six pips. */
        std::uint32_t GetFaceValue() const {
            return m_faceValue;
        }

      private:
        std::byte m_vftable0000[0x8];                // 0x00; C_DieModel vftable (ctor 0x182E7F76C).
        void *m_weakThisPtr;                         // 0x08; enable_shared_from_this weak pointer, ctor 0.
        void *m_weakThisRep;                         // 0x10; its control block, ctor 0.
        SDK::wh::shared::C_Signal m_signal0018;      // 0x18; dispatch unk_1855ED7C8.
        std::uint32_t m_index; // 0x28
        std::byte m_padding002C[0x4];
        SDK::wh::shared::C_Signal m_signal0030;      // 0x30; dispatch unk_1855ED7B8.
        E_DieState m_state; // 0x40
        std::byte m_padding0041[0x7];
        SDK::wh::shared::C_Signal m_signal0048;      // 0x48; dispatch unk_1855ED7C8.
        std::uint32_t m_faceValue; // 0x58
        std::byte m_padding005C[0x4];
        SDK::wh::shared::C_Signal m_signal0060;      // 0x60; dispatch unk_1855ED7A8.
        SDK::wh::shared::C_Signal m_signal0070;      // 0x70; dispatch unk_1855ED7A8.
        std::byte m_opaque0080[0x8];
        std::byte m_object0088[0x30];                // 0x88; built by sub_182E93AF4.

        static void AssertLayout() {
            static_assert(offsetof(C_DieModel, m_index) == 0x28, "C_DieModel::m_index offset mismatch");
            static_assert(offsetof(C_DieModel, m_state) == 0x40, "C_DieModel::m_state offset mismatch");
            static_assert(offsetof(C_DieModel, m_faceValue) == 0x58, "C_DieModel::m_faceValue offset mismatch");
            static_assert(offsetof(C_DieModel, m_signal0018) == 0x18, "C_DieModel::m_signal0018 offset mismatch");
            static_assert(offsetof(C_DieModel, m_signal0070) == 0x70, "C_DieModel::m_signal0070 offset mismatch");
            static_assert(offsetof(C_DieModel, m_object0088) == 0x88, "C_DieModel::m_object0088 offset mismatch");
        }
    };

    static_assert(sizeof(C_DieModel) == 0xB8, "C_DieModel size mismatch");
} // namespace SDK::wh::playermodule
