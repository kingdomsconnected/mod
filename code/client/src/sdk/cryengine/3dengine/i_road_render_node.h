#pragma once

#include "i_render_node.h"

#include <cstdint>

namespace SDK {
    class IRoadRenderNode : public IRenderNode {
      public:
        ~IRoadRenderNode() override = default;

        virtual void SetVertices(const Vec3 *vertices, int vertexCount,
                                 float texCoordBegin, float texCoordEnd,
                                 float globalTexCoordBegin,
                                 float globalTexCoordEnd) = 0; // +0x248
        virtual void SetSortPriority(std::uint8_t priority) = 0; // +0x250
        virtual void SetIgnoreTerrainHoles(bool ignore) = 0; // +0x258
        virtual void GetClipPlanes(Plane *planes, int planeCount,
                                   int vertexId = 0) = 0; // +0x260
        virtual void GetTexCoordInfo(float *texCoordInfo) = 0; // +0x268
    };

    static_assert(sizeof(Plane) == 0x10, "Plane size mismatch");
    static_assert(sizeof(IRoadRenderNode) == 0x50,
                  "IRoadRenderNode size mismatch");
} // namespace SDK
