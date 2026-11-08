#pragma once

#include "../../types.h"
#include "e_ui_apse_view.h"
#include "s_ui_apse_show_params.h"

namespace SDK::wh::framework {
    /**
     * The pause screen's interface as the rest of the game sees it: what the
     * global environment holds at +0x1B0, installed by `C_UIApse::Init`
     * (0x180BC51BC). `C_UIApse` implements it at +0x10 with the table at
     * 0x183ED91B0; only the slots through the one the mod calls are named.
     */
    class I_UIApse {
      public:
        virtual void unk_fn0000() = 0;

        /**
         * Slot 1 (0x181F4AA00), what every `open_apse_*` key calls: opens the
         * pause screen on `view`. Refused -- false, nothing shown -- unless
         * the screen is idle and built, and the UI-state gate allows it. On
         * success it blocks `E_UIState::Apse` until the close finishes, and
         * the camera moves to the view by itself once the screen is up.
         */
        virtual bool Show(E_UIApseView::Type view, S_UIApseShowParams *params) = 0;

        /**
         * Slot 2 (0x180C44F48), what `apse:close` calls: closes the pause
         * screen. `immediate` tears it down now; false fades it out first.
         * Does nothing while it is not up.
         */
        virtual void Hide(bool immediate) = 0;
        virtual void unk_fn0003()         = 0;
        virtual void unk_fn0004()         = 0;
        virtual void unk_fn0005()         = 0;
        virtual void unk_fn0006()         = 0;
        virtual void unk_fn0007()         = 0;
        virtual void unk_fn0008()         = 0;
        virtual void unk_fn0009()         = 0;
        virtual void unk_fn0010()         = 0;
        virtual void unk_fn0011()         = 0;

        /**
         * Slot 12 (0x1811A07D0), as `C_Blacksmithing::AcceptSelectedRecipe`
         * calls it: the id of the recipe selected on the smithing screen,
         * parsed out of its `BlacksmithRecipe:<id>` UI key. The result is
         * constructed into `result`, which must not already hold a string.
         */
        virtual SDK::CryStringT<char> *GetSelectedBlacksmithRecipe(SDK::CryStringT<char> *result) = 0;

      protected:
        ~I_UIApse() = default;
    };
} // namespace SDK::wh::framework
