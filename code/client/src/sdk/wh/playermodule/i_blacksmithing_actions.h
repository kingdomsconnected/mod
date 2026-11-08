#pragma once

namespace SDK::wh::playermodule {
    /** Thirteen-slot interface at C_Blacksmithing+0x68, table 0x183A6A320. */
    class I_BlacksmithingActions {
      public:
        virtual void unk_smith_actions0000() = 0;
        virtual void unk_smith_actions0001() = 0;
        virtual void unk_smith_actions0002() = 0;
        virtual void unk_smith_actions0003() = 0;
        virtual void unk_smith_actions0004() = 0;
        virtual void unk_smith_actions0005() = 0;
        virtual void unk_smith_actions0006() = 0;
        virtual void unk_smith_actions0007() = 0;
        virtual void unk_smith_actions0008() = 0;
        virtual void unk_smith_actions0009() = 0;
        virtual void unk_smith_actions0010() = 0;
        virtual void unk_smith_actions0011() = 0;
        virtual void unk_smith_actions0012() = 0;
    };

    static_assert(sizeof(I_BlacksmithingActions) == 0x8);
} // namespace SDK::wh::playermodule
