#pragma once

#include <cstddef>

namespace SDK {
    /**
     * Common front-end render-pass state.
     *
     * The scalar deleting destructor proves the exact 0x50-byte extent. The
     * constructor initializes the CryString label payload at +0x08 and the
     * LocalDynArray<uint8, 16> input-variable cache at +0x10, including its
     * embedded 16-byte storage at +0x40. That cache backs the pass helpers
     * which compare serialized trivial inputs before consulting IsDirty().
     */
    class CRenderPassBase {
      public:
        virtual ~CRenderPassBase() = default;
        virtual bool IsDirty() const { return false; }

        const char *GetLabel() const { return m_pLabelData0008; }

      protected:
        char *m_pLabelData0008;              // 0x08, CryStringT<char>
        std::byte m_inputVariables0010[0x40]; // 0x10, LocalDynArray<uint8, 16>
    };

    static_assert(sizeof(CRenderPassBase) == 0x50,
                  "CRenderPassBase size mismatch");
} // namespace SDK
