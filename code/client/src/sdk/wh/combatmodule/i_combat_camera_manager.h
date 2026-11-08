#pragma once

#include <cstddef>

namespace SDK::wh::combatmodule {
    /** Combat camera coordination contract. */
    class I_CombatCameraManager {
      public:
        virtual void unk_fn0000() = 0;
        virtual void unk_fn0001() = 0;
        virtual void unk_fn0002() = 0;
        virtual void unk_fn0003() = 0;
        virtual void unk_fn0004() = 0;
        virtual void unk_fn0005() = 0;
        virtual void unk_fn0006() = 0;
        virtual void unk_fn0007() = 0;
    };

    static_assert(sizeof(I_CombatCameraManager) == 0x8,
                  "I_CombatCameraManager size mismatch");
} // namespace SDK::wh::combatmodule
