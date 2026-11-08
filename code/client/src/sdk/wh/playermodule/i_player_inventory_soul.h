#pragma once

namespace SDK::wh::playermodule {
    /** Native player-inventory lifetime and persistence contract. */
    class I_PlayerInventorySoul {
      public:
        virtual ~I_PlayerInventorySoul() = default;
        virtual void unk_fn0001() = 0;
        virtual void unk_fn0002() = 0;
        virtual void unk_fn0003() = 0;
        virtual void unk_fn0004() = 0;
        virtual void unk_fn0005() = 0;
        virtual void unk_fn0006() = 0;
        virtual void unk_fn0007() = 0;
        virtual void unk_fn0008() = 0;
        virtual void unk_fn0009() = 0;
        virtual void unk_fn0010() = 0;
        virtual void unk_fn0011() = 0;
        virtual void unk_fn0012() = 0;
        virtual void unk_fn0013() = 0;
        virtual void unk_fn0014() = 0;
        virtual void unk_fn0015() = 0;
    };

    static_assert(sizeof(I_PlayerInventorySoul) == 0x8,
                  "I_PlayerInventorySoul size mismatch");
} // namespace SDK::wh::playermodule
