#pragma once

#include "../entitymodule/i_equipment_manager_listener.h"
#include "../game/i_entity_side_effect_callback.h"
#include "../shared/c_signal.h"
#include "c_combat_automation_action.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::combatmodule {
    /** Combat automation responsible for equipment and weapon side effects. */
    class C_CombatAutomationWeapons
        : public C_CombatAutomationAction,
          public entitymodule::I_EquipmentManagerListener,
          public game::I_EntitySideEffectCallback {
      public:
        ~C_CombatAutomationWeapons() override = default;

        void unk_fn0010() override {}

      private:
        // Constructor 0x1810EF1B4.
        shared::C_Signal m_signal0040; // 0x40; dispatch unk_1855D4D60
        shared::C_Signal m_signal0050; // 0x50; dispatch unk_185665AC0
        std::int64_t m_time0060;       // 0x60; ctor -100000
        std::int64_t m_time0068;       // 0x68; ctor -100000
        float m_value0070;             // 0x70; ctor 5.0f
        std::uint8_t m_value0074;      // 0x74; ctor 4
        std::byte m_padding0075[0x3];
        std::int64_t m_time0078;       // 0x78; ctor -100000
        std::int32_t m_value0080;      // 0x80; ctor 0
        std::byte m_padding0084[0x4];
        void *m_ptr0088;               // 0x88; ctor 0

        static void AssertLayout() {
            static_assert(offsetof(C_CombatAutomationWeapons, m_signal0040) == 0x40, "C_CombatAutomationWeapons::m_signal0040 offset mismatch");
            static_assert(offsetof(C_CombatAutomationWeapons, m_value0070) == 0x70, "C_CombatAutomationWeapons::m_value0070 offset mismatch");
            static_assert(offsetof(C_CombatAutomationWeapons, m_ptr0088) == 0x88, "C_CombatAutomationWeapons::m_ptr0088 offset mismatch");
        }
    };

    static_assert(sizeof(C_CombatAutomationWeapons) == 0x90,
                  "C_CombatAutomationWeapons size mismatch");
} // namespace SDK::wh::combatmodule
