#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace KCDC::Shared::Game {
    /** Clothing slots and their authored underlayer requirements. */
    struct EquipmentClass {
        std::string_view guid;
        std::uint64_t slots;
        std::uint64_t requiredSlots;
        std::uint64_t excludedSlots;
        std::uint32_t equipOrder;

        bool CanWear(std::uint64_t occupiedSlots) const {
            return (occupiedSlots & requiredSlots) == requiredSlots && (occupiedSlots & excludedSlots) == 0;
        }
    };

    extern const std::vector<EquipmentClass> g_EquipmentClasses;
    const EquipmentClass *FindEquipmentClass(std::string_view guid);
} // namespace KCDC::Shared::Game
