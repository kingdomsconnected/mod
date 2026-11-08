#pragma once

#include "../patterns.h"
#include "shared/c_signal.h"

#include <cstddef>
#include <cstdint>

#include <utils/hooking/hooking.h>

namespace SDK::wh {
    /**
     * The game's session started/ended latch.
     *
     * Not an RTTI class: it is embedded at +0xB8 of the global Warhorse
     * object C_WHGameModules[0] points at (built by 0x1807D0AE0, held in
     * 0x185168B38), so this name describes what it does rather than naming a
     * type the binary declares, and the namespace follows that owner. The
     * latch is a signal followed by two bytes.
     *
     * Start (0x180FC41E8) sets the byte and broadcasts module message 52 the
     * first time; End (0x180FC40F0) clears it and broadcasts 77. A load
     * completion starts it -- the event-21 lambda, the level switch's end and
     * C_NewGameHelper's readiness observer -- and ReturnToMainMenuState ends
     * it.
     */
    struct S_GameSessionLatch {
        S_GameSessionLatch()                                      = delete;
        S_GameSessionLatch(const S_GameSessionLatch &)            = delete;
        S_GameSessionLatch &operator=(const S_GameSessionLatch &) = delete;

        bool IsStarted() const {
            return m_started;
        }

        /** Ends the session the way ReturnToMainMenuState does: message 77 when it was started. */
        void End() {
            hook::this_call<void>(gPatterns.GameSessionLatch__End, this);
        }

      private:
        SDK::wh::shared::C_Signal m_signal; // 0x00, Start and End broadcast their module message through it
        bool m_started;                     // 0x10
        bool m_everStarted;                 // 0x11, Start writes the word 0x0101 over both bytes; End clears only m_started

        static void AssertLayout() {
            static_assert(offsetof(S_GameSessionLatch, m_started) == 0x10, "S_GameSessionLatch::m_started offset mismatch");
            static_assert(offsetof(S_GameSessionLatch, m_everStarted) == 0x11, "S_GameSessionLatch::m_everStarted offset mismatch");
        }
    };
} // namespace SDK::wh
