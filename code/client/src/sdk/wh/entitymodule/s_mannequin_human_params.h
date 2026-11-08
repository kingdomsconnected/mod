#pragma once

namespace SDK::wh::entitymodule {
    /**
     * The human Mannequin parameters, embedded at `C_Human+0xA50`.
     *
     * Named by its RTTI vtable; the C_Human constructor installs it and the
     * object holds nothing else, so it is exactly that vtable pointer.
     */
    struct SMannequinHumanParams {
        virtual ~SMannequinHumanParams() = default;
    };

    static_assert(sizeof(SMannequinHumanParams) == 0x8, "SMannequinHumanParams size mismatch");
} // namespace SDK::wh::entitymodule
