#pragma once

#include "../framework/i_source_monitor_listener.h"
#include "c_ui_flash_base.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace SDK::wh::guimodule {
    /**
     * The full-screen overlay page, and the owner of the game's own screen
     * tint.
     *
     * The game grades the whole screen from the player's condition through
     * the renderer's eight `Global_User_*` post-effect parameters, and this
     * page is their only writer in normal play: `0x1808BDA44` computes a
     * cyan/magenta/yellow/darkness, brightness, contrast, saturation and hue
     * grade whenever one of its inputs changes -- it desaturates as health
     * runs out -- and hands it to `SetUserColor` (`0x1808BDCD4`), which keeps
     * it here and publishes it through `ApplyUserColor`. TrackView's PostFX
     * track and the FlowGraph PostFX nodes write the same parameters, but
     * only while a cinematic or level script drives them.
     *
     * Four hide reasons can take the overlays away (`HideOverlays`, vtable
     * slot 20, `0x1808BCF3C`); while any is set the tint is published as
     * neutral instead, and `SetUserColor` stores without publishing.
     *
     * Layout from the constructor at `0x181863CFC`. The size is not asserted:
     * nothing past the tint is mapped.
     */
    class C_UIOverlay final
        : public C_UIFlashBase
        , public framework::I_SourceMonitorListener {
      public:
        ~C_UIOverlay() override = default;

        void unk_fn0000() override {}

        /** One of the eight values the tint is made of, in the order `SetUserColor` takes them. */
        enum class E_UserColor : std::uint8_t {
            Cyan,
            Magenta,
            Yellow,
            Darkness,
            Brightness,
            Contrast,
            Saturation,
            Hue,
        };

        static constexpr std::size_t kUserColorCount = 8;

        /** Null before the GUI module has built its pages. */
        static C_UIOverlay *GetInstance();

        /**
         * Publishes the tint (`0x1808BDD5C`): the stored values when `enabled`,
         * neutral ones otherwise, each as a forced write of its
         * `Global_User_*` parameter. The game calls it with
         * `!AreOverlaysHidden()`.
         */
        void ApplyUserColor(bool enabled);

        /** Whether any hide reason is set, in which case the game publishes a neutral tint. */
        bool AreOverlaysHidden() const {
            for (const std::uint8_t reason : m_hideReasons) {
                if (reason != 0) {
                    return true;
                }
            }
            return false;
        }

        /** The value `SetUserColor` last stored, published or not. */
        float GetUserColor(E_UserColor value) const {
            return m_userColor[static_cast<std::size_t>(value)];
        }

      private:
        // Constructor sub_181863CFC (0xF8 bytes from sub_181863BDC) binds
        // each of these to the cvar named beside it.
        float m_vignetteIntensity;         // 0x60, wh_ui_photomodeVignetteIntensity
        float m_vignetteRadius;            // 0x64, wh_ui_photomodeVignetteRadius
        std::int32_t m_logoType;           // 0x68, wh_ui_photomodeLogoType
        std::int32_t m_logoPosition;       // 0x6C, wh_ui_photomodeLogoPosition
        std::int32_t m_logoSize;           // 0x70, wh_ui_photomodeLogoSize
        std::int32_t m_screenShotFrameAspect; // 0x74, wh_ui_photomodeScreenShotFrameAspect
        std::int32_t m_grid;               // 0x78, wh_ui_photomodeGrid
        std::int32_t m_logoSizeSmall;      // 0x7C, wh_ui_photomodeLogoSizeSmall (default 128)
        std::int32_t m_logoSizeMedium;     // 0x80, wh_ui_photomodeLogoSizeMedium (default 200)
        std::int32_t m_logoSizeBig;        // 0x84, wh_ui_photomodeLogoSizeBig (default 256)
        std::uint32_t m_values0088[12];    // 0x88; filled from unk_183EF7DF0 by sub_181F96770
        std::array<std::uint8_t, 4> m_hideReasons; // 0xB8, one byte per reason HideOverlays takes
        std::uint32_t m_values00BC[2];     // 0xBC; ctor writes a zero qword

        // Cyan, magenta, yellow, darkness at 0xC4-0xD0 (0 from the
        // constructor), brightness, contrast, saturation at 0xD4-0xDC (1),
        // hue at 0xE0 (0) -- the order ApplyUserColor reads them in.
        std::array<float, kUserColorCount> m_userColor; // 0xC4

        static void AssertLayout() {
            static_assert(offsetof(C_UIOverlay, m_vignetteIntensity) == 0x60, "C_UIOverlay::m_vignetteIntensity offset mismatch");
            static_assert(offsetof(C_UIOverlay, m_logoSizeBig) == 0x84, "C_UIOverlay::m_logoSizeBig offset mismatch");
            static_assert(offsetof(C_UIOverlay, m_values0088) == 0x88, "C_UIOverlay::m_values0088 offset mismatch");
            static_assert(offsetof(C_UIOverlay, m_hideReasons) == 0xB8, "C_UIOverlay::m_hideReasons offset mismatch");
            static_assert(offsetof(C_UIOverlay, m_values00BC) == 0xBC, "C_UIOverlay::m_values00BC offset mismatch");
            static_assert(offsetof(C_UIOverlay, m_userColor) == 0xC4, "C_UIOverlay::m_userColor offset mismatch");
        }
    };
} // namespace SDK::wh::guimodule
