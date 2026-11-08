#pragma once

#include "shared/game/cook_pot_catalog.h"
#include <cstdint>

namespace KCDC::Shared::CookPot {
    enum class State : std::uint8_t {
        Empty,
        Half,
        Full
    };

    /** Who decides whether a pot's fire burns; the server's values are so_fireplace.xml's fire phases. */
    enum class Fire : std::uint8_t {
        // Each player's own fireplace brain, as in single player.
        Native,
        Out,
        Burning
    };

    struct Stock {
        static constexpr std::uint16_t kDefaultPortions = 4;
        static constexpr std::uint16_t kMaximumPortions = 1000;
        std::uint16_t portions                          = kDefaultPortions;
        std::uint16_t capacity                          = kDefaultPortions;
        bool enabled                                    = true;

        KCDC::Shared::Game::CookPotFood food = KCDC::Shared::Game::CookPotFood::Goulash;
        Fire fire                            = Fire::Native;
        // Server-only: clients learn an effect only after eating a serving.
        KCDC::Shared::RPC::BuffId poison;

        bool operator==(const Stock &) const = default;

        State Appearance() const {
            return portions == 0 ? State::Empty : portions * 2 <= capacity ? State::Half : State::Full;
        }

        bool Take() {
            if (!enabled || portions == 0) {
                return false;
            }
            --portions;
            return true;
        }

        bool Refill(std::uint16_t amount) {
            if (amount > kMaximumPortions) {
                return false;
            }
            poison   = {};
            portions = amount;
            capacity = amount == 0 ? kDefaultPortions : amount;
            return true;
        }
    };
} // namespace KCDC::Shared::CookPot
