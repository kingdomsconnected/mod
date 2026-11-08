#pragma once

#include <cstddef>

namespace SDK::wh::entitymodule {
    /** Seven-slot common editor-properties contract. */
    class I_CommonEditorProperties {
      public:
        virtual ~I_CommonEditorProperties() = default;
        virtual void unk_fn0001() = 0;
        virtual void unk_fn0002() = 0;
        virtual void unk_fn0003() = 0;
        virtual void unk_fn0004() = 0;
        virtual void unk_fn0005() = 0;
        virtual void unk_fn0006() = 0;
    };

    static_assert(sizeof(I_CommonEditorProperties) == 0x8,
                  "I_CommonEditorProperties size mismatch");

    /**
     * Light-specific common editor properties embedded by C_ItemSlot.
     *
     * RTTI proves I_CommonEditorProperties at +0x00. The constructor at
     * 0x1803A69A0 initializes all state through +0x28, while its table at
     * 0x183A2DB90 contains exactly the seven inherited contracts.
     */
    class C_LightCommonEditorProperties : public I_CommonEditorProperties {
      public:
        ~C_LightCommonEditorProperties() override = default;

      private:
        const char *m_pState0008; // CryStringT<char> payload
        std::byte m_state0010[0x10];
        const char *m_pState0020; // CryStringT<char> payload
        const char *m_pState0028; // CryStringT<char> payload
    };

    static_assert(sizeof(C_LightCommonEditorProperties) == 0x30,
                  "C_LightCommonEditorProperties size mismatch");
} // namespace SDK::wh::entitymodule
