#pragma once

#include "xgenpublic/i_stealth_watcher.h"
#include <cstddef>
#include <cstdint>

namespace SDK::wh::xgenaimodule {
    /** Native process-wide local stealth watcher; factory allocates 0x30. */
    class C_StealthWatcher: public SDK::wh::xgenaimodule::XGenPublic::I_StealthWatcher {
      private:
        std::uint64_t m_state0008[5]; // 0x08, the singleton factory (0x180F667DC) zeroes all five
    };
    static_assert(sizeof(C_StealthWatcher) == 0x30, "C_StealthWatcher size mismatch");
} // namespace SDK::wh::xgenaimodule
