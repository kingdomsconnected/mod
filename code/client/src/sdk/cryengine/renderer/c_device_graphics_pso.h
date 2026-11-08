#pragma once

#include "c_device_compute_pso.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    /**
     * Graphics-specific high-level PSO interface.
     *
     * Its scalar deleting destructor proves the exact 0x40-byte extent. The
     * native initializer copies five resolved graphics hardware-shader
     * instances to +0x10 and clears the terminal stage byte at +0x38. KCD2's
     * third virtual takes an additional one-byte asynchronous-compilation
     * input absent from older public declarations, so its stripped source
     * signature/name intentionally remain opaque.
     */
    class CDeviceGraphicsPSO : public CDevicePSO {
      public:
        ~CDeviceGraphicsPSO() override = default;

        virtual std::uint8_t Opaque02(
            const CDeviceGraphicsPSODesc &description,
            const std::uint8_t *asyncCompilationState) = 0;

        void *GetHardwareShaderInstance(std::size_t stage) const {
            return stage < 5 ? m_pHardwareShaderInstances[stage] : nullptr;
        }
        std::uint8_t GetGraphicsStageStateRaw() const {
            return m_graphicsStageState0038;
        }

      protected:
        void *m_pHardwareShaderInstances[5]; // 0x10
        std::uint8_t m_graphicsStageState0038; // 0x38
        std::byte m_padding0039[0x7];           // 0x39
    };

    /**
     * DX12 graphics pipeline-state implementation.
     *
     * Factory allocation and scalar deletion both prove 0x60 bytes. Native
     * initialization stores the translated primitive topology at +0x40, uses
     * the NCryDX12::CDevice at +0x48, retains the NCryDX12::CGraphicsPSO at
     * +0x50, and serializes compilation state through the SRW lock at +0x58.
     */
    class CDeviceGraphicsPSO_DX12 : public CDeviceGraphicsPSO {
      public:
        ~CDeviceGraphicsPSO_DX12() override = default;

        std::uint8_t Opaque02(
            const CDeviceGraphicsPSODesc &description,
            const std::uint8_t *asyncCompilationState) override;

        std::uint32_t GetPrimitiveTopologyRaw() const {
            return m_primitiveTopology0040;
        }
        NCryDX12::CDevice *GetDevice() const { return m_pDevice; }
        NCryDX12::CGraphicsPSO *GetGraphicsPSO() const {
            return m_pGraphicsPSO;
        }

      protected:
        std::uint32_t m_primitiveTopology0040; // 0x40
        std::uint32_t m_padding0044;           // 0x44
        NCryDX12::CDevice *m_pDevice;          // 0x48
        NCryDX12::CGraphicsPSO *m_pGraphicsPSO; // 0x50
        void *m_compilationLock;                // 0x58, SRWLOCK
    };

    static_assert(sizeof(CDeviceGraphicsPSO) == 0x40,
                  "CDeviceGraphicsPSO size mismatch");
    static_assert(sizeof(CDeviceGraphicsPSO_DX12) == 0x60,
                  "CDeviceGraphicsPSO_DX12 size mismatch");
} // namespace SDK
