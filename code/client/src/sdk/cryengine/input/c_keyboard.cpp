#include "c_keyboard.h"

#include "../../patterns.h"

#include <utils/hooking/hooking.h>

namespace SDK {
    void CKeyboard::ChangeLanguage(void *layout) {
        hook::this_call<void>(gPatterns.CKeyboard__ChangeLanguage, this, layout);
    }
} // namespace SDK
