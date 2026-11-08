#pragma once

#include "i_action_map_action.h"

namespace SDK {
    /**
     * A walk over one action map's actions, from `IActionMap::CreateActionIterator`.
     *
     * The local `CActionInfo` class, vtable 0x183A6B320. Reference counted:
     * the map hands it out already holding one reference, and `Release`
     * deletes it through its own destructor, inside the game's heap.
     */
    class IActionMapActionIterator {
      public:
        virtual ~IActionMapActionIterator() = default;

        /** The next action, or null at the end. The same object each call, advanced. */
        virtual const IActionMapAction *Next() = 0; // +0x08, 0x180A131EC

        virtual void AddRef()  = 0; // +0x10
        virtual void Release() = 0; // +0x18, deletes at zero
    };

    static_assert(sizeof(IActionMapActionIterator) == 0x8, "IActionMapActionIterator interface size mismatch");
} // namespace SDK
