#pragma once

#include <cstdint>

namespace SDK {
    /**
     * Names one of the renderer's graphics pipelines: CryEngine's
     * `SGraphicsPipelineKey`, one uint32. 0 is the base pipeline every main
     * frame runs, ~0 none at all; `IRenderer::CreateGraphicsPipeline` hands
     * out the rest.
     */
    struct SGraphicsPipelineKey {
        static constexpr std::uint32_t kInvalid = ~0U;
        static constexpr std::uint32_t kBase    = 0;

        std::uint32_t value = kInvalid;

        bool IsValid() const {
            return value != kInvalid;
        }
    };

    static_assert(sizeof(SGraphicsPipelineKey) == 0x4, "SGraphicsPipelineKey size mismatch");
} // namespace SDK
