#pragma once

#include "../c_wh_global_environment.h"
#include "../guimodule/c_ui_game_over.h"
#include "i_ui_game_over.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::playermodule {
    /**
     * What every game-over converges on, at `C_PlayerModule + 0x48`. The name
     * is descriptive: the class has no RTTI of its own.
     * Used by `RaiseGameOver` (0x182E1919C).
     *
     * The first field is a run-once latch nothing in the game clears, because
     * a game-over is only ever left by loading a level.
     */
    class C_GameOverSequence final {
      public:
        C_GameOverSequence()                                      = delete;
        ~C_GameOverSequence()                                     = delete;
        C_GameOverSequence(const C_GameOverSequence &)            = delete;
        C_GameOverSequence &operator=(const C_GameOverSequence &) = delete;

        /** Null until the player module has built its members. */
        static C_GameOverSequence *GetInstance() {
            C_GlobalEnvironment *environment = C_GlobalEnvironment::GetInstance();
            C_PlayerModule *playerModule     = environment != nullptr ? environment->GetPlayerModule() : nullptr;
            return playerModule != nullptr ? *reinterpret_cast<C_GameOverSequence **>(reinterpret_cast<std::byte *>(playerModule) + 0x48) : nullptr;
        }

        bool HasRaised() const {
            return m_raised != 0;
        }

        /** Lets the next death raise the screen again. */
        void AllowAnother() {
            m_raised = 0;
        }

        /**
         * The death screen, which publishes its `I_UIGameOver` sub-object here
         * from its constructor (0x180C3B9E4) -- hence the step back over the
         * 0x58. Null before the GUI module has built its pages.
         */
        guimodule::C_UIGameOver *GetScreen() const {
            return m_pScreen != nullptr ? reinterpret_cast<guimodule::C_UIGameOver *>(reinterpret_cast<std::byte *>(const_cast<I_UIGameOver *>(m_pScreen)) - 0x58) : nullptr;
        }

        /** The same screen, as the interface the sequence calls through. */
        I_UIGameOver *GetScreenInterface() const {
            return m_pScreen;
        }

      private:
        std::uint32_t m_raised; // 0x00
        std::byte m_padding0004[0x4];
        I_UIGameOver *m_pScreen; // 0x08
        std::byte m_opaque0010[0x10];

        static void AssertLayout() {
            static_assert(offsetof(C_GameOverSequence, m_pScreen) == 0x8, "C_GameOverSequence::m_pScreen offset mismatch");
        }
    };
} // namespace SDK::wh::playermodule
