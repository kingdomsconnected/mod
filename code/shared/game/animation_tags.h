#pragma once

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace KCDC::Shared::Game {
    /**
     * Calls `visit(tag)` for each tag of a `+`-separated list, the way
     * `actor_anim_action` and `playAnimation` spell them, with the whitespace
     * around each trimmed and empty entries skipped. `visit` returns false to
     * stop early.
     */
    template <typename Visit>
    void ForEachAnimationTag(std::string_view tags, Visit &&visit) {
        while (!tags.empty()) {
            const std::size_t split = tags.find('+');
            std::string_view tag    = tags.substr(0, split);
            tags                    = split == std::string_view::npos ? std::string_view() : tags.substr(split + 1);
            while (!tag.empty() && std::isspace(static_cast<unsigned char>(tag.front()))) {
                tag.remove_prefix(1);
            }
            while (!tag.empty() && std::isspace(static_cast<unsigned char>(tag.back()))) {
                tag.remove_suffix(1);
            }
            if (!tag.empty() && !visit(tag)) {
                return;
            }
        }
    }

    /**
     * An empty Mannequin tag group, narrowed to a byte for the wire.
     *
     * The engine's own "nothing set" is `CTagDefinition::kInvalidId`, a full
     * 32-bit -1; every tag a replicated body carries fits in a byte. Shared
     * rather than per-entity so a body and its mount cannot drift apart.
     */
    inline constexpr std::uint8_t kNoAnimationTag = 0xFF;
} // namespace KCDC::Shared::Game
