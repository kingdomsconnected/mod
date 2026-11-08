#include "i_system.h"

#include "s_system_global_environment.h"

#include "../../patterns.h"

#include <utils/hooking/hooking.h>

namespace SDK {
    ISystem *ISystem::GetInstance() { return SSystemGlobalEnvironment::Get().pSystem; }

    void ISystem::Quit(std::string_view reason, unsigned int exitCode) {
        // Sixteen bytes, so the reason the native takes by value arrives by hidden pointer.
        hook::this_call<void>(gPatterns.CSystem__Quit, this, &reason, exitCode);
    }
} // namespace SDK
