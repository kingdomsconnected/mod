#pragma once

#include "navigation/c_navigation.h"

#include <cstddef>

namespace SDK::wh::xgenaimodule {
    /**
     * The XGenAI game module, `C_GlobalEnvironment::m_pXGenAIModule`.
     *
     * Built by `0x181889578`. Its Init (vtable slot 2, `0x1807D2BB4`) is
     * called on that same pointer before `0x180C9E073` stores it in the
     * registry, and writes it to `g_pXGenAIModule` (0x185496248), the global
     * the game's own movement calls read.
     *
     * Only the navigation is claimed: that Init builds it (`0x180D39C78`)
     * into `+0x78`, and `0x1807D7A7C` reads it from there. Never constructed
     * by the mod; only the prefix is asserted.
     */
    class C_XGenAIModule {
      public:
        navigation::C_Navigation *GetNavigation() const {
            return m_pNavigation;
        }

      private:
        std::byte m_opaque0000[0x78];
        navigation::C_Navigation *m_pNavigation; // 0x78

        static void AssertLayout() {
            static_assert(offsetof(C_XGenAIModule, m_pNavigation) == 0x78, "C_XGenAIModule::m_pNavigation offset mismatch");
        }
    };
} // namespace SDK::wh::xgenaimodule
