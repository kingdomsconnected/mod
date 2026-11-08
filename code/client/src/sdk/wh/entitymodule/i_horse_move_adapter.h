#pragma once

namespace SDK::wh::entitymodule {
    /**
     * Abstract adapter for rider-driven horse movement.
     *
     * RTTI identifies this namespace-qualified type as the primary base of
     * C_RiderPlayerInput. That implementation's table at 0x183EAADB8 contains
     * six operations followed by the deleting-destructor override at slot 6,
     * with no concrete additions. The next base begins at +0x08.
     */
    class I_HorseMoveAdapter {
      public:
        virtual void unk_fn0000() = 0;
        virtual void unk_fn0001() = 0;
        virtual void unk_fn0002() = 0;
        virtual void unk_fn0003() = 0;
        virtual void unk_fn0004() = 0;
        virtual void unk_fn0005() = 0;
        virtual ~I_HorseMoveAdapter() = default; // slot 6, +0x030
    };

    static_assert(sizeof(I_HorseMoveAdapter) == 0x8,
                  "I_HorseMoveAdapter size mismatch");
} // namespace SDK::wh::entitymodule
