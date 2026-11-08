#pragma once

#include "../../types.h"

#include <cstddef>

namespace SDK {
    // PrepareRenderChunks (0x18051DA4C) walks 0x30-byte records and reads
    // nFirstIndexId and nNumIndices.
    struct SMeshSubset {
        Vec3 vCenter;          // 0x00
        float fRadius;         // 0x0C
        float fTexelDensity;   // 0x10
        int nFirstIndexId;     // 0x14
        int nNumIndices;       // 0x18
        int nFirstVertId;      // 0x1C
        int nNumVerts;         // 0x20
        int nMatID;            // 0x24
        int nMatFlags;         // 0x28
        int nPhysicalizeType;  // 0x2C

      private:
        static void AssertLayout() {
            static_assert(offsetof(SMeshSubset, nFirstIndexId) == 0x14, "SMeshSubset::nFirstIndexId offset mismatch");
            static_assert(offsetof(SMeshSubset, nNumIndices) == 0x18, "SMeshSubset::nNumIndices offset mismatch");
            static_assert(offsetof(SMeshSubset, nMatID) == 0x24, "SMeshSubset::nMatID offset mismatch");
        }
    };

    static_assert(sizeof(SMeshSubset) == 0x30, "SMeshSubset size mismatch");
} // namespace SDK
