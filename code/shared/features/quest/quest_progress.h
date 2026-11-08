#pragma once

#include <cstdint>

namespace KCDC::Shared::Entities {
    /**
     * Where a shared quest or one of its objectives stands.
     *
     * The game's own `wh::questmodule::E_QuestProgress`, so the number the
     * server decides is the number the client writes. `None` is not "not yet":
     * the game drops a quest at `None` out of the journal entirely, which is
     * why removing one is destroying the entity.
     */
    enum class QuestProgress : std::uint8_t {
        None   = 0,
        Active = 1,
        Done   = 2,
        Failed = 3,
    };

    inline bool IsQuestProgress(std::uint8_t value) {
        return value <= static_cast<std::uint8_t>(QuestProgress::Failed);
    }

    /**
     * Which section of the journal a quest files under. Also the game's own
     * numbering, `wh::questmodule::E_QuestType`.
     */
    enum class QuestType : std::uint8_t {
        Main     = 0,
        Side     = 1,
        Activity = 2,
        Event    = 3,
        Micro    = 4,
        Racing   = 5,
    };

    inline bool IsQuestType(std::uint8_t value) {
        return value <= static_cast<std::uint8_t>(QuestType::Racing);
    }
} // namespace KCDC::Shared::Entities
