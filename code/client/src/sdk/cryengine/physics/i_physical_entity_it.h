#pragma once

namespace SDK {
    /**
     * Abstract CryPhysics physical-entity iterator, vtable 0x18407F910.
     *
     * RTTI identifies this as a global-namespace struct. The table contains
     * exactly seven entries: its virtual destructor and six pure operations.
     * Their stripped source contracts cannot be recovered with certainty, so
     * they remain opaque.
     */
    struct IPhysicalEntityIt {
        virtual ~IPhysicalEntityIt() = default; // +0x00
        virtual void unk_fn0001() = 0;
        virtual void unk_fn0002() = 0;
        virtual void unk_fn0003() = 0;
        virtual void unk_fn0004() = 0;
        virtual void unk_fn0005() = 0;
        virtual void unk_fn0006() = 0; // +0x30
    };

    static_assert(sizeof(IPhysicalEntityIt) == 0x8,
                  "IPhysicalEntityIt size mismatch");
} // namespace SDK
