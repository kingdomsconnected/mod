#include "i_cry_pak.h"

#include "s_system_global_environment.h"

#include "../../patterns.h"

#include <utils/hooking/hooking.h>

namespace SDK {
    ICryPak *ICryPak::GetInstance() { return SSystemGlobalEnvironment::Get().pCryPak; }

    bool ICryPak::MountArchive(const char *absolutePath) {
        // Explicit binding root, FLAGS_PATH_REAL, no memory-backed input or
        // native string output. The package remains mounted across UI reloads.
        return hook::this_call<bool>(gPatterns.CCryPak__OpenPackWithBindingRoot, this, GetGameFolder(), absolutePath, 0x10000u, nullptr, nullptr);
    }

    bool ICryPak::MountArchive(const char *bindRoot, const char *absolutePath, unsigned int flags) {
        return hook::this_call<bool>(gPatterns.CCryPak__OpenPackWithBindingRoot, this, bindRoot, absolutePath, flags, nullptr, nullptr);
    }

    bool ICryPak::UnmountArchive(const char *absolutePath) { return ClosePack(absolutePath, 0x10000u); }
} // namespace SDK
