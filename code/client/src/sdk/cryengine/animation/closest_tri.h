#pragma once

#include "../../types.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    /**
     * CryAnimation's three-vertex skinned anchor for face attachments.
     *
     * The element constructor at 0x183743A00, run three times at
     * CAttachmentFACE+0xA0 with stride 0x28, zeroes the position, joint ids
     * and weights and sets the vertex index to -1 -- CryEngine's
     * ClosestTri::AttSkinVertex constructor member for member.
     */
    struct ClosestTri {
        struct AttSkinVertex {
            Vec3 m_attTriPos;                // 0x00
            std::int16_t m_attJointIDs[4];   // 0x0C
            float m_attWeights[4];           // 0x14
            std::uint32_t m_vertexIdx;       // 0x24, -1 in the constructor
        };

        AttSkinVertex v[3];
    };

    static_assert(sizeof(ClosestTri::AttSkinVertex) == 0x28, "ClosestTri::AttSkinVertex size mismatch");
    static_assert(offsetof(ClosestTri::AttSkinVertex, m_attWeights) == 0x14, "ClosestTri::AttSkinVertex::m_attWeights offset mismatch");
    static_assert(offsetof(ClosestTri::AttSkinVertex, m_vertexIdx) == 0x24, "ClosestTri::AttSkinVertex::m_vertexIdx offset mismatch");
    static_assert(sizeof(ClosestTri) == 0x78, "ClosestTri size mismatch");
} // namespace SDK
