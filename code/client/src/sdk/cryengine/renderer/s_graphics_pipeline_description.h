#pragma once

#include <cstddef>
#include <cstdint>

namespace SDK {
    /**
     * Which pipeline class `IRenderer::CreateGraphicsPipeline` builds, as
     * `CD3D9Renderer::RT_CreateGraphicsPipeline` (0x1816832F0) switches on
     * it. Same numbering as CryEngine 5.7's `EGraphicsPipelineType`.
     */
    enum class EGraphicsPipelineType : std::uint32_t {
        Invalid = 0,

        /** `CStandardGraphicsPipeline`: the one every main frame runs. */
        Standard = 1,

        /**
         * `CMinimumGraphicsPipeline`: a depth pre-pass, then forward shading
         * straight into the display context's own target -- no tonemap, so
         * what the clear left there survives where nothing is drawn.
         */
        Minimum = 2,

        /** `CBillboardGraphicsPipeline`: the G-buffer alone, unlit. */
        Billboard = 3,

        Mobile = 4,

        /**
         * `CCharacterToolGraphicsPipeline`: lit deferred shading with
         * subsurface scattering, then a tonemap (KCD2's `ExecuteTonemap`
         * 0x182543EFC, `TonemappingMobile`) that writes no alpha.
         */
        CharacterTool = 5,
    };

    /** CryEngine's `IRenderer::SGraphicsPipelineDescription`. */
    struct SGraphicsPipelineDescription {
        EGraphicsPipelineType type = EGraphicsPipelineType::Invalid;
        std::uint32_t shaderFlags  = 0;
    };

    static_assert(sizeof(SGraphicsPipelineDescription) == 0x8, "SGraphicsPipelineDescription size mismatch");
    static_assert(offsetof(SGraphicsPipelineDescription, shaderFlags) == 0x4, "SGraphicsPipelineDescription::shaderFlags offset mismatch");
} // namespace SDK
