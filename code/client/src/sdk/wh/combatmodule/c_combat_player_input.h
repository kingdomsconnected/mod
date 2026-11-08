#pragma once

#include "../../cryengine/action/i_game_object.h"
#include "c_combat_actor_object.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::combatmodule {
    /**
     * Local-player combat action handler.
     *
     * RTTI proves IActionListener at +0x00 and C_CombatActorObject at +0x08.
     * The corresponding tables at 0x183E5D458 and 0x183E5D478 contain exactly
     * the inherited three- and six-slot contracts with no added virtuals.
     * Its owner allocates exactly 0x180 bytes before construction.
     */
    class C_CombatPlayerInput : public SDK::IActionListener,
                                public C_CombatActorObject {
      public:
        ~C_CombatPlayerInput() override = default;

      private:
        // Constructor 0x180A4C7CC (C_CombatActor *, C_Actor *, __int64, C_CombatPlayerController *).
        void *m_pActor;                      // 0x18; the wh::entitymodule::C_Actor argument
        void *m_pController;                 // 0x20; the C_CombatPlayerController argument
        std::byte m_actionHandlers[0x10];    // 0x28; action-id -> handler map (sub_18079A6E8), filled with every combat action
        void *m_ptr0038;                     // 0x38; ctor 0
        std::int32_t m_value0040;            // 0x40; ctor 0
        std::uint32_t m_values0044[2];       // 0x44; ctor 0
        std::byte m_value004C[0x8];          // 0x4C; ctor writes a zero qword
        std::int32_t m_value0054;            // 0x54; ctor 0
        float m_scale0058;                   // 0x58; ctor 1.0f
        float m_value005C;                   // 0x5C; ctor 0
        void *m_ptr0060;                     // 0x60; ctor 0
        float m_scale0068;                   // 0x68; ctor 1.0f
        float m_vector006C[3];               // 0x6C; ctor 0
        float m_vector0078[3];               // 0x78; ctor 0
        std::byte m_padding0084[0x4];
        std::byte m_object0088[0x50];        // 0x88; built by sub_1809CDEA4 over a std::function<float()> returning 1.0f
        std::byte m_object00D8[0x8];         // 0xD8; sub_1808DDC44
        std::int32_t m_value00E0;            // 0xE0; ctor 0
        std::uint16_t m_value00E4;           // 0xE4; ctor 0
        std::byte m_padding00E6[0x2];
        std::int32_t m_value00E8;            // 0xE8; ctor 0
        std::byte m_object00EC[0x34];        // 0xEC; sub_180A4D004
        std::byte m_object0120[0x58];        // 0x120; sub_180A4D4C4
        std::int32_t m_value0178;            // 0x178; ctor 0
        std::byte m_padding017C[0x4];

        static void AssertLayout() {
            static_assert(offsetof(C_CombatPlayerInput, m_pActor) == 0x18, "C_CombatPlayerInput::m_pActor offset mismatch");
            static_assert(offsetof(C_CombatPlayerInput, m_actionHandlers) == 0x28, "C_CombatPlayerInput::m_actionHandlers offset mismatch");
            static_assert(offsetof(C_CombatPlayerInput, m_object0088) == 0x88, "C_CombatPlayerInput::m_object0088 offset mismatch");
            static_assert(offsetof(C_CombatPlayerInput, m_object0120) == 0x120, "C_CombatPlayerInput::m_object0120 offset mismatch");
            static_assert(offsetof(C_CombatPlayerInput, m_value0178) == 0x178, "C_CombatPlayerInput::m_value0178 offset mismatch");
        }
    };

    static_assert(sizeof(C_CombatPlayerInput) == 0x180,
                  "C_CombatPlayerInput size mismatch");
} // namespace SDK::wh::combatmodule
