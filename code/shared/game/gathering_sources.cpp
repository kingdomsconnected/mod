#include "gathering_sources.h"

#include <algorithm>
#include <stdexcept>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
// The generated catalog is linked into read-only data; see code/server/CMakeLists.txt.
__asm__(".section .rodata\n"
        ".balign 16\n"
        ".global kcdc_gathering_sources\n"
        "kcdc_gathering_sources:\n"
        ".incbin \"" KCDC_GATHERING_SOURCES_BIN "\"\n"
        ".global kcdc_gathering_sources_end\n"
        "kcdc_gathering_sources_end:\n"
        ".previous\n");
extern "C" const std::byte kcdc_gathering_sources[];
extern "C" const std::byte kcdc_gathering_sources_end[];
#endif

namespace KCDC::Shared::Game {
    namespace {
        constexpr std::uint32_t kVersion    = 1;
        constexpr std::size_t kHeaderSize   = 12;
        constexpr std::size_t kEntrySize    = 28;
        constexpr std::size_t kNameSize     = 16;
        constexpr std::size_t kGroupSize    = 11;
        constexpr std::size_t kPositionSize = 6;

        class Reader final {
          public:
            explicit Reader(std::span<const std::byte> data): _data(data) {}

            std::uint32_t Read(std::size_t offset, std::size_t size) const {
                if (offset > _data.size() || size > _data.size() - offset) {
                    throw std::runtime_error("Invalid gathering catalog");
                }
                std::uint32_t value = 0;
                for (std::size_t i = 0; i < size; ++i) {
                    value |= static_cast<std::uint32_t>(_data[offset + i]) << (8 * i);
                }
                return value;
            }

          private:
            std::span<const std::byte> _data;
        };

        std::vector<GatheringSource> DecodeLevel(std::span<const std::byte> packed, std::uint32_t count) {
            if (count > packed.size() / kPositionSize) {
                throw std::runtime_error("Invalid gathering catalog count");
            }
            const Reader reader(packed);
            std::vector<GatheringSource> sources;
            sources.reserve(count);
            std::size_t cursor = 0;
            while (cursor < packed.size()) {
                const auto kind = static_cast<std::uint8_t>(reader.Read(cursor, 1));
                glm::vec3 origin;
                for (unsigned axis = 0; axis < 3; ++axis) {
                    const auto sector = static_cast<std::int16_t>(reader.Read(cursor + 1 + axis * 2, 2));
                    origin[axis]      = static_cast<float>(sector) * 16.0F;
                }
                const auto groupCount = reader.Read(cursor + 7, 4);
                cursor += kGroupSize;
                if (groupCount > count - sources.size()) {
                    throw std::runtime_error("Invalid gathering catalog count");
                }
                for (std::uint32_t i = 0; i < groupCount; ++i, cursor += kPositionSize) {
                    glm::vec3 position;
                    for (unsigned axis = 0; axis < 3; ++axis) {
                        // Native uint16 within the 16 m sector, decoded as the engine does.
                        position[axis] = (static_cast<float>(reader.Read(cursor + axis * 2, 2)) / 65535.0F) * 16.0F + origin[axis];
                    }
                    sources.push_back({kind, position});
                }
            }
            if (sources.size() != count) {
                throw std::runtime_error("Invalid gathering catalog count");
            }
            return sources;
        }

        std::span<const std::byte> EmbeddedCatalog() {
#ifdef _WIN32
            HRSRC resource = FindResourceW(nullptr, L"KCDC_GATHERING_SOURCES", MAKEINTRESOURCEW(10)); // RT_RCDATA
            HGLOBAL loaded = resource != nullptr ? LoadResource(nullptr, resource) : nullptr;
            const void *data = loaded != nullptr ? LockResource(loaded) : nullptr;
            if (data == nullptr) {
                throw std::runtime_error("The gathering catalog is not linked into this executable");
            }
            return {static_cast<const std::byte *>(data), SizeofResource(nullptr, resource)};
#else
            return {kcdc_gathering_sources, kcdc_gathering_sources_end};
#endif
        }
    } // namespace

    std::vector<GatheringSource> DecodeGatheringSources(std::span<const std::byte> catalog, std::string_view level) {
        const Reader reader(catalog);
        if (reader.Read(0, 4) != 0x5347434BU || reader.Read(4, 4) != kVersion) {
            throw std::runtime_error("Invalid gathering catalog header");
        }
        const auto levels = reader.Read(8, 4);
        if (levels > (catalog.size() - kHeaderSize) / kEntrySize) {
            throw std::runtime_error("Invalid gathering catalog level table");
        }
        for (std::uint32_t i = 0; i < levels; ++i) {
            const auto entry = kHeaderSize + i * kEntrySize;
            const auto count = reader.Read(entry + kNameSize, 4);
            const auto offset = reader.Read(entry + kNameSize + 4, 4);
            const auto size  = reader.Read(entry + kNameSize + 8, 4);
            const auto *name = reinterpret_cast<const char *>(catalog.data() + entry);
            if (std::string_view(name, std::find(name, name + kNameSize, '\0')) != level) {
                continue;
            }
            if (offset > catalog.size() || size > catalog.size() - offset) {
                throw std::runtime_error("Invalid gathering catalog level range");
            }
            return DecodeLevel(catalog.subspan(offset, size), count);
        }
        return {};
    }

    std::vector<GatheringSource> GatheringSources(std::string_view level) {
        return DecodeGatheringSources(EmbeddedCatalog(), level);
    }
} // namespace KCDC::Shared::Game
