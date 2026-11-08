#pragma once

#include "i_anim_event_implementation.h"

#include <cstddef>

namespace SDK::wh::xgenaimodule {
    /** Stateless animation-event implementation that spawns an item. */
    class C_SpawnItemEventImplementation : public I_AnimEventImplementation {
      public:
        ~C_SpawnItemEventImplementation() override = default;
    };

    static_assert(sizeof(C_SpawnItemEventImplementation) == 0x8,
                  "C_SpawnItemEventImplementation size mismatch");
} // namespace SDK::wh::xgenaimodule
