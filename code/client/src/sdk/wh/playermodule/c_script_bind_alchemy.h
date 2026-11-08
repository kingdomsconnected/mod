#pragma once

#include "../../cryengine/scriptsystem/c_scriptable_base.h"
#include "c_player_module.h"

#include <cstddef>

namespace SDK::wh::playermodule {
    /**
     * RTTI vtable 0x183ACEC68. Factory 0x180EF7184 allocates 0x68;
     * constructor 0x180EF737C initializes CScriptableBase and stores its
     * minigame owner at 0x60. The owner's concrete type is not yet proven.
     * ScriptHandle arguments to StartAlchemy occupy full 64-bit registers;
     * the bound handler later narrows them to EntityId.
     */
    class C_ScriptBind_Alchemy: public SDK::CScriptableBase {
      private:
        C_PlayerModule *m_pPlayerModule; // 0x60; ctor 0x180EF737C stores its argument

        static void AssertLayout() {
            static_assert(offsetof(C_ScriptBind_Alchemy, m_pPlayerModule) == 0x60, "C_ScriptBind_Alchemy::m_pPlayerModule offset mismatch");
        }
    };

    static_assert(sizeof(C_ScriptBind_Alchemy) == 0x68);
} // namespace SDK::wh::playermodule
