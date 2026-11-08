#pragma once

namespace SDK {
    /**
     * Animation-graph state listener implemented by CAnimatedCharacter.
     *
     * CAnimatedCharacter's secondary table at 0x183A49808 proves an exact
     * four-slot contract at object offset +0x40: a virtual destructor and
     * three callbacks. Diagnostic evidence ties one callback to animation
     * graph query completion, but its stripped signature remains opaque.
     */
    struct IAnimationGraphStateListener {
        virtual ~IAnimationGraphStateListener() = default;
        virtual void unk_fn0001() = 0;
        virtual void unk_fn0002() = 0;
        virtual void unk_fn0003() = 0;
    };

    static_assert(sizeof(IAnimationGraphStateListener) == 0x8,
                  "IAnimationGraphStateListener size mismatch");
} // namespace SDK
