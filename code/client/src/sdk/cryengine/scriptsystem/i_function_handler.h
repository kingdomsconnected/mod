#pragma once

#include "../../types.h"
#include "i_script_system.h"

#include <cstddef>

namespace SDK {
    /**
     * Lua-to-native call frame supplied to every C_ScriptBind* handler.
     *
     * GetThis at +0x10 is proven independently by the AnimDoor and LockBase
     * resolvers: both read the full qword ScriptHandle stamped into the bound
     * table's `__this`. Callers must validate before narrowing a handle known
     * to carry an EntityId. The remaining entries stay opaque until needed.
     */
    class IFunctionHandler {
      public:
        virtual ~IFunctionHandler()                     = default; // +0x00
        virtual IScriptSystem *GetIScriptSystem() const = 0;       // +0x08
        virtual ScriptHandle GetThis() const            = 0;       // +0x10
        virtual void Opaque03()                         = 0;       // +0x18, GetSelfAny in stock CryEngine
        virtual void Opaque04()                         = 0;       // +0x20, GetFuncName
        virtual void Opaque05()                         = 0;       // +0x28, GetParamCount
        virtual void Opaque06()                         = 0;       // +0x30, GetParamType
        virtual void Opaque07()                         = 0;       // +0x38, GetParamAny
        virtual void Opaque08()                         = 0;       // +0x40, the three-value EndFunctionAny
        virtual void Opaque09()                         = 0;       // +0x48, the two-value EndFunctionAny

        /**
         * Returns one value to Lua, and the count to return from the handler.
         *
         * Stock CryEngine declares three `EndFunctionAny` overloads with the
         * one-value form first; MSVC emits same-name virtuals in reverse, so it
         * lands last, at +0x50. The binds' own bool return helper (0x180711B28)
         * calls exactly this slot with a Boolean `ScriptAnyValue`.
         */
        virtual int EndFunctionAny(const ScriptAnyValue &value) = 0; // +0x50
    };

    static_assert(sizeof(IFunctionHandler) == 0x8, "IFunctionHandler size mismatch");
} // namespace SDK
