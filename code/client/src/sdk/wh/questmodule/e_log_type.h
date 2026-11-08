#pragma once

#include <cstdint>

namespace SDK::wh::questmodule {
    /**
     * What a diary entry records about the objective it hangs off. Registered
     * as `wh::questmodule::LogType` at 0x18016C2C0.
     *
     * In the journal it is a visibility gate read twice, and only `Started`
     * and `Updated` clear both: 0x181F5C560 drops `None`, and the row builder
     * at 0x181F5C5A0 renders nothing else. `Completed` and `Canceled` pass
     * the first and are dropped by the second without a word.
     */
    enum class E_LogType : std::int32_t {
        None      = 0,
        Started   = 1,
        Updated   = 2,
        Completed = 3,
        Canceled  = 4,
    };
} // namespace SDK::wh::questmodule
