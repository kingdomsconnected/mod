#pragma once

namespace SDK {
    /**
     * Abstract CryAnimation skinned-attachment interface.
     *
     * RTTI identifies this global-namespace type as a struct. Its table at
     * 0x183A8F930 contains twenty pure operations with the virtual destructor
     * interleaved at slot 10. Their stripped source contracts remain opaque.
     */
    struct IAttachmentSkin {
        virtual void unk_fn0000() = 0;
        virtual void unk_fn0001() = 0;
        virtual void unk_fn0002() = 0;
        virtual void unk_fn0003() = 0;
        virtual void unk_fn0004() = 0;
        virtual void unk_fn0005() = 0;
        virtual void unk_fn0006() = 0;
        virtual void unk_fn0007() = 0;
        virtual void unk_fn0008() = 0;
        virtual void unk_fn0009() = 0;
        virtual ~IAttachmentSkin() = default; // slot 10, +0x050
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
    };

    static_assert(sizeof(IAttachmentSkin) == 0x8,
                  "IAttachmentSkin size mismatch");
} // namespace SDK
