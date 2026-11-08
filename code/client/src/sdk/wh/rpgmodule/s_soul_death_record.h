#pragma once

#include "sdk/types.h"
#include "sdk/wh/rpgmodule/c_soul.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::rpgmodule {
    /**
     * What `C_Soul::QueueDeathIfDepleted` (0x180E7A79C) hands the soul-death
     * queue and what `DispatchSoulDeath` (0x181FDF720) consumes: the dying
     * soul, then the killer's entity id -- 0 when nobody dealt the blow.
     *
     * The dispatch reads nothing else and owns nothing in it, so a record
     * copied out and handed back to the dispatch later is the same death.
     */
    struct S_SoulDeathRecord {
        C_Soul *soul                  = nullptr;
        SDK::EntityId killerEntityId = 0;

      private:
        static void AssertLayout() {
            static_assert(offsetof(S_SoulDeathRecord, soul) == 0x0, "S_SoulDeathRecord::soul offset mismatch");
            static_assert(offsetof(S_SoulDeathRecord, killerEntityId) == 0x8, "S_SoulDeathRecord::killerEntityId offset mismatch");
        }
    };

    static_assert(sizeof(S_SoulDeathRecord) == 0x10, "S_SoulDeathRecord size mismatch");
} // namespace SDK::wh::rpgmodule
