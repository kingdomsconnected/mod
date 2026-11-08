#pragma once

namespace SDK::wh::entitymodule {
    /**
     * Listener base implemented by C_EquipmentManager.
     *
     * The abstract table at 0x183A65020 contains five callbacks followed by
     * the virtual destructor. C_OutfitManager's secondary deleting-destructor
     * thunk independently confirms the destructor at slot 5.
     */
    class I_ItemCollectionListener {
      public:
        virtual void unk_fn0000() = 0;
        virtual void unk_fn0001() = 0;
        virtual void unk_fn0002() = 0;
        virtual void unk_fn0003() = 0;
        virtual void unk_fn0004() = 0;
        virtual ~I_ItemCollectionListener() = default; // slot 5, +0x28
    };

    static_assert(sizeof(I_ItemCollectionListener) == 0x8, "I_ItemCollectionListener size mismatch");
} // namespace SDK::wh::entitymodule
