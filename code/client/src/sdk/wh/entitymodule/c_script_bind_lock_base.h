#pragma once

#include "../../cryengine/scriptsystem/i_function_handler.h"
#include "c_lock_base.h"
#include "c_script_bind_base.h"

#include <cstddef>

namespace SDK::wh::entitymodule {
    /**
     * The Lua surface every lockable entity carries as `entity.lockBase`.
     *
     * A single instance with no getter, so this type describes the vtable
     * rather than being looked up: the useful operations are detours.
     *
     * `AttachTo` builds a table, stamps `__this` with the entity id, delegates
     * it to the bind's method table and stores it under `lockBase`; every
     * handler resolves its `C_LockBase` back out of that.
     *
     * The three Report* slots are the shipped Lua telling C++ that the lock
     * changed, each broadcasting the matching `C_LockBase` signal. Driving one
     * from the other direction is `C_LockBase::SetLocked`.
     */
    class C_ScriptBindLockBase {
      public:
        virtual ~C_ScriptBindLockBase() = default; // slot 0
        virtual void unk_fn0001() = 0;             // slot 1, shared initializer
        virtual void unk_fn0002() = 0;             // slot 2
        virtual void unk_fn0003() = 0;             // slot 3, empty

        /** Installs `entity.lockBase` for this extension. */
        virtual void AttachTo(C_LockBase *lockBase) = 0; // slot 4

        /** Lua `lockBase:ReportLocked()`; broadcasts C_LockBase +0x40. */
        virtual int ReportLocked(IFunctionHandler *handler) = 0; // slot 5

        /** Lua `lockBase:ReportUnlocked()`; broadcasts C_LockBase +0x50. */
        virtual int ReportUnlocked(IFunctionHandler *handler) = 0; // slot 6

        /** Lua `lockBase:ReportLockpicked()`; broadcasts C_LockBase +0x60. */
        virtual int ReportLockpicked(IFunctionHandler *handler) = 0; // slot 7

        /**
         * Lua `lockBase:CanUnlock(guidString)`.
         *
         * Parses the string into a CryGUID and forwards it to
         * `C_LockBase::CanUnlock`, so the key class a lock accepts is chosen
         * by the entity's own script, not by C++.
         */
        virtual int CanUnlock(IFunctionHandler *handler, const char *keyItemClassGuid) = 0; // slot 8

      private:
        ScriptBindPrefixLayout m_prefix; // 0x08

        static void AssertLayout() {
            static_assert(offsetof(C_ScriptBindLockBase, m_prefix) == 0x08, "C_ScriptBindLockBase::m_prefix offset mismatch");
        }
    };

    static_assert(sizeof(C_ScriptBindLockBase) == 0x70, "C_ScriptBindLockBase size mismatch");
} // namespace SDK::wh::entitymodule
