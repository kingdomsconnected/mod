#pragma once

#include "../framework/wuid.h"

#include <cstdint>

namespace SDK::wh::rpgmodule {
    /**
     * Numbered values a buff instance's `Init` reads beyond its definition,
     * handed in as `initParams` to `C_BuffManager::AddBuffToSoul`.
     *
     * Null is replaced by `C_BuffInitParamsDefault` (vtable 0x183A43B88),
     * which answers 0, 0 and an invalid WUID for every index
     * (`BuildBuffInstance`, 0x18063ACE4). The buff only reads them during
     * `Init`; nothing keeps the object, so a caller's own stack one will do.
     * Each implementation answers the indices its buff knows and 0 (or an
     * invalid WUID) for the rest. The table at 0x183A43B60 holds the
     * destructor and these three, all pure.
     */
    class I_BuffInitParams {
      public:
        virtual ~I_BuffInitParams() = default;

        virtual float GetFloat(std::int32_t index) const = 0; // slot 1

        virtual std::uint32_t GetInt(std::int32_t index) const = 0; // slot 2

        /** Returned through a hidden pointer, as every implementation's (0x181A82500, 0x1816A59C0) writes it. */
        virtual framework::WUID GetWuid(std::int32_t index) const = 0; // slot 3
    };

    static_assert(sizeof(I_BuffInitParams) == 0x8, "I_BuffInitParams size mismatch");
} // namespace SDK::wh::rpgmodule
