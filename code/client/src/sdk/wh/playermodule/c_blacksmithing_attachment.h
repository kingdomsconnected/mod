#pragma once

#include "../entitymodule/c_actor.h"

namespace SDK::wh::playermodule {
    /**
     * Something the smithing minigame puts on the smith while a workpiece is
     * worked: a tool, a slot-held tool or the workpiece itself. Vtable
     * 0x183A7BA78; its own slots 1 and 2 are pure, every concrete kind fills
     * them. The base carries no data -- the workpiece attachment's
     * destructor (0x180B6693C) only restores this vtable once its own
     * members are gone.
     */
    class C_BlacksmithingAttachment {
      public:
        virtual ~C_BlacksmithingAttachment()                       = default; // slot 0
        virtual void Attach(SDK::wh::entitymodule::C_Actor *actor) = 0;       // slot 1
        virtual void Detach()                                      = 0;       // slot 2
    };
} // namespace SDK::wh::playermodule
