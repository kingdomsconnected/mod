#pragma once

namespace SDK::wh::game {
    /** RTTI and the two-entry C_CombatLodManager table prove this contract. */
    class I_GameSideEffectCallback {
      public:
        virtual void unk_fn0000() = 0;
        virtual void unk_fn0001() = 0;
    };
    static_assert(sizeof(I_GameSideEffectCallback) == 0x8, "I_GameSideEffectCallback size mismatch");
} // namespace SDK::wh::game
