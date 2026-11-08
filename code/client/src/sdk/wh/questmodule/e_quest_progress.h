#pragma once

#include <cstdint>

namespace SDK::wh::questmodule {
    /**
     * Where a quest or an objective stands.
     *
     * Values are the reflection registration's own, at 0x18016E4D0, which
     * registers the enum as `wh::questmodule::QuestProgress`.
     *
     * `None` is not merely "not started": `C_QuestManager::CollectActiveQuests`
     * drops every quest whose progress port reads zero, so a quest at `None`
     * does not exist as far as the journal is concerned.
     */
    enum class E_QuestProgress : std::int32_t {
        None   = 0,
        Active = 1,
        Done   = 2,
        Failed = 3,
    };
} // namespace SDK::wh::questmodule
