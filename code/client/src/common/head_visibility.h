#pragma once

#include "sdk/cryengine/3dengine/s_mesh_color.h"
#include "sdk/cryengine/3dengine/s_mesh_subset.h"

#include <algorithm>
#include <cstdint>
#include <span>
#include <vector>

namespace KCDC::Common {
    /**
     * Plan against unchanged colours before touching engine storage. A
     * section with any authored FirstPersonView vertices keeps its mask;
     * a section with none needs that group. Actual indices matter: subset
     * vertex bounding ranges can overlap. Shared vertices belonging to an
     * authored section take precedence, preserving its intentional cutout.
     * An empty result means no repair, including malformed input.
     */
    inline std::vector<std::uint8_t> FindMissingFirstPersonVertices(std::span<const SDK::SMeshColor> colors, std::span<const std::uint16_t> indices, std::span<const SDK::SMeshSubset> subsets) {
        std::vector<std::uint8_t> repair(colors.size(), 0);
        for (const SDK::SMeshSubset &subset : subsets) {
            if (subset.nFirstIndexId < 0 || subset.nNumIndices < 0 || static_cast<std::size_t>(subset.nFirstIndexId) > indices.size() || static_cast<std::size_t>(subset.nNumIndices) > indices.size() - subset.nFirstIndexId) {
                return {};
            }
            const auto section = indices.subspan(subset.nFirstIndexId, subset.nNumIndices);
            bool authored      = false;
            for (const std::uint16_t vertex : section) {
                if (vertex >= colors.size()) {
                    return {};
                }
                authored |= (colors[vertex].GetAlpha() & 1u) == 0;
            }
            for (const std::uint16_t vertex : section) {
                repair[vertex] = std::max(repair[vertex], static_cast<std::uint8_t>(authored ? 2 : 1));
            }
        }
        bool changed = false;
        for (std::uint8_t &vertex : repair) {
            vertex = vertex == 1;
            changed |= vertex != 0;
        }
        if (!changed) {
            return {};
        }
        return repair;
    }
} // namespace KCDC::Common
