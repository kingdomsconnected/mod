#pragma once

#include "c_minigame.h"

namespace SDK::wh::playermodule {
    /** Native factory allocates 0xA0; parameters are read from PickableArea. */
    class C_HerbGathering: public C_Minigame {
      public:
        std::uint32_t GetPhase() const {
            return m_phase;
        }
        const SDK::CryGUID &GetItemClass() const {
            return m_itemClass;
        }
        std::uint32_t GetAmountCoefficient() const {
            return m_amountCoefficient;
        }

      private:
        std::uint32_t m_phase;             // 0x68
        float m_pickingDuration;           // 0x6C
        SDK::CryGUID m_itemClass;          // 0x70
        std::uint32_t m_amountCoefficient; // 0x80
        float m_elapsed;                   // 0x84
        void *m_animationAction;           // 0x88; game-owned C_CallbackAction
        void *m_fadeHandle;                // 0x90
        std::uint32_t m_harvestedCount;    // 0x98
        std::byte m_padding009C[4];
        static void AssertLayout() {
            static_assert(offsetof(C_HerbGathering, m_phase) == 0x68);
            static_assert(offsetof(C_HerbGathering, m_pickingDuration) == 0x6C);
            static_assert(offsetof(C_HerbGathering, m_itemClass) == 0x70);
            static_assert(offsetof(C_HerbGathering, m_amountCoefficient) == 0x80);
            static_assert(offsetof(C_HerbGathering, m_elapsed) == 0x84);
            static_assert(offsetof(C_HerbGathering, m_animationAction) == 0x88);
            static_assert(offsetof(C_HerbGathering, m_fadeHandle) == 0x90);
            static_assert(offsetof(C_HerbGathering, m_harvestedCount) == 0x98);
        }
    };
    static_assert(sizeof(C_HerbGathering) == 0xA0);
} // namespace SDK::wh::playermodule
