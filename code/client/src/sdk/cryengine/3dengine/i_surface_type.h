#pragma once

#include <cstdint>

namespace SDK {
    /**
     * One entry of the engine's surface-type table, which is what a ray hit's
     * `surfaceIndex` names. Implemented by `CMaterialSurfaceType`, vftable
     * 0x183A40528; KCD2 emits exactly 17 entries.
     *
     * Its constructor (0x1805F8148) pins the first accessors: the full `mat_*`
     * name at +0x08, that name without its prefix at +0x10, the id at +0x18
     * and the flags at +0x1C.
     */
    struct ISurfaceType {
        virtual ~ISurfaceType() = default; // +0x00
        virtual void Release()  = 0;       // +0x08

        virtual std::uint16_t GetId() const = 0; // +0x10

        /** `mat_wood`, `mat_stone`, `mat_water`. */
        virtual const char *GetName() const = 0; // +0x18

        /** The name with its `mat_` prefix dropped. */
        virtual const char *GetType() const = 0; // +0x20

        virtual int GetFlags() const = 0; // +0x28

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

        /** Fills the parameters in from the table; the manager runs it once, lazily. */
        virtual bool Load(int surfaceId) = 0; // +0x80
    };

    static_assert(sizeof(ISurfaceType) == 0x8, "ISurfaceType size mismatch");
} // namespace SDK
