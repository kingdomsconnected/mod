#pragma once

#include <cstddef>
#include <cstdint>

namespace SDK::wh::game {
    /**
     * Activation lifecycle contract implemented by switchable cameras.
     *
     * C_CameraBoneAnimated's secondary vtable proves that this is an
     * independent six-slot interface. Keep the offset-labelled names distinct
     * from C_Camera's slots so unrelated overrides are not merged in C++.
     */
    class I_CameraActivation {
      public:
        virtual void activation_unk_fn0000() = 0;
        virtual void activation_unk_fn0001() = 0;
        virtual void activation_unk_fn0002() = 0;
        virtual void activation_unk_fn0003() = 0;
        virtual bool activation_unk_fn0004() const = 0;
        virtual void activation_unk_fn0005(std::int32_t value) = 0;
    };

    static_assert(sizeof(I_CameraActivation) == 0x8,
                  "I_CameraActivation size mismatch");
} // namespace SDK::wh::game
