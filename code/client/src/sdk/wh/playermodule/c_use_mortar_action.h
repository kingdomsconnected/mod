#pragma once

#include "../../cryengine/action/i_game_object.h"
#include "c_alchemy_action_base.h"

namespace SDK::wh::playermodule {
    /** Factory 0x181390E94 allocates 0x98; RTTI 0x183F60770 pins both bases. */
    class C_UseMortarAction
        : public SDK::wh::playermodule::C_AlchemyActionBase<SDK::wh::playermodule::S_EmptyParams, SDK::wh::framework::C_Action<SDK::wh::playermodule::I_AlchemyAction>>
        , public SDK::IActionListener {
      public:
        enum class Phase : std::uint32_t {
            None,
            Picking,
            Idle,
            Milling,
            Returning
        };
        Phase GetPhase() const {
            return m_phase;
        }
        std::uint32_t GetCompletedUses() const {
            return m_completedUses;
        }
        void SetRequiredUses(std::uint32_t value) {
            m_requiredUses = value;
        }

      private:
        Phase m_phase; // 0x70; ctor 0x181390F40 and callback 0x1806C3010.
        std::uint32_t m_eventHashes[3];  // 0x74; lower-cased CRC32s of "Attach", "Detach" and "Use"
        void *m_pActionHandlers;         // 0x80; owned 0x10-byte std::map {head, size} of action callbacks
        std::uint32_t m_completedUses; // 0x88; zero until first milling exit.
        std::uint32_t m_requiredUses;  // 0x8C; Start reads MortarUseCount here.
        void *m_ptr0090;                 // 0x90; ctor 0
        static void AssertLayout() {
            static_assert(offsetof(C_UseMortarAction, m_phase) == 0x70);
            static_assert(offsetof(C_UseMortarAction, m_completedUses) == 0x88);
            static_assert(offsetof(C_UseMortarAction, m_requiredUses) == 0x8C);
            static_assert(offsetof(C_UseMortarAction, m_pActionHandlers) == 0x80);
            static_assert(offsetof(C_UseMortarAction, m_ptr0090) == 0x90);
        }
    };
    static_assert(sizeof(C_UseMortarAction) == 0x98);
} // namespace SDK::wh::playermodule
