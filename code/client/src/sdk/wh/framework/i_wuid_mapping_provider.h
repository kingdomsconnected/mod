#pragma once

#include <cstddef>

namespace SDK::wh::framework {
    /** WUID translation contract implemented by the inventory manager. */
    class I_WUIDMappingProvider {
      public:
        virtual void unk_fn0000() = 0;
        virtual void unk_fn0001() = 0;
        virtual ~I_WUIDMappingProvider() = default;
    };

    static_assert(sizeof(I_WUIDMappingProvider) == 0x8,
                  "I_WUIDMappingProvider size mismatch");
} // namespace SDK::wh::framework
