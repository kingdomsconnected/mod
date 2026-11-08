#pragma once

namespace SDK {
    /**
     * IBreakableManager, vtable 0x184071DB0. KCD2 emits exactly 26 entries;
     * only the virtual destructor has a source-identifiable signature, so
     * breakage, repair, and serialization contracts remain opaque.
     */
    class IBreakableManager {
      public:
        virtual ~IBreakableManager() = default; // +0x00
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
        virtual void unk_fn0025() = 0; // +0xC8
    };

    static_assert(sizeof(IBreakableManager) == 0x8, "IBreakableManager size mismatch");
} // namespace SDK
