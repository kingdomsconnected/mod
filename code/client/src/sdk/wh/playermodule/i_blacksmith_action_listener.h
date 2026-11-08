#pragma once

namespace SDK::wh::playermodule {
    /** Three-slot interface at C_Blacksmithing+0x70, table 0x183A6A2C8. */
    class I_BlacksmithActionListener {
      public:
        virtual void unk_smith_listener0000() = 0;
        virtual void unk_smith_listener0001() = 0;
        virtual void unk_smith_listener0002() = 0;
    };

    static_assert(sizeof(I_BlacksmithActionListener) == 0x8);
} // namespace SDK::wh::playermodule
