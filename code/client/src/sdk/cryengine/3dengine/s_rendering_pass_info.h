#pragma once

#include "../renderer/i_render_view.h"
#include "../renderer/s_display_context_key.h"
#include "../renderer/s_graphics_pipeline_key.h"
#include "../system/c_camera_native.h"

#include "../../patterns.h"

#include <utils/hooking/hooking.h>

#include <cstddef>
#include <cstdint>

namespace SDK {
    /**
     * One rendering pass: which camera, which render view, which display
     * context and pipeline. KCD2's `SRenderingPassInfo` layout, checked against
     * `CreateGeneralPassRenderingInfo` (0x18043E940) and the setters it calls.
     *
     * The size is inferred from every caller's stack frame rather than a
     * sized allocation. The only member it owns is the render view, which
     * `ReleaseRenderView` lets go of once the pass has been ended.
     */
    struct SRenderingPassInfo {
        /** `SRenderingPassInfo::DEFAULT_FLAGS`, as `CSystem::Render` passes it. */
        static constexpr std::uint32_t kDefaultFlags = 0x2F7FF;

        /** `ENTITIES`: the only system an object-only pass has anything for. */
        static constexpr std::uint32_t kEntities = 0x8;

        /**
         * A general pass through `pipeline` into `context`, seen by `camera`:
         * builds the pass, copies the camera into the 3D engine's per-frame
         * pool and takes a render view from the renderer. A static returning
         * by value, so the result is the hidden first argument.
         *
         * The native ignores its aux-window argument -- every caller passes
         * false and the build folded it away -- so a secondary view sets
         * `auxWindow` itself afterwards. It also writes the 3D engine's zoom
         * state as if this were the main view; a caller restores it.
         */
        static void CreateGeneralPass(SRenderingPassInfo *result, SGraphicsPipelineKey pipeline, const CCamera &camera, std::uint32_t flags, const SDisplayContextKey &context) {
            hook::call<SRenderingPassInfo *>(gPatterns.SRenderingPassInfo__CreateGeneralPassRenderingInfo, result, pipeline.value, &camera, flags, false, &context);
        }

        /** Lets go of the render view the pass took; after `EF_EndEf3D`, once. */
        void ReleaseRenderView() {
            if (renderView != nullptr) {
                renderView->Release();
                renderView = nullptr;
            }
        }

        std::uint8_t threadId;          // 0x00
        std::uint8_t renderStackLevel;  // 0x01
        std::uint8_t shadowMapType;     // 0x02
        std::uint8_t cameraUnderWater;  // 0x03
        std::uint32_t renderingFlags;   // 0x04
        float zoomFactor;               // 0x08
        std::uint32_t mainFrameId;      // 0x0C
        float timerValue;               // 0x10, KCD2-only
        std::uint32_t renderItemSorter; // 0x14
        const CCamera *camera;          // 0x18, the engine's pool copy
        IRenderView *renderView;        // 0x20, owned
        std::uint8_t shadowSide;        // 0x28
        std::uint8_t shadowLod;         // 0x29
        std::uint8_t shadowCacheLod;    // 0x2A
        bool zoomInProgress;            // 0x2B
        std::uint8_t zoomMode;          // 0x2C
        bool auxWindow;                 // 0x2D
        std::byte padding002E[0x2];
        SDisplayContextKey displayContext;     // 0x30
        SGraphicsPipelineKey graphicsPipeline; // 0x40
        std::byte padding0044[0x4];
        std::byte unknown0048[0xC]; // 0x48, KCD2-only, zeroed by the constructor
        std::byte padding0054[0x4];
        void *shadowPasses; // 0x58
    };

    static_assert(offsetof(SRenderingPassInfo, renderingFlags) == 0x04, "SRenderingPassInfo::renderingFlags offset mismatch");
    static_assert(offsetof(SRenderingPassInfo, camera) == 0x18, "SRenderingPassInfo::camera offset mismatch");
    static_assert(offsetof(SRenderingPassInfo, renderView) == 0x20, "SRenderingPassInfo::renderView offset mismatch");
    static_assert(offsetof(SRenderingPassInfo, auxWindow) == 0x2D, "SRenderingPassInfo::auxWindow offset mismatch");
    static_assert(offsetof(SRenderingPassInfo, displayContext) == 0x30, "SRenderingPassInfo::displayContext offset mismatch");
    static_assert(offsetof(SRenderingPassInfo, graphicsPipeline) == 0x40, "SRenderingPassInfo::graphicsPipeline offset mismatch");
    static_assert(offsetof(SRenderingPassInfo, shadowPasses) == 0x58, "SRenderingPassInfo::shadowPasses offset mismatch");
    static_assert(sizeof(SRenderingPassInfo) == 0x60, "SRenderingPassInfo size mismatch");
} // namespace SDK
