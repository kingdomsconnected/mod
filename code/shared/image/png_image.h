#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace KCDC::Shared::Image {
    /** The longest side of a PNG a script may hand over; a client scales any size up to this. */
    inline constexpr std::uint32_t kMaximumPngSide = 4096;

    /**
     * A PNG travels inside its definition message; 256 KiB of it keeps one
     * message far below the transport's limits even as base64.
     */
    inline constexpr std::size_t kMaximumPngBytes = 256U * 1024U;

    /**
     * Reads a script's base64 PNG: canonical RFC 4648 base64 of at most
     * `kMaximumPngBytes`, a PNG whose chunks hold together -- the signature,
     * IHDR first with both sides 1 to `kMaximumPngSide`, every chunk's CRC
     * right, an IDAT, and IEND as the last bytes. Checked where a server
     * accepts the image, so it never takes a file cut short or damaged that
     * its clients would refuse. Image data that does not inflate still passes;
     * a client that cannot decode it shows its stock picture instead.
     */
    bool ReadBase64Png(const std::string &base64, std::string &png);

    /** Where a decoded image goes on a square: its scaled size and its top-left corner. */
    struct ImageFit {
        std::uint32_t width  = 0;
        std::uint32_t height = 0;
        std::uint32_t left   = 0;
        std::uint32_t top    = 0;
    };

    /** An image of `width` x `height` on a square of `side`: the longer side scaled to it, aspect kept, centred. */
    ImageFit FitImage(std::uint32_t width, std::uint32_t height, std::uint32_t side);

    /** `scaled`, `fit.width` x `fit.height` RGBA rows without padding, centred on a transparent square of `side`. */
    std::vector<std::uint8_t> CenterImage(const std::vector<std::uint8_t> &scaled, const ImageFit &fit, std::uint32_t side);
} // namespace KCDC::Shared::Image
