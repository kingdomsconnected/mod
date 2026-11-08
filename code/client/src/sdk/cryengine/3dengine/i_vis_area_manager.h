#pragma once

namespace SDK {
    /**
     * Abstract indoor/outdoor visibility-area manager, vtable 0x183A64F30.
     *
     * KCD2 emits exactly 11 entries. Only the virtual destructor has a
     * source-identifiable signature, so all visibility-area contracts remain
     * opaque.
     */
    class IVisAreaManager {
      public:
        virtual ~IVisAreaManager() = default; // +0x00
        virtual void unk_fn0001() = 0;
        virtual void unk_fn0002() = 0;
        virtual void unk_fn0003() = 0;
        virtual void unk_fn0004() = 0;
        virtual void unk_fn0005() = 0;
        virtual void unk_fn0006() = 0;
        virtual void unk_fn0007() = 0;
        virtual void unk_fn0008() = 0;
        virtual void unk_fn0009() = 0;
        virtual void unk_fn0010() = 0; // +0x50
    };

    static_assert(sizeof(IVisAreaManager) == 0x8,
                  "IVisAreaManager size mismatch");
} // namespace SDK
