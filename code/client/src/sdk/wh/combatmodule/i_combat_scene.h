#pragma once

namespace SDK::wh::combatmodule {
    class I_CombatScene {
      public:
        virtual ~I_CombatScene()  = default;
        virtual void unk_fn0001() = 0;
        virtual void unk_fn0002() = 0;
    };
    static_assert(sizeof(I_CombatScene) == 0x8, "I_CombatScene size mismatch");
} // namespace SDK::wh::combatmodule
