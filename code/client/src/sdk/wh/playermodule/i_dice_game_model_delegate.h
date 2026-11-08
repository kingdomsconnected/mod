#pragma once

#include <cstddef>

namespace SDK::wh::playermodule {
    /**
     * The game model as its states see it: the secondary base of
     * `C_DiceGameModel` at +0x18 (vtable 0x18473F368). Every state method
     * takes it as its first argument after `this`, and every delegate method
     * the native calls through it is reached with that adjustor.
     *
     * Only its address is ever handed around here, so nothing of its vtable
     * is restated.
     */
    class I_DiceGameModelDelegate {
      private:
        // Embedded by value at C_DiceGameModel +0x18, so its virtuals stay
        // undeclared: this is the table pointer the model's ctor installs.
        const void *m_pVTable; // 0x00
    };

    static_assert(sizeof(I_DiceGameModelDelegate) == 0x8, "I_DiceGameModelDelegate size mismatch");
} // namespace SDK::wh::playermodule
