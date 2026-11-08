#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace KCDC::Features::Player {
    /** The server's clock for one evaluation: real time and the replicated world time. */
    struct SoulClock {
        std::uint64_t realMilliseconds  = 0;
        std::uint64_t worldMilliseconds = 0;

        bool operator==(const SoulClock &) const = default;
    };

    /** One buff instance active on the soul, named by its buff definition GUID. */
    struct SoulEffectInstance {
        std::string id;
        std::string definition;
        // Absent for a permanent effect. A timed effect needs simulation the
        // server does not run, so the evaluator refuses one that contributes.
        std::optional<std::uint64_t> expiresAt;
    };

    /**
     * The character inputs an evaluation reads, assembled in memory from
     * what the server already holds for a connected player: the accepted
     * progression digest (skill levels and perks), the buffs the owner
     * reported, and the replicated soul's health. Nothing here is stored.
     */
    struct SoulProfile {
        // E_SoulSkill to its whole level. A skill that is absent reads as 0.
        std::map<std::uint32_t, std::uint32_t> skillLevels;
        // Owned perk GUIDs, canonical lower case.
        std::set<std::string> perks;
        std::vector<SoulEffectInstance> effects;
        float health = 0.0F;
    };

    struct SoulSnapshot {
        SoulClock clock;
        SoulProfile profile;
    };
} // namespace KCDC::Features::Player
