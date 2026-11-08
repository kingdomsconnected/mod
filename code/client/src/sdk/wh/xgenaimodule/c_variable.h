#pragma once

#include "../../cryengine/scriptsystem/i_script_system.h"
#include "../../types.h"

namespace SDK::wh::xgenaimodule {
    /**
     * One named variable of a brain, as the behaviour trees and
     * `XGenAIModule.GetBrainVariable`/`SetBrainVariable` see it.
     *
     * The base table is 0x183A3FFF0; slots 4 and 5 are pure there and the
     * concrete kinds -- `C_SingleVariable`, `C_AssociativeArrayTreeVariable`
     * and the rest -- implement them. Both script binds call exactly these two
     * slots on what `C_VariableIndex::FindVariable` returns.
     */
    class C_Variable {
      public:
        virtual void unk_fn0000() = 0;
        virtual void unk_fn0001() = 0;
        virtual void unk_fn0002() = 0;
        virtual void unk_fn0003() = 0;

        /**
         * The value as a script value, written into `out` and returned. A
         * table result carries a reference the caller owns and must release
         * (`C_AssociativeArrayTreeVariable::GetValue` 0x181138D18 AddRefs it).
         */
        virtual ScriptAnyValue &GetValue(ScriptAnyValue &out) const = 0; // slot 4, +0x20

        /**
         * Replaces the value. On a struct-like variable this **erases every
         * key the table does not carry** (`C_AssociativeArrayTreeVariable::
         * SetValue` 0x180A28460), so a field is changed by reading the whole
         * table, editing it and writing it back.
         */
        virtual bool SetValue(const ScriptAnyValue &value) = 0; // slot 5, +0x28
    };

    /**
     * A brain's variables by name. Embedded at `C_AIBrain+0xC8`, whose base
     * constructor (0x180418D10) installs this table (0x183A3FB58).
     */
    class C_VariableIndex {
      public:
        virtual void unk_fn0000() = 0;
        virtual void unk_fn0001() = 0;

        /**
         * Interns `name` and looks the variable up. The script binds pass
         * `true`; the flag's meaning beyond that is not modeled.
         */
        virtual C_Variable *FindVariable(const CryStringT<char> &name, bool flag) = 0; // slot 2, +0x10 (0x1805C71F0)
    };
} // namespace SDK::wh::xgenaimodule
