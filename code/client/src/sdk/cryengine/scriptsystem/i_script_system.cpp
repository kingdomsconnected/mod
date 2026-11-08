#include "i_script_system.h"

#include "../system/s_system_global_environment.h"

#include "../../patterns.h"

#include <utils/hooking/hooking.h>

namespace SDK {
    IScriptSystem *IScriptSystem::GetInstance() {
        return SSystemGlobalEnvironment::Get().pScriptSystem;
    }

    bool IScriptSystem::PopStackValue(ScriptAnyValue &value) {
        return hook::this_call<bool>(gPatterns.CScriptSystem__PopStackValue, this, &value);
    }
} // namespace SDK
