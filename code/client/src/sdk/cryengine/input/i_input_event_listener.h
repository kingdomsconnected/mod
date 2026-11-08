#pragma once

namespace SDK {
    /**
     * Primary base of C_Keybinds, confirmed by its RTTI hierarchy. Its four
     * vtable entries have no destructor and are not called by the mod; only
     * its size is needed to place the IPlayerProfileListener base at +0x8.
     */
    struct IInputEventListener {
        virtual void Opaque01() = 0;
        virtual void Opaque02() = 0;
        virtual void Opaque03() = 0;
        virtual void Opaque04() = 0;
    };

    static_assert(sizeof(IInputEventListener) == 0x8, "IInputEventListener size mismatch");
} // namespace SDK
