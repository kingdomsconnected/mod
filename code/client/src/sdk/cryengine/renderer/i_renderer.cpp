#include "i_renderer.h"

#include "../system/s_system_global_environment.h"

#include "../../patterns.h"

#include <utils/hooking/hooking.h>

namespace SDK {
    IRenderer *IRenderer::GetInstance() {
        return SSystemGlobalEnvironment::Get().pRenderer;
    }

    bool IRenderer::ReloadFile(const char *path) {
        return hook::this_call<bool>(gPatterns.CRenderer__EF_ReloadFile, this, path);
    }
} // namespace SDK
