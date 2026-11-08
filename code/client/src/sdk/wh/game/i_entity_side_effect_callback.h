#pragma once

#include <cstddef>

namespace SDK::wh::game {
    /** Receives entity-side-effect lifecycle notifications. */
    class I_EntitySideEffectCallback {
      public:
        virtual void unk_fn0000() = 0;
        virtual void unk_fn0001() = 0;
    };

    static_assert(sizeof(I_EntitySideEffectCallback) == 0x8,
                  "I_EntitySideEffectCallback size mismatch");
} // namespace SDK::wh::game
