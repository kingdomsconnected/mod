#pragma once

#include <cstdint>

namespace KCDC::Core {
    /** Which of `required` this machine is missing, as a `KCDC::Shared::Game::DLCBit` mask. */
    std::uint16_t MissingDLC(std::uint16_t required);
} // namespace KCDC::Core
