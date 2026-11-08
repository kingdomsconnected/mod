#pragma once

#include "../../cryengine/scaleform/c_flash_ui_element.h"
#include "../../cryengine/scaleform/i_ui_element_event_listener.h"
#include "../../cryengine/scaleform/s_ui_arguments.h"
#include "../../types.h"
#include "c_ui_base.h"
#include "c_ui_flash_object.h"
#include "native_shared_ref.h"

#include <cstddef>

namespace SDK::wh::guimodule {
    /** Flash-backed native UI object and UI-element event listener. */
    class C_UIFlashBase : public C_UIBase, public SDK::IUIElementEventListener {
      public:
        ~C_UIFlashBase() override = default;

        void unk_fn0001() override {}
        void unk_fn0002() override {}
        void unk_fn0003() override {}
        void unk_fn0004() override {}
        void unk_fn0005() override {}
        void unk_fn0006() override {}
        void unk_fn0007() override {}
        void unk_fn0008() override {}
        void unk_fn0009() override {}

        virtual void unk_fn0010() {}
        virtual void unk_fn0011() {}
        virtual void unk_fn0012() {}
        virtual void unk_fn0013() {}

        void OnUIEvent(IUIElement *sender, const SUIEventDesc &event, const SUIArguments &arguments, SFlashVarValue *result) override {}

        /**
         * The FlashUI element this object drives, or null before it has been
         * resolved. The event dispatcher rejects events arriving from any
         * other element, which is what pins this offset.
         */
        CFlashUIElement *GetElement() const { return m_pElement; }

        /** The loaded movie, or null before the element has been shown. */
        IFlashPlayer *GetFlashPlayer() const { return m_pElement != nullptr ? m_pElement->GetLoadedFlashPlayer() : nullptr; }

        /**
         * Writes the declared array `name` from a table of rows, the way every
         * data list the game shows reaches its movie (0x1807F7114): the count,
         * then each row's reflected properties. Reads `rows` and copies the
         * references it needs, so a view over a buffer the mod owns is fine.
         */
        bool SetFlashArray(const CryStringT<char> &name, const StdVector<NativeSharedRef<C_UIFlashObject>> &rows);

        /** Writes the declared array `name` from plain arguments (0x1807F6FA4), through IUIElement::SetArray. */
        void SetFlashArguments(const CryStringT<char> &name, const SUIArguments &arguments);

        /**
         * Invokes the declared function `name` with no arguments (0x180566BCC).
         * False when the element is not resolved or the movie says so. What a
         * function reads is whatever array was set before it.
         */
        bool CallFlashFunction(const CryStringT<char> &name);

        /**
         * Invokes the declared function `name` with `(float, int, int)`: the
         * `CallFlashFunction<float,int,int>` instantiation (0x182BB0DBC), which
         * is what `C_UIBook::SetFullBook` calls `fc_setFullBook` through. Each
         * value is taken by reference, as the template does.
         */
        bool CallFlashFunction(const CryStringT<char> &name, const float &first, const int &second, const int &third);

        /** The element's name as the object was constructed with it. */
        const char *GetElementName() const {
            return m_pElementName;
        }

      private:
        void *m_pState0018;
        void *m_pState0020;
        void *m_pState0028;
        void *m_pEventRecordsBegin;    // 0x30, 0x40-byte elements
        void *m_pEventRecordsEnd;      // 0x38
        void *m_pEventRecordsCapacity; // 0x40
        CFlashUIElement *m_pElement;   // 0x48
        const char *m_pElementName;    // 0x50

        static void AssertLayout() {
            static_assert(offsetof(C_UIFlashBase, m_pElement) == 0x48, "C_UIFlashBase::m_pElement offset mismatch");
            static_assert(offsetof(C_UIFlashBase, m_pElementName) == 0x50, "C_UIFlashBase::m_pElementName offset mismatch");
        }
    };

    static_assert(sizeof(C_UIFlashBase) == 0x58, "C_UIFlashBase size mismatch");
} // namespace SDK::wh::guimodule
