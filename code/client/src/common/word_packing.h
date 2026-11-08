#pragma once

#include <bit>
#include <cstdint>

// Small values packed into one machine word.
//
// State that crosses threads without a lock has to be published a word at a
// time, because a word is the largest thing the hardware will write
// indivisibly. Anything narrower than 64 bits is therefore paired up on the
// way in and taken apart on the way out.
namespace KCDC::Common {
    /** Two floats in one word: `low` in the low half, `high` in the high. */
    constexpr std::uint64_t PackFloats(float low, float high) {
        return (static_cast<std::uint64_t>(std::bit_cast<std::uint32_t>(high)) << 32) | std::bit_cast<std::uint32_t>(low);
    }

    constexpr void UnpackFloats(std::uint64_t packed, float &low, float &high) {
        low  = std::bit_cast<float>(static_cast<std::uint32_t>(packed));
        high = std::bit_cast<float>(static_cast<std::uint32_t>(packed >> 32));
    }
} // namespace KCDC::Common
