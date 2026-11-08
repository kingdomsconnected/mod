#pragma once

#include "../../types.h"
#include "s_mesh_color.h"
#include "s_mesh_subset.h"

#include <algorithm>
#include <cstddef>

namespace SDK {
    /**
     * The mesh data and stream layout used by KCD2.
     * Constructor 0x18048C0D0; allocation 0x18048C1E0 proves size 0x110.
     * GetStreamInfo (0x18048F4C8) proves colours at +0x58, stream 7:
     * KCD2 has two UV streams.
     */
    class CMesh {
      public:
        enum EStream {
            POSITIONS    = 0,
            POSITIONSF16 = 1,
            COLORS_0     = 7,
            INDICES      = 9,
            P3S_C4B_T2S  = 14,
            LAST_STREAM  = 15,
        };

        // Vtable 0x183A39AF0: deleting destructor, then ReallocStream.
        virtual ~CMesh() = default;

        // 0x18048F1BC. An unchanged count still detaches a shared stream
        // through the game's allocator. Owned streams need no allocation.
        virtual void ReallocStream(int stream, int newCount) = 0;

        const SMeshColor *GetColors() const {
            return m_pColor0;
        }

        int GetColorCount() const {
            return m_streamSize[COLORS_0];
        }

        SMeshColor *GetWritableColors(int count) {
            ReallocStream(COLORS_0, count);
            return m_pColor0;
        }

        // GetVertexCount (0x180520634) uses these three stream counts.
        int GetVertexCount() const {
            return std::max({m_streamSize[POSITIONS], m_streamSize[POSITIONSF16], m_streamSize[P3S_C4B_T2S]});
        }

        const std::uint16_t *GetIndices() const {
            return m_pIndices;
        }
        int GetIndexCount() const {
            return m_streamSize[INDICES];
        }
        const DynArray<SMeshSubset> &GetSubsets() const {
            return m_subsets;
        }

      private:
        // Stock member order. The constructor 0x18048C0D0 zeroes every
        // pointer, seeds m_bbox with +/-1e15 and empties the four DynArrays
        // at 0xD8, 0xF0, 0xF8 and 0x108; KCD2 swaps the stock extra-bone
        // mapping pointer for a second UV stream.
        void *m_pFaces;                              // 0x08, SMeshFace *
        std::int32_t *m_pTopologyIds;                // 0x10
        std::uint16_t *m_pIndices;                   // 0x18
        Vec3 *m_pPositions;                          // 0x20
        void *m_pPositionsF16;                       // 0x28, Vec3f16 *
        void *m_pNorms;                              // 0x30, SMeshNormal *
        void *m_pTangents;                           // 0x38, SMeshTangents *
        void *m_pQTangents;                          // 0x40, SMeshQTangents *
        void *m_pTexCoord;                           // 0x48, SMeshTexCoord *
        void *m_pTexCoord1;                          // 0x50, SMeshTexCoord *, KCD2's second UV stream
        SMeshColor *m_pColor0;                       // 0x58
        SMeshColor *m_pColor1;                       // 0x60
        std::int32_t *m_pVertMats;                   // 0x68
        void *m_pP3S_C4B_T2S;                        // 0x70, SVF_P3S_C4B_T2S *
        void *m_pBoneMapping;                        // 0x78, SMeshBoneMapping_uint16 *
        std::int32_t m_nCoorCount;                   // 0x80
        int m_streamSize[LAST_STREAM];               // 0x84
        AABB m_bbox;                                 // 0xC0
        DynArray<SMeshSubset> m_subsets;             // 0xD8
        std::uint32_t m_nSharedStreamMask;           // 0xE0
        float m_texMappingDensity;                   // 0xE4
        float m_geometricMeanFaceArea;               // 0xE8
        DynArray<std::byte> m_vertexDeltas;          // 0xF0, DynArray<Vec4>
        DynArray<std::uint64_t> m_vertexMorphsBitfield; // 0xF8
        std::uint32_t m_numMorphs;                   // 0x100
        DynArray<std::uint32_t> m_verticesDeltaOffsets; // 0x108

        static void AssertLayout() {
            static_assert(offsetof(CMesh, m_pIndices) == 0x18, "CMesh::m_pIndices offset mismatch");
            static_assert(offsetof(CMesh, m_pColor0) == 0x58, "CMesh::m_pColor0 offset mismatch");
            static_assert(offsetof(CMesh, m_streamSize) == 0x84, "CMesh::m_streamSize offset mismatch");
            static_assert(offsetof(CMesh, m_subsets) == 0xD8, "CMesh::m_subsets offset mismatch");
            static_assert(offsetof(CMesh, m_bbox) == 0xC0, "CMesh::m_bbox offset mismatch");
            static_assert(offsetof(CMesh, m_nSharedStreamMask) == 0xE0, "CMesh::m_nSharedStreamMask offset mismatch");
            static_assert(offsetof(CMesh, m_verticesDeltaOffsets) == 0x108, "CMesh::m_verticesDeltaOffsets offset mismatch");
        }
    };

    static_assert(sizeof(CMesh) == 0x110, "CMesh size mismatch");
} // namespace SDK
