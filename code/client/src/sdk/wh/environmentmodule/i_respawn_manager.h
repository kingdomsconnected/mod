#pragma once

#include <cstdint>

namespace SDK::wh::environmentmodule {
    /**
     * Abstract environment respawn service.
     *
     * The abstract table at 0x183ED0608 contains exactly four entries: its
     * virtual destructor and three stripped respawn/query operations. The
     * concrete C_RespawnManager table at 0x183AB92A8 preserves this order.
     */
    class I_RespawnManager {
      public:
        virtual ~I_RespawnManager()             = default;
        virtual void unk_fn0001()               = 0;
        virtual std::uint32_t HarvestSelected() = 0; // +0x10, 0x182ADDFA4
        virtual void unk_fn0003()               = 0;
    };

    static_assert(sizeof(I_RespawnManager) == 0x8, "I_RespawnManager size mismatch");
} // namespace SDK::wh::environmentmodule
