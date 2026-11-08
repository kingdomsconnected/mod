#pragma once

#include "../system/cry_name.h"
#include "s_action_input.h"

namespace SDK {
    /**
     * One action in an action map, and the inputs bound to it.
     *
     * `CActionMapAction`, vtable 0x183A6B120. Stock CryEngine declares two
     * `GetActionInput` overloads; MSVC emits same-name virtuals in reverse, so
     * the game has the device overload at slot 5 (0x180A10B40) and the index
     * overload at slot 6 (0x180648414). Only the index one is named here.
     */
    class IActionMapAction {
      public:
        virtual ~IActionMapAction() = default;
        virtual void Opaque01()     = 0; // GetMemoryUsage
        virtual void Opaque02()     = 0; // Release
        virtual void Opaque03()     = 0; // GetNumActionInputs
        virtual void Opaque04()     = 0; // FindActionInput(const char *)
        virtual void Opaque05()     = 0; // GetActionInput(EActionInputDevice, int)

        /** The input at `index`, or null past the end; bounds-checked by the game. */
        virtual const SActionInput *GetActionInput(int index) const = 0; // +0x30, 0x180648414

        /** The action's name, an interned `CCryName` at +0x40. */
        virtual const CCryName &GetActionId() const = 0; // +0x38, 0x181A73520
    };

    static_assert(sizeof(IActionMapAction) == 0x8, "IActionMapAction interface size mismatch");
} // namespace SDK
