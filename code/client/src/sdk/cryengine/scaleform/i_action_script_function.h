#pragma once

#include "i_flash_variable_object.h"

#include <cstddef>

namespace SDK {
    /** Native function callback. Registration borrows this object. */
    struct IActionScriptFunction {
        struct Params {
            // Identity only: compare against the owner's known movie handle.
            // Do not dereference; the callback need not reacquire its player.
            void *pFromPlayer;
            void *pUserData;
            const IFlashVariableObject *pThis;
            const IFlashVariableObject **pArgs;
            unsigned int numArgs;
            std::byte m_opaque0024[4];

          private:
            static void AssertLayout() {
                static_assert(offsetof(Params, pFromPlayer) == 0, "Params::pFromPlayer offset mismatch");
                static_assert(offsetof(Params, pUserData) == 8, "Params::pUserData offset mismatch");
                static_assert(offsetof(Params, pThis) == 0x10, "Params::pThis offset mismatch");
                static_assert(offsetof(Params, pArgs) == 0x18, "Params::pArgs offset mismatch");
                static_assert(offsetof(Params, numArgs) == 0x20, "Params::numArgs offset mismatch");
            }
        };

        struct IReturnValue {
            virtual void SetObject(const IFlashVariableObject *value) = 0; // 0
            /** Keep managed=true when returning temporary string storage. */
            virtual void Set(const SFlashVarValue &value, bool createManagedValue = true) = 0; // 1

          protected:
            virtual ~IReturnValue() = default; // 2
        };

        /** this/args/return are borrowed until Call returns; Clone to retain. */
        virtual void Call(const Params &params, IReturnValue *returnValue) = 0;

      protected:
        virtual ~IActionScriptFunction() = default;
    };
    static_assert(sizeof(IActionScriptFunction::Params) == 0x28, "IActionScriptFunction::Params size mismatch");
    static_assert(sizeof(IActionScriptFunction::IReturnValue) == 8, "IActionScriptFunction::IReturnValue size mismatch");
    static_assert(sizeof(IActionScriptFunction) == 8, "IActionScriptFunction size mismatch");
} // namespace SDK
