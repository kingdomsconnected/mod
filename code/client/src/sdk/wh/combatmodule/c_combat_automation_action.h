#pragma once

#include "c_combat_actor_updated_object.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::combatmodule {
    /** Base for update-aware combat automation actions. */
    class C_CombatAutomationAction : public C_CombatActorUpdatedObject {
      public:
        ~C_CombatAutomationAction() override = default;

        virtual void unk_fn0010() = 0;
        virtual void unk_fn0011() {} // emitted operation
        virtual void unk_fn0012() {} // emitted operation
        virtual void unk_fn0013() {} // emitted operation

      private:
        std::int32_t m_state0018;     // 0x18; ctor 0x180C5785C writes 0
        std::byte m_padding001C[0x4];
        std::int64_t m_time0020;      // 0x20; ctor -100000, the engine's "never" timestamp
        void *m_ptr0028;              // 0x28; ctor 0

        static void AssertLayout() {
            static_assert(offsetof(C_CombatAutomationAction, m_state0018) == 0x18, "C_CombatAutomationAction::m_state0018 offset mismatch");
            static_assert(offsetof(C_CombatAutomationAction, m_time0020) == 0x20, "C_CombatAutomationAction::m_time0020 offset mismatch");
        }
    };

    static_assert(sizeof(C_CombatAutomationAction) == 0x30,
                  "C_CombatAutomationAction size mismatch");
} // namespace SDK::wh::combatmodule
