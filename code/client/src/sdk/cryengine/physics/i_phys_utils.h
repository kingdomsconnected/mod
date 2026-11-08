#pragma once

namespace SDK {
    /**
     * Abstract CryPhysics utility interface, vtable 0x18407FC48.
     *
     * KCD2 emits exactly six entries. Only the virtual destructor has a
     * source-identifiable signature, so all five utility contracts remain
     * opaque.
     */
    class IPhysUtils {
      public:
        virtual ~IPhysUtils() = default; // +0x00
        virtual void unk_fn0001() = 0;
        virtual void unk_fn0002() = 0;
        virtual void unk_fn0003() = 0;
        virtual void unk_fn0004() = 0;
        virtual void unk_fn0005() = 0; // +0x28
    };

    static_assert(sizeof(IPhysUtils) == 0x8, "IPhysUtils size mismatch");
} // namespace SDK
