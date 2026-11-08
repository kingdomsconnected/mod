#pragma once

#include <cstddef>

namespace SDK::wh::combatmodule {
    /** Combat-facing environment coordination contract. */
    class I_CombatEnvironmentManager {
      public:
        virtual void unk_fn0000() = 0;
        virtual void unk_fn0001() = 0;
        virtual void unk_fn0002() = 0;
    };

    static_assert(sizeof(I_CombatEnvironmentManager) == 0x8,
                  "I_CombatEnvironmentManager size mismatch");
} // namespace SDK::wh::combatmodule
