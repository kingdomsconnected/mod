#pragma once

#include "../shared/c_signal.h"
#include "c_ui_flash_base.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::guimodule {
    /**
     * The map screen's own movie, the "ApseMap" element, embedded in C_UIMap
     * at +0x208 and constructed at 0x1809EF244. Every POI, legend row and the
     * checkpoint reach the screen through this object: an array is set, then
     * a declared function reads it -- `PoiMarkers` then `AddPoiMarkers` or
     * `RemovePoiMarkers`.
     *
     * Only the C_UIFlashBase part is modelled. The further vtables at +0x10
     * and +0x58 belong to bases nothing here needs.
     */
    class C_UIMapContent final: public C_UIFlashBase {
      public:
        C_UIMapContent()                                  = delete;
        C_UIMapContent(const C_UIMapContent &)            = delete;
        C_UIMapContent &operator=(const C_UIMapContent &) = delete;

      private:
        // +0x58..+0xD8 are the C_UIExtendedInfo layer (sub_180BC8628, called
        // with "ApseMap"); the rest is the constructor 0x1809EF244.
        const void *m_pExtendedInfoVTable;     // 0x58
        std::uint16_t m_value0060;             // 0x60; ctor 0
        std::byte m_padding0062[0x6];
        CryStringT<char> m_string0068;         // 0x68; ctor empty
        std::byte m_opaque0070[0x38];          // 0x70; not written by either constructor
        void *m_ptr00A8;                       // 0xA8; ctor 0
        std::byte m_dynTextureHelper[0x28];    // 0xB0; wh::guimodule::C_UIFlashDynTextureHelper: vftable, three zeroed pointers, +0x20 unwritten
        SDK::wh::shared::C_Signal m_signal00D8; // 0xD8; dispatch unk_185665AC0
        SDK::wh::shared::C_Signal m_signal00E8; // 0xE8; dispatch unk_185669258
        SDK::wh::shared::C_Signal m_signal00F8; // 0xF8; dispatch unk_185669258
        std::byte m_opaque0108[0x38];          // 0x108; not written by the constructor
        void *m_ptr0140;                       // 0x140; ctor 0
        const void *m_pString0148;             // 0x148; ctor sub_1804FD80C() + 12
        float m_value0150;                     // 0x150; ctor -1.0f
        float m_value0154;                     // 0x154; ctor -1.0f
        float m_value0158;                     // 0x158; ctor -60.0f
        std::uint8_t m_flag015C;               // 0x15C; ctor 0
        std::byte m_padding015D[0x3];
        std::uint8_t m_flag0160;               // 0x160; ctor 0
        std::byte m_padding0161[0x7];

        static void AssertLayout() {
            static_assert(offsetof(C_UIMapContent, m_pExtendedInfoVTable) == 0x58, "C_UIMapContent::m_pExtendedInfoVTable offset mismatch");
            static_assert(offsetof(C_UIMapContent, m_string0068) == 0x68, "C_UIMapContent::m_string0068 offset mismatch");
            static_assert(offsetof(C_UIMapContent, m_dynTextureHelper) == 0xB0, "C_UIMapContent::m_dynTextureHelper offset mismatch");
            static_assert(offsetof(C_UIMapContent, m_signal00D8) == 0xD8, "C_UIMapContent::m_signal00D8 offset mismatch");
            static_assert(offsetof(C_UIMapContent, m_ptr0140) == 0x140, "C_UIMapContent::m_ptr0140 offset mismatch");
            static_assert(offsetof(C_UIMapContent, m_value0158) == 0x158, "C_UIMapContent::m_value0158 offset mismatch");
            static_assert(offsetof(C_UIMapContent, m_flag0160) == 0x160, "C_UIMapContent::m_flag0160 offset mismatch");
        }
    };

    // C_UIMap's next member starts at +0x370, 0x168 past this one.
    static_assert(sizeof(C_UIMapContent) == 0x168, "C_UIMapContent size mismatch");
} // namespace SDK::wh::guimodule
