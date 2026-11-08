#include "png_image.h"

#include <utils/hashing.h>
#include <utils/string_utils.h>

#include <algorithm>
#include <string_view>

namespace KCDC::Shared::Image {
    namespace {
        // Strict RFC 4648 decoding; the caller re-encodes to refuse non-canonical input.
        bool Base64Decode(const std::string &in, std::string &out) {
            if (in.empty() || in.size() % 4 != 0) {
                return false;
            }
            out.clear();
            unsigned value = 0;
            int bits       = -8;
            for (std::size_t i = 0; i < in.size(); ++i) {
                const char c = in[i];
                if (c == '=') {
                    return i + 2 >= in.size();
                }
                const char *table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
                const char *found = std::char_traits<char>::find(table, 64, c);
                if (found == nullptr) {
                    return false;
                }
                value = (value << 6) + static_cast<unsigned>(found - table);
                bits += 6;
                if (bits >= 0) {
                    out.push_back(static_cast<char>((value >> bits) & 0xFF));
                    bits -= 8;
                }
            }
            return true;
        }

        bool PngStructure(const std::string &png) {
            const auto word = [&png](std::size_t at) {
                return static_cast<std::uint32_t>(static_cast<unsigned char>(png[at])) << 24 | static_cast<std::uint32_t>(static_cast<unsigned char>(png[at + 1])) << 16 | static_cast<std::uint32_t>(static_cast<unsigned char>(png[at + 2])) << 8
                       | static_cast<unsigned char>(png[at + 3]);
            };
            if (!png.starts_with(std::string_view("\x89PNG\r\n\x1a\n", 8))) {
                return false;
            }
            bool data = false;
            for (std::size_t at = 8; png.size() - at >= 12;) {
                const std::uint32_t length = word(at);
                if (length > png.size() - at - 12 || Framework::Utils::Hashing::CalculateCRC32(png.data() + at + 4, length + 4) != word(at + 8 + length)) {
                    return false;
                }
                const std::string_view type(png.data() + at + 4, 4);
                if (at == 8 && (type != "IHDR" || length != 13 || word(16) < 1 || word(16) > kMaximumPngSide || word(20) < 1 || word(20) > kMaximumPngSide)) {
                    return false;
                }
                data = data || type == "IDAT";
                at += 12 + length;
                if (type == "IEND") {
                    return data && at == png.size();
                }
            }
            return false;
        }
    } // namespace

    bool ReadBase64Png(const std::string &base64, std::string &png) {
        return base64.size() <= (kMaximumPngBytes + 2) / 3 * 4 && Base64Decode(base64, png) && Framework::Utils::StringUtils::Base64Encode(png) == base64 && png.size() <= kMaximumPngBytes && PngStructure(png);
    }

    ImageFit FitImage(std::uint32_t width, std::uint32_t height, std::uint32_t side) {
        // Rounded to the nearest pixel, never below one; the divisor is the
        // longer side, so it is zero only when both are and neither divides.
        ImageFit fit;
        fit.width  = width >= height ? side : std::max<std::uint32_t>(1, static_cast<std::uint32_t>((static_cast<std::uint64_t>(width) * side + height / 2) / height));
        fit.height = height >= width ? side : std::max<std::uint32_t>(1, static_cast<std::uint32_t>((static_cast<std::uint64_t>(height) * side + width / 2) / width));
        fit.left   = (side - fit.width) / 2;
        fit.top    = (side - fit.height) / 2;
        return fit;
    }

    std::vector<std::uint8_t> CenterImage(const std::vector<std::uint8_t> &scaled, const ImageFit &fit, std::uint32_t side) {
        std::vector<std::uint8_t> square(static_cast<std::size_t>(side) * side * 4, 0);
        for (std::uint32_t row = 0; row < fit.height; ++row) {
            std::copy_n(scaled.data() + static_cast<std::size_t>(row) * fit.width * 4, fit.width * 4, square.data() + (static_cast<std::size_t>(fit.top + row) * side + fit.left) * 4);
        }
        return square;
    }
} // namespace KCDC::Shared::Image
