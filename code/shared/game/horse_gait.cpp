#include "shared/game/horse_gait.h"

#include <array>
#include <limits>

namespace KCDC::Shared::Game {
    namespace {
        // A speed this far over a style's switch speed is the next style's, so a
        // band's floor stays clear of the one below it.
        constexpr float kStyleSwitchMargin = 0.05F;

        constexpr std::size_t kMaxStyles = 8;

        bool IsCartVariant(std::string_view tags) {
            std::size_t start = 0;
            while (start <= tags.size()) {
                const std::size_t end = std::min(tags.find('+', start), tags.size());
                if (tags.substr(start, end - start) == "cart") {
                    return true;
                }
                start = end + 1;
            }
            return false;
        }

        HorseGaitBand BuildBand(std::size_t index) {
            const HorseMoveStyleEntry &style = g_HorseMoveStyles[index];
            HorseGaitBand band;
            band.styleId      = style.styleId;
            band.moveSpeedMin = 0.0F;
            band.moveSpeedMax = std::numeric_limits<float>::max();
            band.turnSpeedMax = std::numeric_limits<float>::max();
            bool found        = false;
            for (const HorseGaitBlendspaceEntry &entry : g_HorseGaitBlendspaces) {
                if (entry.gait != style.gait || IsCartVariant(entry.tags)) {
                    continue;
                }
                found             = true;
                band.moveSpeedMin = std::max(band.moveSpeedMin, entry.moveSpeedMin);
                band.moveSpeedMax = std::min(band.moveSpeedMax, entry.moveSpeedMax);
                band.turnSpeedMax = std::min(band.turnSpeedMax, std::min(-entry.turnSpeedMin, entry.turnSpeedMax));
            }
            if (!found) {
                band.moveSpeedMin = style.realSpeedMin;
                band.moveSpeedMax = style.realSpeedMax;
                band.turnSpeedMax = 0.0F;
            }
            // Held to the speeds that keep the game on this style: above the one
            // below's switch speed, and, short of the last, at or under its own.
            if (index > 0) {
                band.moveSpeedMin = std::max(band.moveSpeedMin, g_HorseMoveStyles[index - 1].realSpeedMax + kStyleSwitchMargin);
            }
            if (index + 1 < g_HorseMoveStyles.size()) {
                band.moveSpeedMax = std::min(band.moveSpeedMax, style.realSpeedMax);
            }
            band.moveSpeedMax = std::max(band.moveSpeedMax, band.moveSpeedMin);
            return band;
        }

        struct BandTable {
            std::array<HorseGaitBand, kMaxStyles> bands {};
            std::size_t count = 0;

            BandTable() {
                count = std::min(g_HorseMoveStyles.size(), kMaxStyles);
                for (std::size_t i = 0; i < count; ++i) {
                    bands[i] = BuildBand(i);
                }
            }
        };

        const BandTable &Bands() {
            static const BandTable table;
            return table;
        }
    } // namespace

    std::int32_t HorseMoveStyleForSpeed(float speed) {
        for (const HorseMoveStyleEntry &style : g_HorseMoveStyles) {
            if (style.realSpeedMax >= speed) {
                return style.styleId;
            }
        }
        return g_HorseMoveStyles.empty() ? -1 : g_HorseMoveStyles.back().styleId;
    }

    HorseGaitBand HorseGaitBandFor(std::int32_t styleId) {
        const BandTable &table = Bands();
        if (table.count == 0) {
            return {};
        }
        const std::size_t index = styleId < 0 ? 0 : std::min(static_cast<std::size_t>(styleId), table.count - 1);
        return table.bands[index];
    }
} // namespace KCDC::Shared::Game
