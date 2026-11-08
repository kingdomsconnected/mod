#pragma once

#include "c_attachment_skin_base.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    /**
     * Concrete CryAnimation skinned character attachment.
     *
     * RTTI proves the global-namespace hierarchy through
     * C_AttachmentSkinBase. Its primary table at 0x183A65A08 contains the 49
     * base entries plus one operation; its secondary IAttachmentSkin table at
     * 0x183A65958 adds nothing. Factory allocation and sized deletion prove
     * the complete 0x198-byte layout, initialized by 0x180976A60.
     */
    class CAttachmentSKIN : public C_AttachmentSkinBase {
      public:
        ~CAttachmentSKIN() override = default;

        virtual void unk_fn0049() = 0;

      private:
        void *m_array0058;                    // 0x058, points at the shared empty sentinel 0x185665C00
        std::uint32_t m_count0060;            // 0x060, zero
        std::uint16_t m_flags0064;            // 0x064, zero
        std::byte m_pad0066[0x2];             // 0x066
        void *m_array0068;                    // 0x068, points at the shared empty sentinel 0x185665C00
        std::uint32_t m_count0070;            // 0x070, zero
        std::uint16_t m_flags0074;            // 0x074, 0x200
        std::byte m_pad0076[0x2];             // 0x076
        void *m_pointer0078;                  // 0x078, null; released through 0x1809780E8 when set
        void *m_pRenderMeshsSW[2];            // 0x080, _smart_ptr<IRenderMesh>[2]
        CryStringT<char> m_sSoftwareMeshName; // 0x090
        std::byte m_vertexAnimation[0xD0];    // 0x098, CVertexAnimation (vtable), built by 0x180976C64
        std::byte m_state0168[0x30];          // 0x168, zeroed in the constructor

        static void AssertLayout() {
            static_assert(offsetof(CAttachmentSKIN, m_pRenderMeshsSW) == 0x80, "CAttachmentSKIN::m_pRenderMeshsSW offset mismatch");
            static_assert(offsetof(CAttachmentSKIN, m_sSoftwareMeshName) == 0x90, "CAttachmentSKIN::m_sSoftwareMeshName offset mismatch");
            static_assert(offsetof(CAttachmentSKIN, m_vertexAnimation) == 0x98, "CAttachmentSKIN::m_vertexAnimation offset mismatch");
        }
    };

    static_assert(sizeof(CAttachmentSKIN) == 0x198,
                  "CAttachmentSKIN size mismatch");
} // namespace SDK
