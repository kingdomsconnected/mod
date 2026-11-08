#pragma once

#include "common/head_visibility.h"

#include <array>
#include <bit>

namespace HeadVisibilityTests {
    inline SDK::SMeshColor Color(std::uint8_t alpha) {
        return std::bit_cast<SDK::SMeshColor>(std::array<std::uint8_t, 4> {11, 22, 33, alpha});
    }

    inline SDK::SMeshSubset Subset(int first, int count) {
        SDK::SMeshSubset result {};
        result.nFirstIndexId = first;
        result.nNumIndices   = count;
        return result;
    }

    inline void Apply(std::span<SDK::SMeshColor> colors, std::span<const std::uint16_t> indices, std::span<const SDK::SMeshSubset> subsets) {
        const auto repair = KCDC::Common::FindMissingFirstPersonVertices(colors, indices, subsets);
        for (std::size_t i = 0; i < repair.size(); ++i) {
            if (repair[i]) {
                colors[i].MaskA(0xFE);
            }
        }
    }
} // namespace HeadVisibilityTests

MODULE(head_visibility, {
    IT("repairs unmarked eye overlays beside authored skin without hiding its neck", {
        std::array colors {HeadVisibilityTests::Color(0xFE), HeadVisibilityTests::Color(0xFF), HeadVisibilityTests::Color(0xFD), HeadVisibilityTests::Color(0xFF)};
        const std::array<std::uint16_t, 6> indices {0, 1, 0, 2, 2, 2};
        const std::array subsets {HeadVisibilityTests::Subset(0, 3), HeadVisibilityTests::Subset(3, 3)};
        HeadVisibilityTests::Apply(colors, indices, subsets);
        EQUALS(colors[0].GetAlpha(), 0xFE);
        EQUALS(colors[1].GetAlpha(), 0xFF); // Authored section's visible neck.
        EQUALS(colors[2].GetAlpha(), 0xFC); // Missing overlay, other group kept.
        EQUALS(colors[3].GetAlpha(), 0xFF); // Unreferenced vertex.
        EQUALS(KCDC::Common::FindMissingFirstPersonVertices(colors, indices, subsets).empty(), true);
    });

    IT("protects shared authored vertices independently of section order and bounding ranges", {
        std::array colors {HeadVisibilityTests::Color(0xFE), HeadVisibilityTests::Color(0xFF), HeadVisibilityTests::Color(0xFF), HeadVisibilityTests::Color(0xFF)};
        const std::array<std::uint16_t, 6> indices {0, 2, 0, 1, 2, 3};
        std::array subsets {HeadVisibilityTests::Subset(0, 3), HeadVisibilityTests::Subset(3, 3)};
        const auto first = KCDC::Common::FindMissingFirstPersonVertices(colors, indices, subsets);
        std::reverse(subsets.begin(), subsets.end());
        const auto second = KCDC::Common::FindMissingFirstPersonVertices(colors, indices, subsets);
        EQUALS(first == second, true);
        HeadVisibilityTests::Apply(colors, indices, subsets);
        EQUALS(colors[0].GetAlpha(), 0xFE);
        EQUALS(colors[1].GetAlpha(), 0xFE);
        EQUALS(colors[2].GetAlpha(), 0xFF);
        EQUALS(colors[3].GetAlpha(), 0xFE);
    });

    IT("rejects invalid section and vertex indices before returning any writes", {
        const std::array colors {HeadVisibilityTests::Color(0xFF)};
        const std::array<std::uint16_t, 3> indices {0, 0, 0};
        std::array subsets {HeadVisibilityTests::Subset(0, 3), HeadVisibilityTests::Subset(3, 1)};
        EQUALS(KCDC::Common::FindMissingFirstPersonVertices(colors, indices, subsets).empty(), true);
        subsets[1] = HeadVisibilityTests::Subset(-1, 1);
        EQUALS(KCDC::Common::FindMissingFirstPersonVertices(colors, indices, subsets).empty(), true);
        subsets[1] = HeadVisibilityTests::Subset(0, -1);
        EQUALS(KCDC::Common::FindMissingFirstPersonVertices(colors, indices, subsets).empty(), true);
        const std::array<std::uint16_t, 3> badIndices {0, 0, 1};
        EQUALS(KCDC::Common::FindMissingFirstPersonVertices(colors, badIndices, std::span(subsets).first(1)).empty(), true);
    });

    IT("preserves RGB and every shadow and non-first-person mask for all alpha values", {
        const std::array<std::uint16_t, 3> indices {0, 0, 0};
        const std::array subsets {HeadVisibilityTests::Subset(0, 3)};
        bool preserved = true;
        for (unsigned alpha = 0; alpha < 256; ++alpha) {
            std::array colors {HeadVisibilityTests::Color(static_cast<std::uint8_t>(alpha))};
            HeadVisibilityTests::Apply(colors, indices, subsets);
            const unsigned after = colors[0].GetAlpha();
            const auto rgba      = std::bit_cast<std::array<std::uint8_t, 4>>(colors[0]);
            preserved &= rgba[0] == 11 && rgba[1] == 22 && rgba[2] == 33 && after == (alpha & 0xFE);
            for (unsigned active = 0; active < 256; ++active) {
                preserved &= bool((~alpha & ~1u) & active) == bool((~after & ~1u) & active);
                if (!(active & 1u)) {
                    preserved &= bool(~alpha & active) == bool(~after & active);
                }
            }
        }
        EQUALS(preserved, true);
    });
});
