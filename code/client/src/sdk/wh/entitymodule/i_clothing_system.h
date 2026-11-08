#pragma once

namespace SDK::wh::entitymodule {
    /**
     * Primary clothing-system interface.
     *
     * The abstract table at 0x183E8B988 contains exactly a virtual destructor
     * and two operations whose source signatures are not present in the
     * binary.
     */
    class I_ClothingSystem {
      public:
        virtual ~I_ClothingSystem() = default;
        virtual void unk_fn0001() = 0;
        virtual void unk_fn0002() = 0;
    };

    static_assert(sizeof(I_ClothingSystem) == 0x8, "I_ClothingSystem size mismatch");
} // namespace SDK::wh::entitymodule
