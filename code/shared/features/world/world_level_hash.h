#pragma once

#include <cstdint>
#include <string>

namespace KCDC::Shared::RPC {
    /**
     * FNV-1a over the level name.
     *
     * Level entity GUIDs are unique only within a level, so every message
     * about one names the level it came from. Zero is reserved for unset,
     * which is why a hash that lands on zero is nudged to one.
     */
    inline std::uint32_t LevelHash(const std::string &level) {
        std::uint32_t hash = 2166136261U;
        for (const char character : level) {
            hash ^= static_cast<std::uint8_t>(character);
            hash *= 16777619U;
        }
        return hash != 0 ? hash : 1U;
    }
} // namespace KCDC::Shared::RPC
