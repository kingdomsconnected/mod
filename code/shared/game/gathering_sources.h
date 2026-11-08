#pragma once

#include <cstddef>
#include <cstdint>
#include <glm/vec3.hpp>
#include <span>
#include <string_view>
#include <vector>

namespace KCDC::Shared::Game {
    struct GatheringSource {
        std::uint8_t kind;
        glm::vec3 position;
    };

    /**
     * Authored medicinal herbs in a retail level, decoded from the catalog
     * embedded in the executable; excludes mushrooms and quest entities.
     * Unknown level: empty.
     */
    std::vector<GatheringSource> GatheringSources(std::string_view level);

    /** Decodes one level of a packed catalog. Unknown level: empty. Malformed: throws. */
    std::vector<GatheringSource> DecodeGatheringSources(std::span<const std::byte> catalog, std::string_view level);
} // namespace KCDC::Shared::Game
