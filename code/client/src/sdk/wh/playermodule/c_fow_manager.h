#pragma once

#include "i_fog_of_war.h"

#include <cstddef>

namespace SDK::wh::playermodule {
    /**
     * Owns one fog of war per level and reveals the current one around the
     * player: `C_FOWManager::Update` (0x180534AF8) calls `RevealAt`
     * (0x180B57FA8), which is `GetFogOfWar()->Reveal(position, ...)`.
     * C_PlayerModule +0x38; constructor 0x180BE9FB4.
     */
    class C_FOWManager final {
      public:
        C_FOWManager()                                = delete;
        ~C_FOWManager()                               = delete;
        C_FOWManager(const C_FOWManager &)            = delete;
        C_FOWManager &operator=(const C_FOWManager &) = delete;

        /** Never null: between levels it is the manager's own embedded C_NullFogOfWar, whose slots are all no-ops. */
        I_FogOfWar *GetFogOfWar() const {
            return m_pFogOfWar;
        }

      private:
        std::byte m_opaque0000[0x18];
        I_FogOfWar *m_pFogOfWar; // 0x18
        std::byte m_opaque0020[0x88];

        static void AssertLayout() {
            static_assert(offsetof(C_FOWManager, m_pFogOfWar) == 0x18, "C_FOWManager::m_pFogOfWar offset mismatch");
        }
    };

    static_assert(sizeof(C_FOWManager) == 0xA8, "C_FOWManager size mismatch");
} // namespace SDK::wh::playermodule
