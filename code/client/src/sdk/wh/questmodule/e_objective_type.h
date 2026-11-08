#pragma once

#include <cstdint>

namespace SDK::wh::questmodule {
    /**
     * Whether an objective has to be completed. Registered as
     * `wh::questmodule::ObjectiveType` at 0x18016C990.
     *
     * `C_Objective` does not store one of these -- it keeps a bare bool at
     * +0x108, which `C_UIFlashListObjective` copies to its own `IsOptional`.
     * The enum is the authored vocabulary behind that bool.
     */
    enum class E_ObjectiveType : std::int32_t {
        Obligatory = 0,
        Optional   = 1,
    };
} // namespace SDK::wh::questmodule
