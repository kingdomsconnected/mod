#pragma once

#include "s_flash_var_value.h"

namespace SDK {
    /** Native callback subobjects; destructors follow their handler slot. */
    struct IFSCommandHandler {
        virtual void HandleFSCommand(const char *command, const char *arguments, void *userData) = 0;

      protected:
        virtual ~IFSCommandHandler() = default;
    };

    struct IExternalInterfaceHandler {
        virtual void HandleExternalInterfaceCall(const char *method, const SFlashVarValue *arguments, int count, void *userData, SFlashVarValue *result) = 0;

      protected:
        virtual ~IExternalInterfaceHandler() = default;
    };

    static_assert(sizeof(IFSCommandHandler) == 0x8, "IFSCommandHandler size mismatch");
    static_assert(sizeof(IExternalInterfaceHandler) == 0x8, "IExternalInterfaceHandler size mismatch");
} // namespace SDK
