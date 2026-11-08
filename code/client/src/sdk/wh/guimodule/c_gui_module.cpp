#include "c_gui_module.h"

#include "c_ui_apse.h"
#include "c_ui_loading_screen.h"
#include "c_ui_menu.h"
#include "c_ui_overlay.h"

#include "../../patterns.h"

namespace SDK::wh::guimodule {
    C_UIBase *C_GUIModule::FindPage(std::uint64_t vtable) const {
        for (const std::shared_ptr<C_UIBase> &page : m_pages) {
            C_UIBase *candidate = page.get();
            if (candidate != nullptr && *reinterpret_cast<const std::uint64_t *>(candidate) == vtable) {
                return candidate;
            }
        }
        return nullptr;
    }

    C_UIMenu *C_GUIModule::GetMenu() const {
        return reinterpret_cast<C_UIMenu *>(FindPage(gPatterns.C_UIMenu__VTable));
    }

    C_UILoadingScreen *C_GUIModule::GetLoadingScreen() const {
        return reinterpret_cast<C_UILoadingScreen *>(FindPage(gPatterns.C_UILoadingScreen__VTable));
    }

    C_UIApse *C_GUIModule::GetApse() const {
        return reinterpret_cast<C_UIApse *>(FindPage(gPatterns.C_UIApse__VTable));
    }

    C_UIOverlay *C_GUIModule::GetOverlay() const {
        return reinterpret_cast<C_UIOverlay *>(FindPage(gPatterns.C_UIOverlay__VTable));
    }
} // namespace SDK::wh::guimodule
