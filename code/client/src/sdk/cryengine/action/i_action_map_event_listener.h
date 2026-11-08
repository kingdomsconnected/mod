#pragma once

namespace SDK {
    /**
     * Observer interface for action-map events.
     *
     * RTTI identifies this global-namespace type as a struct. Its table at
     * 0x183EC9B68 contains the virtual destructor followed by exactly one
     * pure callback. Its stripped source contract remains opaque.
     */
    struct IActionMapEventListener {
        virtual ~IActionMapEventListener() = default;
        virtual void unk_fn0001() = 0;
    };

    static_assert(sizeof(IActionMapEventListener) == 0x8,
                  "IActionMapEventListener size mismatch");
} // namespace SDK
