#include "c_ui_hud_mask.h"

#include "../../patterns.h"

namespace SDK::wh::guimodule {
    C_UIHudMask *C_UIHudMask::GetInstance() {
        return *reinterpret_cast<C_UIHudMask **>(gPatterns.C_UIHudMask__Instance);
    }
} // namespace SDK::wh::guimodule
