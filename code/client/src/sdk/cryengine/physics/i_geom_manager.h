#pragma once

#include "phys_geometry.h"
#include "strided_pointer.h"

namespace SDK {
    /** CTriMesh::Create (0x180993128) and CalcPhysicalProperties (0x18080F238) verify these flags. No shared flags means the game copies input buffers. */
    enum meshflags : int {
        mesh_AABB          = 0x40,
        mesh_multicontact2 = 0x400,
        mesh_no_vtx_merge  = 0x20000,
        mesh_always_static = 0x100000,
    };

    /**
     * Abstract CryPhysics geometry manager, vtable 0x184080088.
     *
     * KCD2 emits exactly 23 entries; CGeomManager's matching table is 0x183A66788.
     */
    class IGeomManager {
      public:
        virtual ~IGeomManager() = default; // +0x00
        virtual void unk_fn0001() = 0;
        virtual void unk_fn0002() = 0;
        virtual void unk_fn0003() = 0;
        // Scalar overload is slot 4; leave the BVParams overload at slot 3 opaque to preserve MSVC's overload ordering.
        virtual IGeometry *CreateMesh(strided_pointer<const Vec3> vertices, strided_pointer<unsigned short> indices, char *materials, int *foreignIndices, int triangleCount, int flags, float approximationTolerance = 0.0F, int minTrianglesPerNode = 2, int maxTrianglesPerNode = 4,
            float favorAABB = 1.0F)                                                                                                               = 0; // +0x20, 0x180992DEC
        virtual void unk_fn0005() = 0;
        virtual void DestroyGeometry(IGeometry *geometry)                                                                                         = 0; // +0x30, 0x180C5FF50; releases the CreateMesh reference
        virtual phys_geometry *RegisterGeometry(IGeometry *geometry, int surfaceIndex = 0, int *materialMapping = nullptr, int materialCount = 0) = 0; // +0x38, 0x1808100EC; takes its own geometry reference
        virtual int AddRefGeometry(phys_geometry *geometry)                                                                                       = 0; // +0x40, 0x18084BF60
        virtual int UnregisterGeometry(phys_geometry *geometry)                                                                                   = 0; // +0x48, 0x18084A954; releases one registered reference
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
        virtual void unk_fn0022() = 0; // +0xB0
    };

    static_assert(sizeof(IGeomManager) == 0x8, "IGeomManager size mismatch");
} // namespace SDK
