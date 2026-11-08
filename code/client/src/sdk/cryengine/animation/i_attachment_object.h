#pragma once

#include <cstdint>

namespace SDK {
    struct IAttachment;

    /**
     * Abstract CryAnimation attachment-payload interface.
     *
     * RTTI identifies this global-namespace type as a struct. Its table at
     * 0x183A70F70 contains the virtual destructor, ten pure operations, and
     * eleven emitted defaults. Their stripped source contracts remain opaque.
     */
    struct IAttachmentObject {
        /**
         * Which payload a socket carries, CryEngine's `IAttachmentObject::EType`.
         *
         * `Entity` is the value this binary proves: `CEntityAttachment`'s
         * table at 0x183A68F68 returns 4 from this slot. The rest are the
         * stock CryAnimation order around it.
         */
        enum class EType : std::uint32_t {
            Unknown  = 0,
            StatObj  = 1,
            Skeleton = 2,
            SkinMesh = 3,
            Entity   = 4,
            Light    = 5,
            Effect   = 6,
        };

        virtual ~IAttachmentObject() = default; // +0x000
        /** What `this` is, which is the only safe way to downcast a payload. */
        virtual EType GetAttachmentType() = 0; // +0x008
        /**
         * Puts the payload where the socket now is, once per attachment
         * update. `CEntityAttachment`'s writes the socket transform into the
         * bound entity, which is why an entity attachment follows a bone
         * while rendering entirely on its own.
         */
        virtual void ProcessAttachment(IAttachment *socket) = 0; // +0x010
        virtual void unk_fn0003() {} // emitted default
        virtual void unk_fn0004() {} // emitted default
        virtual void unk_fn0005() {} // emitted default
        virtual void unk_fn0006() = 0;
        virtual void unk_fn0007() = 0;
        virtual void unk_fn0008() {} // emitted default
        virtual void unk_fn0009() {} // emitted default
        virtual void unk_fn0010() {} // emitted default
        virtual void unk_fn0011() {} // emitted default
        virtual void unk_fn0012() = 0;
        virtual void unk_fn0013() = 0;
        virtual void unk_fn0014() = 0;
        virtual void unk_fn0015() = 0;
        virtual void unk_fn0016() = 0;
        virtual void unk_fn0017() {} // emitted default
        virtual void unk_fn0018() = 0;
        virtual void unk_fn0019() {} // emitted default
        virtual void unk_fn0020() {} // emitted default
        virtual void unk_fn0021() {} // +0x0A8, emitted default
    };

    static_assert(sizeof(IAttachmentObject) == 0x8,
                  "IAttachmentObject size mismatch");
} // namespace SDK
