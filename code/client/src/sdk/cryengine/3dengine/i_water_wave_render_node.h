#pragma once

#include "i_render_node.h"

#include <cstdint>

namespace SDK {
    /** Exact runtime parameters stored by a water-wave render node. */
    struct SWaterWaveParams {
        Vec3 position;             // 0x00
        float speed;               // 0x0C
        float speedVariation;      // 0x10
        float lifetime;            // 0x14
        float lifetimeVariation;   // 0x18
        float height;              // 0x1C
        float heightVariation;     // 0x20
        float positionVariation;   // 0x24
        float currentLifetime;     // 0x28
        float currentFrameLifetime;// 0x2C
        float currentSpeed;        // 0x30
        float currentHeight;       // 0x34
    };

    class IWaterWaveRenderNode : public IRenderNode {
      public:
        ~IWaterWaveRenderNode() override = default;

        virtual void Create(std::uint64_t id, const Vec3 *vertices,
                            std::uint32_t vertexCount, const Vec2 &uvScale,
                            const Matrix34 &worldTransform) = 0; // +0x248
        virtual void SetParams(const SWaterWaveParams &params) = 0; // +0x250
        virtual const SWaterWaveParams &GetParams() const = 0; // +0x258
    };

    static_assert(sizeof(SWaterWaveParams) == 0x38,
                  "SWaterWaveParams size mismatch");
    static_assert(sizeof(IWaterWaveRenderNode) == 0x50,
                  "IWaterWaveRenderNode size mismatch");
} // namespace SDK
