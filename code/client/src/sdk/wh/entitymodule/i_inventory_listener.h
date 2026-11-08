#pragma once

namespace SDK::wh::entitymodule {
    /**
     * Inventory-change listener used by humanoid hand/equipment integration.
     *
     * Its standalone table at 0x183A65058 and C_HumanHandHelper's secondary
     * table at 0x183A3B270 contain seven callbacks followed by the virtual
     * destructor. The unusual destructor-last order is preserved exactly.
     */
    class I_InventoryListener {
      public:
        virtual void unk_fn0000() = 0;
        virtual void unk_fn0001() = 0;
        virtual void unk_fn0002() = 0;
        virtual void unk_fn0003() = 0;
        virtual void unk_fn0004() = 0;
        virtual void unk_fn0005() = 0;
        virtual void unk_fn0006() = 0;
        virtual ~I_InventoryListener() = default; // +0x38
    };

    static_assert(sizeof(I_InventoryListener) == 0x8,
                  "I_InventoryListener size mismatch");
} // namespace SDK::wh::entitymodule
