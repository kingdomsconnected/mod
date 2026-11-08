#pragma once

#include "i_ui_element.h"

namespace SDK {
    /** Registry interface proven against CFlashUI table 0x18404A7D8. */
    struct IFlashUI {
        virtual ~IFlashUI()       = default; // 0
        virtual void unk_fn0001() = 0;
        virtual void unk_fn0002() = 0;
        virtual void unk_fn0003() = 0;
        virtual void unk_fn0004() = 0;
        virtual void unk_fn0005() = 0;
        virtual void unk_fn0006() = 0;
        virtual void unk_fn0007() = 0;
        virtual void unk_fn0008() = 0;
        virtual void unk_fn0009() = 0;
        virtual void unk_fn0010() = 0;

        /** Slot 11. XML must be reachable through CryPak. */
        virtual bool LoadElementsFromFile(const char *path) = 0;

        virtual void unk_fn0012() = 0;

        /**
         * Slot 13 (0x18056406C). Writes an **owning** handle into `out` and
         * returns it; `Release` what comes back. Out of range leaves null.
         * Indices are dense and `GetUIElementCount` bounds them.
         */
        virtual IUIElement **GetUIElementAt(IUIElement **out, int index) = 0;

        virtual void unk_fn0014() = 0; // Native intrusive return, not a raw pointer.

        /** Slot 15 (0x180564170), the registry's own element array length. */
        virtual int GetUIElementCount() const = 0;

        virtual void unk_fn0016() = 0;
        virtual void unk_fn0017() = 0;

        /**
         * Slot 18. Accepts Element@ID.ui or a bare element name (ID zero).
         * Returns a borrowed pointer; AddRef before retaining it.
         */
        virtual IUIElement *GetUIElementByInstanceStr(const char *instance) const = 0;

        /** The registry can legitimately be null before native UI startup. */
        static IFlashUI *GetInstance();
    };

    static_assert(sizeof(IFlashUI) == 0x8, "IFlashUI size mismatch");
} // namespace SDK
