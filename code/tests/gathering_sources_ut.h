#pragma once

#include "shared/game/gathering_sources.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace GatheringSourcesTests {
    struct Group {
        std::uint8_t kind;
        std::int16_t sector[3];
        std::vector<std::uint16_t> positions;
    };

    struct Level {
        std::string_view name;
        std::vector<Group> groups;
    };

    class Writer final {
      public:
        void Bytes(const void *data, std::size_t size) {
            const auto *bytes = static_cast<const std::byte *>(data);
            data_.insert(data_.end(), bytes, bytes + size);
        }
        template <typename T>
        void Value(T value) {
            for (std::size_t i = 0; i < sizeof(T); ++i) {
                data_.push_back(static_cast<std::byte>((static_cast<std::uint64_t>(value) >> (8 * i)) & 0xFF));
            }
        }
        void Patch32(std::size_t at, std::uint32_t value) {
            for (std::size_t i = 0; i < 4; ++i) {
                data_[at + i] = static_cast<std::byte>((value >> (8 * i)) & 0xFF);
            }
        }
        std::vector<std::byte> &Data() {
            return data_;
        }

      private:
        std::vector<std::byte> data_;
    };

    // Builds a catalog in the generator's layout: header, level table, native groups.
    inline std::vector<std::byte> Catalog(std::initializer_list<Level> levels) {
        Writer writer;
        writer.Bytes("KCGS", 4);
        writer.Value<std::uint32_t>(1);
        writer.Value<std::uint32_t>(static_cast<std::uint32_t>(levels.size()));
        std::vector<std::size_t> entries;
        for (const auto &level : levels) {
            char name[16] = {};
            level.name.copy(name, sizeof(name));
            writer.Bytes(name, sizeof(name));
            entries.push_back(writer.Data().size());
            writer.Value<std::uint32_t>(0);
            writer.Value<std::uint32_t>(0);
            writer.Value<std::uint32_t>(0);
        }
        std::size_t index = 0;
        for (const auto &level : levels) {
            const auto offset = writer.Data().size();
            std::uint32_t count = 0;
            for (const auto &group : level.groups) {
                writer.Value<std::uint8_t>(group.kind);
                for (const auto sector : group.sector) {
                    writer.Value<std::uint16_t>(static_cast<std::uint16_t>(sector));
                }
                writer.Value<std::uint32_t>(static_cast<std::uint32_t>(group.positions.size() / 3));
                for (const auto value : group.positions) {
                    writer.Value<std::uint16_t>(value);
                }
                count += static_cast<std::uint32_t>(group.positions.size() / 3);
            }
            writer.Patch32(entries[index], count);
            writer.Patch32(entries[index] + 4, static_cast<std::uint32_t>(offset));
            writer.Patch32(entries[index] + 8, static_cast<std::uint32_t>(writer.Data().size() - offset));
            ++index;
        }
        return writer.Data();
    }

    inline std::vector<std::byte> Sample() {
        // Two adjacent plants one native step apart, then one in the next group.
        return Catalog({{"trosecko", {{2, {0, -1, 0}, {0, 65535, 0, 1, 65535, 0}}, {3, {1, 0, 0}, {0, 0, 0}}}}, {"klaster", {{4, {0, 0, 0}, {0, 0, 0}}}}});
    }

    inline bool Refuses(std::span<const std::byte> catalog, std::string_view level = "trosecko") {
        try {
            KCDC::Shared::Game::DecodeGatheringSources(catalog, level);
            return false;
        }
        catch (const std::runtime_error &) {
            return true;
        }
    }

    inline std::uint64_t Digest(const std::vector<KCDC::Shared::Game::GatheringSource> &sources) {
        std::uint64_t hash = 14695981039346656037ULL;
        for (const auto &source : sources) {
            hash = (hash ^ source.kind) * 1099511628211ULL;
            for (const float coordinate : {source.position.x, source.position.y, source.position.z}) {
                const auto bits = std::bit_cast<std::uint32_t>(coordinate);
                for (unsigned shift = 0; shift < 32; shift += 8) {
                    hash = (hash ^ ((bits >> shift) & 255U)) * 1099511628211ULL;
                }
            }
        }
        return hash;
    }
} // namespace GatheringSourcesTests

