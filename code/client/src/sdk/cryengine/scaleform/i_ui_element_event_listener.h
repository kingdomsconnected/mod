#pragma once

#include "i_ui_element.h"
#include "s_ui_arguments.h"
#include "s_ui_event_desc.h"

namespace SDK {
    /** Native UI-element event listener with a destructor-last ABI. */
    class IUIElementEventListener {
      public:
        /** Result is null for FSCommand, stack-borrowed for ExternalInterface. */
        virtual void OnUIEvent(IUIElement *sender, const SUIEventDesc &event, const SUIArguments &arguments, SFlashVarValue *result) {} // slot 0
        /** Undeclared FSCommand/ExternalInterface event; no result pointer. */
        virtual void OnUIEventEx(IUIElement *sender, const char *command, const SUIArguments &arguments, void *userData) {} // slot 1
        /** The movie can deliver setup events before this notification. */
        virtual void OnInit(IUIElement *sender, IFlashPlayer *player) {} // slot 2

        /** Invalidate cached references now; native movie reset follows. */
        virtual void OnUnload(IUIElement *sender) {} // slot 3

        virtual void OnSetVisible(IUIElement *sender, bool visible) {} // slot 4
        /** New registry instance; its movie need not be initialized yet. */
        virtual void OnInstanceCreated(IUIElement *sender, IUIElement *instance) {} // slot 5

        /** The instance is detached; other intrusive owners may retain it. */
        virtual void OnInstanceDestroyed(IUIElement *sender, IUIElement *instance) {} // slot 6
        virtual ~IUIElementEventListener() = default; // slot 7
    };

    static_assert(sizeof(IUIElementEventListener) == 0x8, "IUIElementEventListener size mismatch");
} // namespace SDK
