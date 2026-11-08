#pragma once

#include "../../types.h"

#include <cstddef>

namespace SDK {
    class ICrySizer;

    /**
     * ABI view of CryEngine's header-counted DynArray payload handle.
     *
     * The sole pointer addresses element zero; the signed element count is
     * stored in the native allocation header immediately before that pointer.
     */
    template <typename T>
    struct CryDynArrayLayout {
        T *elements;
    };

    /**
     * Abstract 3D BSP volume, vtable 0x183B6B680.
     *
     * Its five entries are the deleting destructor followed by point
     * containment, memory reporting, buffer export, and buffer import. The
     * nested aliases preserve the native DynArray<DynArray<Vec3>> face-list
     * ABI used by IEntitySystem::CreateBSPTree3D.
     */
    class IBSPTree3D {
      public:
        using CFace = CryDynArrayLayout<Vec3>;
        using FaceList = CryDynArrayLayout<CFace>;

        virtual ~IBSPTree3D() = default; // +0x00
        virtual bool IsInside(const Vec3 &position) const = 0; // +0x08
        virtual void GetMemoryUsage(ICrySizer *sizer) const = 0; // +0x10
        virtual std::size_t WriteToBuffer(void *buffer) const = 0; // +0x18
        virtual void ReadFromBuffer(const void *buffer) = 0; // +0x20
    };

    static_assert(sizeof(CryDynArrayLayout<Vec3>) == 0x8,
                  "CryDynArrayLayout size mismatch");
    static_assert(sizeof(IBSPTree3D::CFace) == 0x8,
                  "IBSPTree3D::CFace size mismatch");
    static_assert(sizeof(IBSPTree3D::FaceList) == 0x8,
                  "IBSPTree3D::FaceList size mismatch");
    static_assert(sizeof(IBSPTree3D) == 0x8, "IBSPTree3D size mismatch");
} // namespace SDK
