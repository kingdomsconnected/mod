#pragma once

#include "../../ncrydx12/c_device_object_dx12.h"
#include "c_device_pso_desc.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    /**
     * Common high-level device pipeline-state base.
     *
     * Its scalar deleting destructor proves the exact 0x10-byte extent.
     * Compute initialization updates the packed dword at +0x08 by advancing
     * the count in bits 2..31 while preserving its two state bits; bit zero is
     * then replaced with native-PSO creation success. The atomic dword at
     * +0x0C is the intrusive reference count used by factory-owned pointers.
     */
    class CDevicePSO {
      public:
        virtual ~CDevicePSO() = default;
        virtual bool Opaque01() { return false; }

        bool IsValid() const { return (m_state0008 & 1U) != 0; }
        std::uint32_t GetUpdateCount() const { return m_state0008 >> 2U; }
        std::uint32_t GetStateRaw() const { return m_state0008; }
        std::int32_t GetReferenceCount() const { return m_referenceCount000C; }

      protected:
        std::uint32_t m_state0008;         // 0x08
        std::int32_t m_referenceCount000C; // 0x0C
    };

    /** Compute-specific PSO interface with its resolved hardware shader. */
    class CDeviceComputePSO : public CDevicePSO {
      public:
        ~CDeviceComputePSO() override = default;

        virtual bool Init(const CDeviceComputePSODesc &description) = 0;

        void *GetHardwareShaderInstance() const {
            return m_pHardwareShaderInstance;
        }

      protected:
        void *m_pHardwareShaderInstance; // 0x10
    };

    /**
     * DX12 compute pipeline-state implementation.
     *
     * Both the factory allocation and derived scalar deleting destructor prove
     * the exact 0x28-byte extent. Init stores the selected compute hardware
     * shader at +0x10, uses the NCryDX12::CDevice at +0x18, and retains the
     * resulting native NCryDX12::CComputePSO at +0x20.
     */
    class CDeviceComputePSO_DX12 : public CDeviceComputePSO {
      public:
        ~CDeviceComputePSO_DX12() override = default;

        bool Init(const CDeviceComputePSODesc &description) override;

        NCryDX12::CDevice *GetDevice() const { return m_pDevice; }
        NCryDX12::CComputePSO *GetComputePSO() const {
            return m_pComputePSO;
        }

      protected:
        NCryDX12::CDevice *m_pDevice;         // 0x18
        NCryDX12::CComputePSO *m_pComputePSO; // 0x20
    };

    static_assert(sizeof(CDevicePSO) == 0x10,
                  "CDevicePSO size mismatch");
    static_assert(sizeof(CDeviceComputePSO) == 0x18,
                  "CDeviceComputePSO size mismatch");
    static_assert(sizeof(CDeviceComputePSO_DX12) == 0x28,
                  "CDeviceComputePSO_DX12 size mismatch");
} // namespace SDK
