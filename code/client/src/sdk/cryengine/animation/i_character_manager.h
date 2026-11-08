#pragma once

namespace SDK {
    /**
     * Abstract CryAnimation character-manager interface.
     *
     * RTTI identifies this global-namespace type as a struct. Its table at
     * 0x184061510 contains exactly 64 entries: the virtual destructor, 60
     * pure operations, and three emitted default operations. Their stripped
     * source contracts remain opaque.
     */
    struct ICharacterManager {
        virtual ~ICharacterManager() = default; // +0x00
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
        virtual void unk_fn0016() = 0;
        virtual void unk_fn0017() = 0;
        virtual void unk_fn0018() = 0;
        virtual void unk_fn0019() = 0;
        virtual void unk_fn0020() = 0;
        virtual void unk_fn0021() = 0;
        virtual void unk_fn0022() = 0;
        virtual void unk_fn0023() = 0;
        virtual void unk_fn0024() = 0;
        virtual void unk_fn0025() = 0;
        virtual void unk_fn0026() = 0;
        virtual void unk_fn0027() = 0;
        virtual void unk_fn0028() = 0;
        virtual void unk_fn0029() = 0;
        virtual void unk_fn0030() = 0;
        virtual void unk_fn0031() = 0;
        virtual void unk_fn0032() = 0;
        virtual void unk_fn0033() = 0;
        /**
         * Slot 34 (0x1804BE0B0). Joins every animation job queued this frame
         * by `CCharInstance::StartAnimationProcessing` (0x18083E2F8), updates
         * attachment bindings, then clears the queue. Between its return and
         * the next frame's `PrepareAndStartAnimProc` no job reads any
         * character, so that is where a live actor's character may be
         * replaced.
         */
        virtual void SyncAllAnimations() = 0; // +0x110
        virtual void unk_fn0035() = 0;
        virtual void unk_fn0036() = 0;
        virtual void unk_fn0037() = 0;
        virtual void unk_fn0038() = 0;
        virtual void unk_fn0039() = 0;
        virtual void unk_fn0040() = 0;
        virtual void unk_fn0041() = 0;
        virtual void unk_fn0042() = 0;
        virtual void unk_fn0043() = 0;
        virtual void unk_fn0044() = 0;
        virtual void unk_fn0045() = 0;
        virtual void unk_fn0046() = 0;
        virtual void unk_fn0047() = 0;
        virtual void unk_fn0048() = 0;
        virtual void unk_fn0049() = 0;
        virtual void unk_fn0050() = 0;
        virtual void unk_fn0051() = 0;
        virtual void unk_fn0052() = 0;
        virtual void unk_fn0053() = 0;
        virtual void unk_fn0054() = 0;
        virtual void unk_fn0055() = 0;
        virtual void unk_fn0056() = 0;
        virtual void unk_fn0057() = 0;
        virtual void unk_fn0058() = 0;
        virtual void unk_fn0059() = 0;
        virtual void unk_fn0060() = 0;
        virtual void unk_fn0061() = 0;
        virtual void unk_fn0062() = 0;
        virtual void unk_fn0063() = 0; // +0x1F8
    };

    static_assert(sizeof(ICharacterManager) == 0x8,
                  "ICharacterManager size mismatch");
} // namespace SDK
