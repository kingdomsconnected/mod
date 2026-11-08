#pragma once

#include "c_clear_surface_pass.h"
#include "c_texture.h"
#include "s_display_context_description.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    struct RenderDisplayStringLayout {
        union {
            char inlineBuffer[16];
            char *heapBuffer;
        } storage;
        std::uint64_t size;
        std::uint64_t capacity;

        const char *Data() const {
            return capacity < 16 ? storage.inlineBuffer : storage.heapBuffer;
        }
    };

    struct SRenderViewportLayout {
        std::int32_t x;
        std::int32_t y;
        std::int32_t width;
        std::int32_t height;
        float minDepth;
        float maxDepth;
    };

    struct SharedRenderOutputLayout {
        void *object;
        void *controlBlock;
    };

    /**
     * Common state for renderer display contexts.
     *
     * The scalar deleting destructor proves the exact 0x1B0-byte boundary.
     * Construction and resolution-change paths recover the complete leading
     * viewport state, shared render output, color/depth targets, aspect ratio,
     * viewport, embedded CClearSurfacePass, and terminal 0x10-stride vector.
     * The three KCD2 virtuals appended after GetStorableColorOutput remain
     * opaque because their source identities are not yet proven.
     */
    class CRenderDisplayContext {
      protected:
        virtual void ReleaseResources();
        virtual void ChangeDisplayResolution(std::uint32_t displayWidth, std::uint32_t displayHeight, const SRenderViewportLayout &viewport);

      public:
        virtual ~CRenderDisplayContext() = default;
        virtual bool IsSwapChainBacked() const {
            return false;
        }
        virtual void PrePresent() {}
        virtual void PostPresent() {}
        virtual CTexture *GetPresentedBackBuffer() const = 0;
        virtual CTexture *GetCurrentBackBuffer() const   = 0;
        virtual bool IsHighDynamicRangeDisplay() const {
            return false;
        }
        virtual bool OpaqueVirtual09(float &value) const = 0;
        virtual std::uint32_t GetColorFormat() const     = 0;
        virtual CTexture *OpaqueVirtual11()              = 0;
        virtual CTexture *GetStorableColorOutput()       = 0;

        const char *GetName() const {
            return m_name.Data();
        }
        std::uint32_t GetId() const {
            return m_uniqueId;
        }
        bool IsMainViewport() const {
            return m_mainViewport;
        }
        const SDisplayContextDescription &GetDescription() const {
            return m_description;
        }
        std::int32_t GetSuperSamplingX() const {
            return m_superSamplingX;
        }
        std::int32_t GetSuperSamplingY() const {
            return m_superSamplingY;
        }
        float GetResolutionScaleX() const {
            return m_resolutionScaleX;
        }
        float GetResolutionScaleY() const {
            return m_resolutionScaleY;
        }
        std::uint32_t GetDisplayWidth() const {
            return m_displayWidth;
        }
        std::uint32_t GetDisplayHeight() const {
            return m_displayHeight;
        }
        void *GetRenderOutput() const {
            return m_renderOutput.object;
        }
        CTexture *GetColorTarget() const {
            return m_pColorTarget;
        }
        CTexture *GetDepthTarget() const {
            return m_pDepthTarget;
        }
        float GetAspectRatio() const {
            return m_aspectRatio;
        }
        const SRenderViewportLayout &GetViewport() const {
            return m_viewport;
        }
        CClearSurfacePass &GetClearSurfacePass() {
            return m_clearSurfacePass;
        }
        const CClearSurfacePass &GetClearSurfacePass() const {
            return m_clearSurfacePass;
        }

      protected:
        RenderDisplayStringLayout m_name;         // 0x008
        std::uint32_t m_uniqueId;                 // 0x028
        bool m_mainViewport;                      // 0x02C
        std::byte m_padding002D[0x3];             // 0x02D
        SDisplayContextDescription m_description; // 0x030
        std::int32_t m_superSamplingX;            // 0x078
        std::int32_t m_superSamplingY;            // 0x07C
        float m_resolutionScaleX;                 // 0x080
        float m_resolutionScaleY;                 // 0x084
        bool m_resolutionScaleState0088;          // 0x088
        std::byte m_padding0089[0x3];             // 0x089
        std::uint32_t m_displayWidth;             // 0x08C
        std::uint32_t m_displayHeight;            // 0x090
        std::uint32_t m_padding0094;              // 0x094
        SharedRenderOutputLayout m_renderOutput;  // 0x098
        CTexture *m_pColorTarget;                 // 0x0A8
        CTexture *m_pDepthTarget;                 // 0x0B0
        float m_aspectRatio;                      // 0x0B8
        SRenderViewportLayout m_viewport;         // 0x0BC
        std::uint32_t m_padding00D4;              // 0x0D4
        CClearSurfacePass m_clearSurfacePass;     // 0x0D8
        void *m_state0198Begin;                   // 0x198
        void *m_state0198End;                     // 0x1A0
        void *m_state0198Capacity;                // 0x1A8
    };

    static_assert(sizeof(RenderDisplayStringLayout) == 0x20, "render-display string layout mismatch");
    static_assert(sizeof(SRenderViewportLayout) == 0x18, "render viewport layout mismatch");
    static_assert(sizeof(CRenderDisplayContext) == 0x1B0, "CRenderDisplayContext size mismatch");
} // namespace SDK
