#pragma once

#include "i_render_node.h"

#include <cstddef>

namespace SDK {
    /** Exact configuration payload for a localized distance-cloud node. */
    struct SDistanceCloudProperties {
        Vec3 position;             // 0x00
        float sizeX;               // 0x0C
        float sizeY;               // 0x10
        float rotationZ;           // 0x14
        const char *materialName;  // 0x18
    };

    class IDistanceCloudRenderNode : public IRenderNode {
      public:
        ~IDistanceCloudRenderNode() override = default;

        virtual void SetProperties(
            const SDistanceCloudProperties &properties) = 0; // +0x248
    };

    static_assert(offsetof(SDistanceCloudProperties, materialName) == 0x18,
                  "SDistanceCloudProperties::materialName offset mismatch");
    static_assert(sizeof(SDistanceCloudProperties) == 0x20,
                  "SDistanceCloudProperties size mismatch");
    static_assert(sizeof(IDistanceCloudRenderNode) == 0x50,
                  "IDistanceCloudRenderNode size mismatch");
} // namespace SDK
