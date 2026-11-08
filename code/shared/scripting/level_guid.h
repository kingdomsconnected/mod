#pragma once

#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>

namespace KCDC::Scripting {
    // A level EntityGuid as script sees it: sixteen lowercase hex digits. Not a
    // number -- it is a full 64 bits, and a JS double rounds above 2^53.
    inline std::string FormatLevelGuid(std::uint64_t guid) {
        static constexpr char kDigits[] = "0123456789abcdef";

        std::string text(16, '0');
        for (int digit = 15; digit >= 0; --digit) {
            text[static_cast<std::size_t>(digit)] = kDigits[guid & 0xFU];
            guid >>= 4U;
        }
        return text;
    }

    // The GUID a string names, or 0. Takes an `0x` prefix and either case, and
    // refuses the whole string rather than parsing a valid prefix of it.
    inline std::uint64_t ParseLevelGuid(std::string_view text) {
        if (text.size() > 2 && (text.substr(0, 2) == "0x" || text.substr(0, 2) == "0X")) {
            text.remove_prefix(2);
        }
        if (text.empty() || text.size() > 16) {
            return 0;
        }

        std::uint64_t guid = 0;
        for (const char character : text) {
            const int digit = std::isdigit(static_cast<unsigned char>(character)) ? character - '0' : character >= 'a' && character <= 'f' ? character - 'a' + 10 : character >= 'A' && character <= 'F' ? character - 'A' + 10 : -1;
            if (digit < 0) {
                return 0;
            }
            guid = (guid << 4U) | static_cast<std::uint64_t>(digit);
        }
        return guid;
    }
} // namespace KCDC::Scripting
