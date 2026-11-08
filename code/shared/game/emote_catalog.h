#pragma once

#include <cstdint>
#include <vector>

namespace KCDC::Shared::Game {
    struct EmoteEntry {
        std::uint16_t id;
        const char *name;
        const char *fragment;
        const char *tag;
        std::uint32_t fragmentCrc;
        std::uint32_t tagCrc;
        float duration;
        // The game's own `isfullbody`, straight out of `actor_anim_action`.
        // A full-body clip takes the FullBody scope and displaces locomotion,
        // so the body it plays on has to stop moving under it; an upper-body
        // one plays over a walk and must not.
        bool fullBody;
    };

    extern const std::vector<EmoteEntry> g_Emotes;

    inline const EmoteEntry *FindEmote(std::uint16_t id) {
        for (const auto &entry : g_Emotes) {
            if (entry.id == id) {
                return &entry;
            }
        }
        return nullptr;
    }
} // namespace KCDC::Shared::Game
