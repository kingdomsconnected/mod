#pragma once

namespace SDK {
    /** CryPhysics geometry interface prefix, verified against CTriMesh's table at 0x183A56B98. */
    struct IGeometry {
        virtual ~IGeometry()      = default; // +0x00
        virtual void unk_fn0001() = 0;       // +0x08, GetType
        virtual int AddRef()      = 0;       // +0x10, 0x18097D1A0
        virtual int Release()     = 0;       // +0x18, 0x18097CA04; frees through the game allocator
    };

    static_assert(sizeof(IGeometry) == 0x08);
} // namespace SDK
