#pragma once

#include "../framework/i_source_monitor_listener.h"
#include "../framework/i_ui_hud_mask.h"
#include "c_ui_flash_base.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::guimodule {
    /**
     * Native HUD policy. RTTI pins bases at 0, 0x58 and 0x60. Construction
     * at 0x1817944DC and allocation at 0x1817943BC establish size 0x320.
     * Query the inherited interface to preserve the game's current rules.
     */
    class C_UIHudMask: public C_UIFlashBase, public framework::I_UIHudMask, public framework::I_SourceMonitorListener {
      public:
        /** Null before native GUI construction or after its destruction. */
        static C_UIHudMask *GetInstance();

      private:
        // Constructor 0x1817944DC.
        void *m_ptr0068;                         // 0x68; ctor 0
        std::uint64_t m_elementValues[28];       // 0x70; one per HUD element, zeroed by the constructor's memset
        std::uint32_t m_gameplayVisibleElements; // 0x150, before native preferences.
        std::byte m_padding0154[0x4];
        // One pair per HUD element (sub_180BC67C0: first 0x7FFFFFFFFFFFF,
        // second 0); InitializeElementSourceMasks (0x1817945A8) fills them.
        std::uint64_t m_elementSourceMasks[28][2]; // 0x158
        bool m_flag0318;                         // 0x318; ctor 0
        std::byte m_padding0319[0x7];

        static void AssertLayout() {
            static_assert(offsetof(C_UIHudMask, m_ptr0068) == 0x68, "C_UIHudMask::m_ptr0068 offset mismatch");
            static_assert(offsetof(C_UIHudMask, m_elementValues) == 0x70, "C_UIHudMask::m_elementValues offset mismatch");
            static_assert(offsetof(C_UIHudMask, m_gameplayVisibleElements) == 0x150, "C_UIHudMask::m_gameplayVisibleElements offset mismatch");
            static_assert(offsetof(C_UIHudMask, m_elementSourceMasks) == 0x158, "C_UIHudMask::m_elementSourceMasks offset mismatch");
            static_assert(offsetof(C_UIHudMask, m_flag0318) == 0x318, "C_UIHudMask::m_flag0318 offset mismatch");
        }
    };

    static_assert(sizeof(C_UIHudMask) == 0x320, "C_UIHudMask size mismatch");
} // namespace SDK::wh::guimodule
