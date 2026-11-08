#pragma once

#include "sdk/types.h"
#include <cstddef>

namespace KCDC::Features::Items {
    /**
     * Observed POD result, not a claim about its original C++ type name.
     * 0x180560314 initializes 0x18 bytes; 0x18055F9CC returns it through RDX.
     */
    struct NativeInventoryActionHint {
        bool visible;
        bool enabled;
        std::byte m_opaque0002[6];
        SDK::CryStringT<char> reason;
        SDK::CryStringT<char> label;

      private:
        static void AssertLayout() {
            static_assert(offsetof(NativeInventoryActionHint, visible) == 0);
            static_assert(offsetof(NativeInventoryActionHint, enabled) == 1);
            static_assert(offsetof(NativeInventoryActionHint, reason) == 8);
            static_assert(offsetof(NativeInventoryActionHint, label) == 0x10);
        }
    };
    static_assert(sizeof(NativeInventoryActionHint) == 0x18);
} // namespace KCDC::Features::Items
