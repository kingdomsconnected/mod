#pragma once

#include "c_device_render_pass_desc.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    /**
     * API-independent device render-pass base.
     *
     * Its scalar deleting destructor proves 0x30 bytes. Construction and the
     * common update path identify the 64-bit description hash at +0x08, update
     * count at +0x10, and validity byte at +0x28. The intervening release-
     * diagnostic state remains opaque. Only UpdateImpl's source identity is
     * proven among the three non-destructor virtual slots.
     */
    class CDeviceRenderPass_Base {
      public:
        virtual ~CDeviceRenderPass_Base() = default;
        virtual void Opaque01() {}
        virtual bool Opaque02() = 0;

        std::uint64_t GetHash() const { return m_hash0008; }
        std::uint32_t GetUpdateCount() const { return m_updateCount0010; }
        bool IsValid() const { return m_valid0028; }

      protected:
        virtual bool UpdateImpl(const CDeviceRenderPassDesc &description) = 0;

        std::uint64_t m_hash0008;          // 0x08
        std::uint32_t m_updateCount0010;   // 0x10
        std::byte m_diagnosticState0014[0x14]; // 0x14
        bool m_valid0028;                  // 0x28
        std::byte m_padding0029[0x7];      // 0x29
    };

    /**
     * Concrete DX12 render-pass view set.
     *
     * The shared-object allocator zeroes an exact 0x60-byte object and sets
     * the base validity byte. UpdateImpl resolves up to four native render-
     * target views into +0x30, records their count at +0x50, and resolves the
     * optional native depth-stencil view at +0x58.
     */
    class CDeviceRenderPass : public CDeviceRenderPass_Base {
      public:
        ~CDeviceRenderPass() override = default;
        bool Opaque02() override { return true; }

        std::uint32_t GetRenderTargetCount() const {
            return m_renderTargetCount0050;
        }
        void *GetRenderTargetView(std::size_t index) const {
            return index < m_renderTargetCount0050 && index < 4
                       ? m_renderTargetViews[index]
                       : nullptr;
        }
        void *GetDepthStencilView() const { return m_pDepthStencilView; }

      protected:
        bool UpdateImpl(const CDeviceRenderPassDesc &description) override;

        void *m_renderTargetViews[4];       // 0x30
        std::uint32_t m_renderTargetCount0050; // 0x50
        std::uint32_t m_padding0054;        // 0x54
        void *m_pDepthStencilView;          // 0x58
    };

    static_assert(sizeof(CDeviceRenderPass_Base) == 0x30,
                  "CDeviceRenderPass_Base size mismatch");
    static_assert(sizeof(CDeviceRenderPass) == 0x60,
                  "CDeviceRenderPass size mismatch");
} // namespace SDK
