#pragma once

namespace SDK::wh::combatmodule {
    /**
     * Abstract combat-input controller for the local player.
     *
     * RTTI identifies this namespace-qualified type as a class. Its table at
     * 0x183A79BC8 contains the virtual destructor followed by exactly seven
     * pure operations. Their stripped source contracts remain opaque.
     */
    class I_CombatPlayerController {
      public:
        virtual ~I_CombatPlayerController() = default;
        virtual void unk_fn0001() = 0;
        virtual void unk_fn0002() = 0;
        virtual void unk_fn0003() = 0;
        virtual void unk_fn0004() = 0;
        virtual void unk_fn0005() = 0;
        virtual void unk_fn0006() = 0;
        virtual void unk_fn0007() = 0;
    };

    static_assert(sizeof(I_CombatPlayerController) == 0x8,
                  "I_CombatPlayerController size mismatch");
} // namespace SDK::wh::combatmodule
