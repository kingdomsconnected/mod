#pragma once

#include "../../cryengine/movie/i_movie_listener.h"
#include "../../types.h"
#include "../shared/c_signal.h"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace SDK::wh::guimodule {
    /** Public fader-controller interface at the primary C_FaderController base. */
    class I_FaderController {
      public:
        virtual ~I_FaderController() = default;
    };

    static_assert(sizeof(I_FaderController) == 0x8, "I_FaderController size mismatch");

    struct E_FaderState {
        enum Type : std::uint8_t {
            FadedIn   = 0,
            FadingOut = 1,
            FadedOut  = 2,
            FadingIn  = 3,
        };
    };

    /**
     * GUI-wide fader state controller.
     *
     * RTTI places I_FaderController at +0x00, IMovieListener at +0x08, and
     * std::enable_shared_from_this at +0x10. CryDeferrable's empty bases also
     * occupy +0x10 through empty-base optimization and add no storage.
     */
    class C_FaderController : public I_FaderController, public IMovieListener, public std::enable_shared_from_this<C_FaderController> {
      public:
        E_FaderState::Type GetState() const {
            return m_state;
        }

      private:
        // Constructor sub_180D26844; the cvar fields are bound to the cvars
        // named beside them.
        SDK::wh::shared::C_Signal m_signal0020; // 0x20; dispatch unk_1856679D8
        void *m_pOwner;                        // 0x30; the constructor's argument
        E_FaderState::Type m_state; // 0x38
        std::uint8_t m_value0039;              // 0x39; ctor 0 (word write with +0x38)
        std::uint8_t m_value003A;              // 0x3A; ctor 0
        std::byte m_padding003B[0x1];
        std::uint32_t m_values003C[2];         // 0x3C; ctor writes a zero qword
        std::uint32_t m_value0044;             // 0x44; ctor 0
        std::byte m_object0048[0x40];          // 0x48; sub_1806B5C70
        std::byte m_object0088[0x40];          // 0x88; sub_1806B5C70
        std::byte m_hashMap00C8[0x40];         // 0xC8; std::unordered_map (sub_18043C878)
        std::byte m_hashMap0108[0x40];         // 0x108; std::unordered_map (sub_18043C878)
        std::byte m_object0148[0x10];          // 0x148; sub_180D26214
        void *m_ptrs0158[3];                   // 0x158; ctor 0
        std::uint32_t m_value0170;             // 0x170; ctor 0
        std::byte m_padding0174[0x4];
        std::int64_t m_times0178[3];           // 0x178; ctor -100000
        void *m_ptr0190;                       // 0x190; ctor 0
        bool m_flag0198;                       // 0x198; ctor 0
        std::byte m_padding0199[0x3];
        std::int32_t m_debug;                  // 0x19C, wh_ui_FaderDebug
        std::int32_t m_profiler;               // 0x1A0, wh_ui_FaderProfiler
        std::int32_t m_suspend;                // 0x1A4, wh_ui_FaderSuspend
        std::int32_t m_autoDebug;              // 0x1A8, wh_ui_FaderAutoDebug
        float m_debugAlpha;                    // 0x1AC, wh_ui_FaderDebugAlpha
        float m_fadeOutDefaultDuration;        // 0x1B0, wh_ui_FaderFadeOutDefaultDuration
        float m_fadeInDefaultDuration;         // 0x1B4, wh_ui_FaderFadeInDefaultDuration
        float m_progressIndicatorTimerDuration; // 0x1B8, wh_ui_FaderProgressIndicatorTimerDuration
        float m_debugStuckDetectorTimerDuration; // 0x1BC, wh_ui_FaderDebugStuckDetectorTimerDuration
        float m_profilerSaveDuration;          // 0x1C0, wh_ui_FaderProfilerSaveDuration
        std::byte m_padding01C4[0x4];
        const char *m_profilerSavePath;        // 0x1C8, wh_ui_FaderProfilerSavePath
        std::byte m_object01D0[0x20];          // 0x1D0; sub_1808DD5C0(owner, "Placeholder")
        void *m_ptrs01F0[3];                   // 0x1F0; ctor 0
        CryStringT<char> m_name;               // 0x208; "fader"

        static void AssertLayout() {
            static_assert(offsetof(C_FaderController, m_signal0020) == 0x20, "C_FaderController::m_signal0020 offset mismatch");
            static_assert(offsetof(C_FaderController, m_state) == 0x38, "C_FaderController::m_state offset mismatch");
            static_assert(offsetof(C_FaderController, m_values003C) == 0x3C, "C_FaderController::m_values003C offset mismatch");
            static_assert(offsetof(C_FaderController, m_object0148) == 0x148, "C_FaderController::m_object0148 offset mismatch");
            static_assert(offsetof(C_FaderController, m_times0178) == 0x178, "C_FaderController::m_times0178 offset mismatch");
            static_assert(offsetof(C_FaderController, m_debug) == 0x19C, "C_FaderController::m_debug offset mismatch");
            static_assert(offsetof(C_FaderController, m_profilerSavePath) == 0x1C8, "C_FaderController::m_profilerSavePath offset mismatch");
            static_assert(offsetof(C_FaderController, m_name) == 0x208, "C_FaderController::m_name offset mismatch");
        }
    };

    static_assert(sizeof(C_FaderController) == 0x210, "C_FaderController size mismatch");
} // namespace SDK::wh::guimodule
