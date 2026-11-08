#pragma once

#include "i_surface_type.h"

namespace SDK {
    /**
     * The engine's surface-type registry, implemented by
     * `CSurfaceTypeManager`, vftable 0x183A404B8; KCD2 emits exactly 9
     * entries. `CMatMan`'s constructor allocates one and keeps it at
     * `CMatMan+0xB8`, so there is one per process.
     *
     * Reaching it directly is only needed to look a type up without the
     * engine's warning; `IMaterialManager` forwards both lookups.
     */
    struct ISurfaceTypeManager {
        virtual ~ISurfaceTypeManager() = default; // +0x00

        virtual void LoadSurfaceTypes() = 0; // +0x08

        /** `warn` writes the engine's "undefined surface type" line. */
        virtual ISurfaceType *GetSurfaceTypeByName(const char *name, const char *why, bool warn) = 0; // +0x10

        /** Never null: an id outside 1..254, and an unclaimed slot, fall back to the default type. */
        virtual ISurfaceType *GetSurfaceType(int surfaceId, const char *why) = 0; // +0x18

        virtual void unk_fn0004() = 0;
        virtual void unk_fn0005() = 0;
        virtual void unk_fn0006() = 0;
        virtual void unk_fn0007() = 0;
        virtual void unk_fn0008() = 0; // +0x40
    };

    static_assert(sizeof(ISurfaceTypeManager) == 0x8, "ISurfaceTypeManager size mismatch");
} // namespace SDK
