#pragma once

#include <cstddef>
#include <cstdint>

namespace SDK {
    class CDeviceResourceLayout;
    class CShader;

    /**
     * Compute pipeline-state creation description.
     *
     * yasli's registered TypeID proves the exact 0x28-byte extent. The
     * serializer names the resource layout, technique, and shader masks, while
     * the DX12 initializer consumes the shader at +0x00 and the same fields at
     * their serialized offsets.
     */
    class CDeviceComputePSODesc {
      public:
        CShader *GetShader() const { return m_pShader0000; }
        std::uint64_t GetRuntimeShaderMask() const {
            return m_shaderFlagsRT0008;
        }
        CDeviceResourceLayout *GetResourceLayout() const {
            return m_pResourceLayout0010;
        }
        std::uint32_t GetTechnique() const { return m_technique0018; }
        std::uint32_t GetMaterialShaderMask() const {
            return m_shaderFlagsMD001C;
        }
        std::uint16_t GetMaterialVariantShaderMask() const {
            return m_shaderFlagsMDV0020;
        }

      protected:
        CShader *m_pShader0000;                       // 0x00
        std::uint64_t m_shaderFlagsRT0008;            // 0x08
        CDeviceResourceLayout *m_pResourceLayout0010; // 0x10
        std::uint32_t m_technique0018;                // 0x18
        std::uint32_t m_shaderFlagsMD001C;             // 0x1C
        std::uint16_t m_shaderFlagsMDV0020;            // 0x20
        std::byte m_padding0022[0x6];                  // 0x22
    };

    /**
     * Graphics pipeline-state creation description.
     *
     * yasli's registered TypeID proves the exact 0x40-byte extent. Its
     * bidirectional serializer directly names and addresses every member from
     * the shared shader/layout prefix through the terminal vertex format.
     */
    class CDeviceGraphicsPSODesc {
      public:
        CShader *GetShader() const { return m_pShader0000; }
        CDeviceResourceLayout *GetResourceLayout() const {
            return m_pResourceLayout0008;
        }
        std::uint64_t GetRuntimeShaderMask() const {
            return m_shaderFlagsRT0010;
        }
        std::uint32_t GetTechnique() const { return m_technique0018; }
        std::uint32_t GetMaterialShaderMask() const {
            return m_shaderFlagsMD001C;
        }
        std::uint16_t GetMaterialVariantShaderMask() const {
            return m_shaderFlagsMDV0020;
        }
        std::uint16_t GetPrimitiveTypeRaw() const {
            return m_primitiveType0022;
        }
        std::uint16_t GetObjectStreamMask() const {
            return m_objectStreamMask0024;
        }
        std::uint8_t GetFlagsRaw() const { return m_flags0026; }
        std::uint8_t GetShaderQualityRaw() const {
            return m_shaderQuality0027;
        }
        std::uint64_t GetRenderPassHandle() const {
            return m_renderPassHandle0028;
        }
        std::uint64_t GetRenderState() const { return m_renderState0030; }
        std::uint32_t GetStencilState() const { return m_stencilState0038; }
        std::uint8_t GetStencilReadMask() const {
            return m_stencilReadMask003C;
        }
        std::uint8_t GetStencilWriteMask() const {
            return m_stencilWriteMask003D;
        }
        std::uint8_t GetCullModeRaw() const { return m_cullMode003E; }
        std::uint8_t GetVertexFormatRaw() const {
            return m_vertexFormat003F;
        }

      protected:
        CShader *m_pShader0000;                       // 0x00
        CDeviceResourceLayout *m_pResourceLayout0008; // 0x08
        std::uint64_t m_shaderFlagsRT0010;            // 0x10
        std::uint32_t m_technique0018;                // 0x18
        std::uint32_t m_shaderFlagsMD001C;             // 0x1C
        std::uint16_t m_shaderFlagsMDV0020;            // 0x20
        std::uint16_t m_primitiveType0022;             // 0x22
        std::uint16_t m_objectStreamMask0024;          // 0x24
        std::uint8_t m_flags0026;                      // 0x26
        std::uint8_t m_shaderQuality0027;              // 0x27
        std::uint64_t m_renderPassHandle0028;           // 0x28
        std::uint64_t m_renderState0030;                // 0x30
        std::uint32_t m_stencilState0038;               // 0x38
        std::uint8_t m_stencilReadMask003C;             // 0x3C
        std::uint8_t m_stencilWriteMask003D;            // 0x3D
        std::uint8_t m_cullMode003E;                    // 0x3E
        std::uint8_t m_vertexFormat003F;                // 0x3F
    };

    static_assert(sizeof(CDeviceComputePSODesc) == 0x28,
                  "CDeviceComputePSODesc size mismatch");
    static_assert(sizeof(CDeviceGraphicsPSODesc) == 0x40,
                  "CDeviceGraphicsPSODesc size mismatch");
} // namespace SDK
