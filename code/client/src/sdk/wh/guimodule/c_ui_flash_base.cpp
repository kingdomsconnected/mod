#include "c_ui_flash_base.h"

#include "../../patterns.h"

#include <utils/safe_win32.h>

#include <utils/hooking/hooking.h>

namespace SDK::wh::guimodule {
    bool C_UIFlashBase::SetFlashArray(const CryStringT<char> &name, const StdVector<NativeSharedRef<C_UIFlashObject>> &rows) {
        return hook::this_call<bool>(gPatterns.C_UIFlashBase__SetFlashArray, this, &name, &rows);
    }

    void C_UIFlashBase::SetFlashArguments(const CryStringT<char> &name, const SUIArguments &arguments) {
        hook::this_call<void>(gPatterns.C_UIFlashBase__SetFlashArguments, this, &name, &arguments);
    }

    bool C_UIFlashBase::CallFlashFunction(const CryStringT<char> &name) {
        return hook::this_call<bool>(gPatterns.C_UIFlashBase__CallFlashFunction, this, &name);
    }

    bool C_UIFlashBase::CallFlashFunction(const CryStringT<char> &name, const float &first, const int &second, const int &third) {
        return hook::this_call<bool>(gPatterns.C_UIFlashBase__CallFlashFunctionFloatIntInt, this, &name, &first, &second, &third);
    }
} // namespace SDK::wh::guimodule
