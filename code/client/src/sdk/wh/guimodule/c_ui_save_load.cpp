#include "c_ui_save_load.h"

#include "../../patterns.h"

#include <utils/hooking/hooking.h>

namespace SDK::wh::guimodule {
    void C_UISaveLoad::InvokeStartNewGameRaw(std::uint32_t startMode, const char *modList, const char *levelName) {
        hook::this_call<void>(gPatterns.C_UISaveLoad__StartNewGame, this, startMode, modList, levelName);
    }

    void C_UISaveLoad::InvokeAddLoadButton(std::int32_t rowId, std::int32_t mode, const CryStringT<char> &label, const CryStringT<char> &tooltip, bool enabled) {
        const ScopedCryString function("AddLoadButton");

        // The packer reads each argument through a const reference, so every
        // scalar needs storage of its own.
        const std::int32_t rowIdStorage = rowId;
        const std::int32_t modeStorage  = mode;
        const bool enabledStorage       = enabled;

        hook::this_call<void>(gPatterns.C_UISaveLoad__CallFlash5, this, &function.Get(), &rowIdStorage, &modeStorage, &label, &tooltip, &enabledStorage);
    }
} // namespace SDK::wh::guimodule
