#pragma once

#include "../framework/i_ui_book.h"
#include "../playermodule/c_alchemy.h"
#include "../playermodule/c_reading.h"
#include "c_ui_flash_base.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::guimodule {
    /**
     * The one readable-book screen: 0xA8 bytes (allocated at 0x1818AC481),
     * primary table 0x183D2E510, `I_UIBook` at +0x58 (RTTI mdisp 0x58).
     *
     * GeneralBook.gfx and AlchemyBook.gfx are both driven from here. The
     * movie never reaches the HUD: `I_UIBook::BindToEntityTexture` renders it
     * into the open book's page material as `<GfxName>@<entityId>.ui`, the
     * CryEngine Flash-UI dynamic texture.
     *
     * `SetFullBook` is three calls the mod can make itself: `Texts` and
     * `Images` through `SetFlashArguments`, then
     * `CallFlashFunction("SetFullBook", DyslexiaLevel, Type, VisualCategory)`.
     */
    class C_UIBook
        : public C_UIFlashBase
        , public framework::I_UIBook {
      public:
        /** The reading minigame `OnMinigameAdded` linked, or null outside a reading session. */
        playermodule::C_Reading *GetReading() const {
            return m_pReading;
        }

      private:
        // Constructor sub_180BC8F10.
        CryStringT<char> m_flashName;          // 0x60; "GeneralBook"
        std::byte m_dynTextureHelper[0x28];    // 0x68; wh::guimodule::C_UIFlashDynTextureHelper: vftable, three zeroed pointers, +0x20 unwritten
        std::uint32_t m_value0090;             // 0x90; ctor 0
        std::byte m_padding0094[0x4];
        playermodule::C_Reading *m_pReading; // 0x98
        playermodule::C_Alchemy *m_pAlchemy; // 0xA0

        static void AssertLayout() {
            static_assert(offsetof(C_UIBook, m_flashName) == 0x60, "C_UIBook::m_flashName offset mismatch");
            static_assert(offsetof(C_UIBook, m_dynTextureHelper) == 0x68, "C_UIBook::m_dynTextureHelper offset mismatch");
            static_assert(offsetof(C_UIBook, m_value0090) == 0x90, "C_UIBook::m_value0090 offset mismatch");
            static_assert(offsetof(C_UIBook, m_pReading) == 0x98, "C_UIBook::m_pReading offset mismatch");
            static_assert(offsetof(C_UIBook, m_pAlchemy) == 0xA0, "C_UIBook::m_pAlchemy offset mismatch");
        }
    };

    static_assert(sizeof(C_UIBook) == 0xA8, "C_UIBook size mismatch");
} // namespace SDK::wh::guimodule
