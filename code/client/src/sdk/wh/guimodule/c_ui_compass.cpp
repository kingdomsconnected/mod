#include "c_ui_compass.h"

#include "../../patterns.h"

#include <utils/hooking/hooking.h>

#include <cstdint>

namespace SDK::wh::guimodule {
    C_UICompass *C_UICompass::GetInstance() {
        // The GUI module, at WHGlobalEnvironment +0xE8 -- the same pointer
        // C_UIHudMessages::GetInstance takes, and the lookup's only argument.
        const auto environment = *reinterpret_cast<std::uint8_t **>(gPatterns.WHGlobalEnvironment__Instance);
        if (environment == nullptr) {
            return nullptr;
        }

        auto *guiModule = *reinterpret_cast<void **>(environment + 0xE8);
        if (guiModule == nullptr) {
            return nullptr;
        }

        return hook::call<C_UICompass *>(gPatterns.GetUICompass, guiModule);
    }

    void C_UICompass::AddMark(CompassMarkRef &&mark) {
        if (mark.Get() == nullptr) {
            return;
        }

        // A shared_ptr by value, which x64 passes by address: the native
        // destroys the pair in place, so the handle is spent afterwards.
        hook::this_call<void>(gPatterns.C_UICompass__AddMark, this, mark.Slot());
        mark.Disown();
    }

    bool C_UICompass::RemoveMark(CompassMarkRef &&mark) {
        if (mark.Get() == nullptr) {
            return false;
        }

        const bool removed = hook::this_call<bool>(gPatterns.C_UICompass__RemoveMark, this, mark.Slot());
        mark.Disown();
        return removed;
    }

    void C_UICompass::RemoveAllMarks() {
        hook::this_call<void>(gPatterns.C_UICompass__RemoveAllMarks, this);
    }
} // namespace SDK::wh::guimodule
