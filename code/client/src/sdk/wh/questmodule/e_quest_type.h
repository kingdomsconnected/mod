#pragma once

#include <cstdint>

namespace SDK::wh::questmodule {
    /**
     * Which section of the journal a quest files under, and which icon the
     * HUD gives it. Registered as `wh::questmodule::QuestType` at 0x18016DDC0.
     */
    enum class E_QuestType : std::int32_t {
        Main     = 0,
        Side     = 1,
        Activity = 2,
        Event    = 3,
        Micro    = 4,
        Racing   = 5,
    };
} // namespace SDK::wh::questmodule
