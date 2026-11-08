#pragma once

#include <cstddef>

namespace SDK::wh::entitymodule {
    /** Receives equipment-manager state changes. */
    class I_EquipmentManagerListener {
      public:
        virtual ~I_EquipmentManagerListener() = default;
        virtual void unk_fn0001() = 0;
        virtual void unk_fn0002() = 0;
        virtual void unk_fn0003() {} // emitted no-op callback
    };

    static_assert(sizeof(I_EquipmentManagerListener) == 0x8,
                  "I_EquipmentManagerListener size mismatch");
} // namespace SDK::wh::entitymodule
