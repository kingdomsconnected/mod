#pragma once

namespace SDK::wh::entitymodule {
    /**
     * Observer interface for actor/vehicle movement input.
     *
     * RTTI identifies this namespace-qualified type as a class. Its table at
     * 0x183A79B38 contains one pure operation, one emitted default, and the
     * virtual destructor at slot 2. Their stripped contracts remain opaque.
     */
    class I_MovementInputListener {
      public:
        virtual void unk_fn0000() = 0;
        virtual void unk_fn0001() {} // emitted default
        virtual ~I_MovementInputListener() = default; // slot 2, +0x010
    };

    static_assert(sizeof(I_MovementInputListener) == 0x8,
                  "I_MovementInputListener size mismatch");
} // namespace SDK::wh::entitymodule
