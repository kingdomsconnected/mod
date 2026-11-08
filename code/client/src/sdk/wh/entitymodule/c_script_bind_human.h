#pragma once

#include "../../cryengine/scriptsystem/i_function_handler.h"
#include "c_script_bind_base.h"

#include <cstddef>

namespace SDK::wh::entitymodule {
    /**
     * Native Lua surface installed as `entity.human`.
     *
     * Allocated at 0x78 by the registrar at 0x180E30204 and built by the
     * constructor at 0x18128B9DC, which registers every Lua name against a
     * thunk into this table. The table at 0x183ECB3D0 has 44 slots; the ones
     * named here are proven against their bodies and the rest stay opaque.
     */
    class C_ScriptBindHuman {
      public:
        virtual ~C_ScriptBindHuman() = default; // slot 0
        virtual void unk_fn0001()    = 0;       // CScriptableBase::Init
        virtual void unk_fn0002()    = 0;       // CScriptableBase::Done
        virtual void unk_fn0003()    = 0;
        virtual void unk_fn0004()    = 0;

        /** Slot 5 (0x182AC12D4), Lua `human:RequestDialog(target, alias, bool, bool)`. */
        virtual int RequestDialog(SDK::IFunctionHandler *handler) = 0;

        virtual void unk_fn0006() = 0;
        virtual void unk_fn0007() = 0;
        virtual void unk_fn0008() = 0;
        virtual void unk_fn0009() = 0;
        virtual void unk_fn0010() = 0;
        virtual void unk_fn0011() = 0;
        virtual void unk_fn0012() = 0;
        virtual void unk_fn0013() = 0;
        virtual void unk_fn0014() = 0;
        virtual void unk_fn0015() = 0;
        virtual void unk_fn0016() = 0;
        virtual void unk_fn0017() = 0;
        virtual void unk_fn0018() = 0;
        virtual void unk_fn0019() = 0;
        virtual void unk_fn0020() = 0;
        virtual void unk_fn0021() = 0;
        virtual void unk_fn0022() = 0;
        virtual void unk_fn0023() = 0;
        virtual void unk_fn0024() = 0;
        virtual void unk_fn0025() = 0;
        virtual void unk_fn0026() = 0;
        virtual void unk_fn0027() = 0;
        virtual void unk_fn0028() = 0;
        virtual void unk_fn0029() = 0;
        virtual void unk_fn0030() = 0;
        virtual void unk_fn0031() = 0;
        virtual void unk_fn0032() = 0;
        virtual void unk_fn0033() = 0;
        virtual void unk_fn0034() = 0;
        virtual void unk_fn0035() = 0;
        virtual void unk_fn0036() = 0;
        virtual void unk_fn0037() = 0;
        virtual void unk_fn0038() = 0;
        virtual void unk_fn0039() = 0;

        /**
         * Slot 40 (0x1804CECE8), Lua `human:CanBeRobbed()` on the victim: true
         * unless the pickpocket state at `C_Human+0xA08`, refreshed first,
         * reads 2. `BasicAIActions:GetActions` offers the Rob entry on it.
         */
        virtual int CanBeRobbed(SDK::IFunctionHandler *handler) = 0;

        virtual void unk_fn0041() = 0;
        virtual void unk_fn0042() = 0;
        virtual void unk_fn0043() = 0;

      private:
        ScriptBindPrefixLayout m_prefix; // 0x08
        void *m_pScriptService0070; // 0x70, ctor 0x18128B9DC: pSystem->vfn[89]()->vfn[16]()

        static void AssertLayout() {
            static_assert(offsetof(C_ScriptBindHuman, m_prefix) == 0x08, "C_ScriptBindHuman::m_prefix offset mismatch");
            static_assert(offsetof(C_ScriptBindHuman, m_pScriptService0070) == 0x70, "C_ScriptBindHuman::m_pScriptService0070 offset mismatch");
        }
    };

    static_assert(sizeof(C_ScriptBindHuman) == 0x78, "C_ScriptBindHuman size mismatch");
} // namespace SDK::wh::entitymodule