MODULE(gathering_sources, {
    IT("matches every PR 128 source in its original order and exact float identity", {
        const auto trosky = KCDC::Shared::Game::GatheringSources("trosecko");
        EQUALS(trosky.size(), std::size_t(758392));
        EQUALS(GatheringSourcesTests::Digest(trosky), 0x6326a0ebccc84e2eULL);
        const auto kuttenberg = KCDC::Shared::Game::GatheringSources("kutnohorsko");
        EQUALS(kuttenberg.size(), std::size_t(2362314));
        EQUALS(GatheringSourcesTests::Digest(kuttenberg), 0x3487abbdb5169e06ULL);
        const auto monastery = KCDC::Shared::Game::GatheringSources("klaster");
        EQUALS(monastery.size(), std::size_t(419736));
        EQUALS(GatheringSourcesTests::Digest(monastery), 0x156d02b247fc36a0ULL);
        EQUALS(KCDC::Shared::Game::GatheringSources("unknown").empty(), true);
    });

    IT("decodes native positions in order and keeps adjacent plants apart", {
        const auto catalog = GatheringSourcesTests::Sample();
        const auto sources = KCDC::Shared::Game::DecodeGatheringSources(catalog, "trosecko");
        EQUALS(sources.size(), std::size_t(3));
        EQUALS(sources[0].kind, std::uint8_t(2));
        EQUALS(sources[0].position.x, 0.0F);
        EQUALS(sources[0].position.y, 0.0F);
        EQUALS(sources[1].position.x, (1.0F / 65535.0F) * 16.0F);
        EQUALS(sources[0].position.x != sources[1].position.x, true);
        EQUALS(sources[2].kind, std::uint8_t(3));
        EQUALS(sources[2].position.x, 16.0F);
        const auto monastery = KCDC::Shared::Game::DecodeGatheringSources(catalog, "klaster");
        EQUALS(monastery.size(), std::size_t(1));
        EQUALS(monastery[0].kind, std::uint8_t(4));
        EQUALS(KCDC::Shared::Game::DecodeGatheringSources(catalog, "kutnohorsko").empty(), true);
    });

    IT("rejects a malformed header or level table", {
        auto catalog = GatheringSourcesTests::Sample();
        EQUALS(GatheringSourcesTests::Refuses(std::span(catalog).first(10)), true);
        catalog[0] = std::byte {'X'};
        EQUALS(GatheringSourcesTests::Refuses(catalog), true);
        catalog = GatheringSourcesTests::Sample();
        catalog[4] = std::byte {2};
        EQUALS(GatheringSourcesTests::Refuses(catalog), true);
        catalog = GatheringSourcesTests::Sample();
        catalog[8] = std::byte {9};
        EQUALS(GatheringSourcesTests::Refuses(catalog), true);
    });

    IT("rejects level data outside the catalog or not matching its count", {
        auto catalog = GatheringSourcesTests::Sample();
        // First level entry: name at 12, count at 28, offset at 32, size at 36.
        catalog[36] = std::byte {0xFF};
        EQUALS(GatheringSourcesTests::Refuses(catalog), true);
        catalog = GatheringSourcesTests::Sample();
        catalog[28] = std::byte {4};
        EQUALS(GatheringSourcesTests::Refuses(catalog), true);
        catalog = GatheringSourcesTests::Sample();
        catalog[28] = std::byte {2};
        EQUALS(GatheringSourcesTests::Refuses(catalog), true);
        EQUALS(GatheringSourcesTests::Refuses(std::span(catalog).first(catalog.size() - 1), "klaster"), true);
    });
});
