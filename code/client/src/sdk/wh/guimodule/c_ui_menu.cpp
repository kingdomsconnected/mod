#include "c_ui_menu.h"

#include "../../patterns.h"

#include <utils/hooking/hooking.h>

#include <cstdint>

namespace SDK::wh::guimodule {
    const char *ToString(E_ButtonId::Type id) {
        return hook::call<const char *>(gPatterns.E_ButtonId__ToString, id);
    }

    const char *ToString(E_ConfirmationId::Type id) {
        return hook::call<const char *>(gPatterns.E_ConfirmationId__ToString, id);
    }

    void C_UIMenu::ShowConfirmationRaw(E_ConfirmationId::Type id, const char *heading, const char *confirmLabel, const char *cancelLabel) {
        const ScopedCryString function("AddConfirmation");
        const ScopedCryString headingText(heading);
        const ScopedCryString confirmText(confirmLabel);
        const ScopedCryString cancelText(cancelLabel);

        // The packer reads every argument through a const reference, so the
        // name pointer and the two numbers need storage of their own. The
        // numbers are the row's default and back answers.
        const char *idName        = ToString(id);
        const std::int32_t first  = 0;
        const std::int32_t second = 0;

        hook::this_call<void>(gPatterns.C_UIMenuBase__CallFlashAddConfirmation, this, &function.Get(), &idName, &headingText.Get(), &confirmText.Get(), &cancelText.Get(), &first, &second);
    }
} // namespace SDK::wh::guimodule
