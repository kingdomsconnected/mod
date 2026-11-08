#pragma once

#include <cstddef>
#include <cstdint>

namespace SDK {
    class CGpuBuffer;

    /** Exact 0x10-byte resource/view binding used by render-pass descriptions. */
    struct DeviceRenderPassBindingLayout {
        void *m_pResource;                 // 0x00
        std::uint64_t m_viewAndFlags0008;  // 0x08
    };

    /**
     * Device render-pass attachment description.
     *
     * Construction proves two four-entry color-attachment arrays, three
     * output-UAV bindings, and paired depth-related bindings, all with a 0x10
     * stride. The invalidation callback clears both color records for a slot,
     * or both depth records for the sentinel depth slot, before marking the
     * description dirty at +0x118. Native CDeviceRenderPass creation consumes
     * the primary color array at +0x40 and depth binding at +0xB0.
     *
     * KCD2 added the auxiliary attachment array and depth record relative to
     * the older public layout; their precise source terminology is therefore
     * intentionally not guessed.
     */
    class CDeviceRenderPassDesc {
      public:
        static constexpr std::size_t MaxRenderTargetCount = 4;
        static constexpr std::size_t MaxOutputUavCount = 3;

        const DeviceRenderPassBindingLayout *GetAuxiliaryColorAttachments()
            const {
            return m_auxiliaryColorAttachments0000;
        }
        const DeviceRenderPassBindingLayout *GetRenderTargets() const {
            return m_renderTargets0040;
        }
        const DeviceRenderPassBindingLayout *GetOutputUavs() const {
            return m_outputUavs0080;
        }
        const DeviceRenderPassBindingLayout &GetDepthTarget() const {
            return m_depthTarget00B0;
        }
        const DeviceRenderPassBindingLayout &GetAuxiliaryDepthAttachment()
            const {
            return m_auxiliaryDepthAttachment00C0;
        }
        void *GetInvalidationCallbackOwner() const {
            return m_pInvalidationCallbackOwner;
        }
        bool HasChanged() const { return m_resourcesInvalidated0118; }

      protected:
        DeviceRenderPassBindingLayout
            m_auxiliaryColorAttachments0000[MaxRenderTargetCount]; // 0x000
        DeviceRenderPassBindingLayout
            m_renderTargets0040[MaxRenderTargetCount];             // 0x040
        DeviceRenderPassBindingLayout
            m_outputUavs0080[MaxOutputUavCount];                   // 0x080
        DeviceRenderPassBindingLayout m_depthTarget00B0;           // 0x0B0
        DeviceRenderPassBindingLayout m_auxiliaryDepthAttachment00C0; // 0x0C0
        std::byte m_invalidationCallback00D0[0x40];                 // 0x0D0
        void *m_pInvalidationCallbackOwner;                         // 0x110
        bool m_resourcesInvalidated0118;                            // 0x118
        std::byte m_padding0119[0x7];                               // 0x119
    };

    static_assert(sizeof(DeviceRenderPassBindingLayout) == 0x10,
                  "DeviceRenderPassBindingLayout size mismatch");
    static_assert(sizeof(CDeviceRenderPassDesc) == 0x120,
                  "CDeviceRenderPassDesc size mismatch");
} // namespace SDK
