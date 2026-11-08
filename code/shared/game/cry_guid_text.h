#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

// The one place canonical GUID text becomes the 16 bytes the engine addresses
// things by. The game's tables write GUIDs as text and the wire carries the
// bytes, so every catalog that names something by GUID needs this conversion.
namespace KCDC::Shared::Game {
    namespace Detail {
        /** -1 for anything that is not a hex digit. */
        inline constexpr int HexValue(char digit) {
            if (digit >= '0' && digit <= '9') {
                return digit - '0';
            }
            if (digit >= 'a' && digit <= 'f') {
                return digit - 'a' + 10;
            }
            if (digit >= 'A' && digit <= 'F') {
                return digit - 'A' + 10;
            }
            return -1;
        }

        /** The byte the pair at `at` spells, or -1 when either half is not hex. */
        inline constexpr int HexByte(std::string_view text, std::size_t at) {
            const int high = HexValue(text[at]);
            const int low  = HexValue(text[at + 1]);
            return high < 0 || low < 0 ? -1 : (high << 4) | low;
        }

        /**
         * Where each of the 16 bytes is spelled in the canonical 8-4-4-4-12
         * text, as the index of its first hex digit.
         *
         * The game stores the first three fields as native little-endian
         * integers and the last eight as single bytes in the order written,
         * so those first three come out of the text back to front and the
         * rest come out forwards. Matches CryEngine's own
         * `CryGUID::Construct(d1, d2, d3, d4[8])`.
         */
        inline constexpr std::size_t kGuidByteDigits[16] = {6, 4, 2, 0, 11, 9, 16, 14, 19, 21, 24, 26, 28, 30, 32, 34};
    } // namespace Detail

    /**
     * The two halves of a `CryGUID` from its canonical text. `low` is
     * CryEngine's `hipart` and `high` its `lopart`.
     *
     * False for malformed text, leaving both halves untouched, so a caller
     * can keep its zero-initialised value and let the engine refuse it.
     */
    inline constexpr bool CryGuidFromText(std::string_view guid, std::uint64_t &low, std::uint64_t &high) {
        if (guid.size() != 36 || guid[8] != '-' || guid[13] != '-' || guid[18] != '-' || guid[23] != '-') {
            return false;
        }

        std::uint64_t parsedLow  = 0;
        std::uint64_t parsedHigh = 0;
        for (std::size_t index = 0; index < 16; ++index) {
            const int byte = Detail::HexByte(guid, Detail::kGuidByteDigits[index]);
            if (byte < 0) {
                return false;
            }
            std::uint64_t &half = index < 8 ? parsedLow : parsedHigh;
            half |= static_cast<std::uint64_t>(static_cast<std::uint8_t>(byte)) << ((index % 8) * 8);
        }

        low  = parsedLow;
        high = parsedHigh;
        return true;
    }

    /** Characters in canonical GUID text, without a terminator. */
    inline constexpr std::size_t kCryGuidTextLength = 36;

    /**
     * The canonical lower-case 8-4-4-4-12 text of a `CryGUID`'s two halves:
     * the inverse of `CryGuidFromText`, through the same digit table, so what
     * one writes the other reads back to the same bytes.
     */
    inline constexpr std::array<char, kCryGuidTextLength> CryGuidToText(std::uint64_t low, std::uint64_t high) {
        constexpr char kDigits[] = "0123456789abcdef";
        std::array<char, kCryGuidTextLength> text {};
        for (char &character : text) {
            character = '-';
        }
        for (std::size_t index = 0; index < 16; ++index) {
            const std::uint64_t half = index < 8 ? low : high;
            const auto byte          = static_cast<std::uint8_t>(half >> ((index % 8) * 8));
            const std::size_t at     = Detail::kGuidByteDigits[index];
            text[at]                 = kDigits[byte >> 4];
            text[at + 1]             = kDigits[byte & 0x0F];
        }
        return text;
    }

    inline std::string CryGuidToString(std::uint64_t low, std::uint64_t high) {
        const auto text = CryGuidToText(low, high);
        return std::string(text.data(), text.size());
    }

    namespace Detail {
        inline constexpr bool CryGuidTextRoundTrips(std::string_view guid) {
            std::uint64_t low  = 0;
            std::uint64_t high = 0;
            if (!CryGuidFromText(guid, low, high)) {
                return false;
            }
            const std::array<char, kCryGuidTextLength> text = CryGuidToText(low, high);
            return std::string_view(text.data(), text.size()) == guid;
        }
    } // namespace Detail

    static_assert(Detail::CryGuidTextRoundTrips("e7534f15-32fd-4405-bc63-91f0dc1fa786"), "CryGuidToText does not invert CryGuidFromText");
} // namespace KCDC::Shared::Game
