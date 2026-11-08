#pragma once

#include <nlohmann/json.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <utility>

namespace KCDC::Shared::Inventory {
    /** Current outfit's quick-access assignments, using persistent server row IDs. */
    struct Quickslots {
        static constexpr std::size_t kSlotCount = 4;
        std::array<std::array<std::string, 2>, kSlotCount> weapons {};
        std::array<std::string, kSlotCount> items {};
        std::uint32_t activeWeapon = 0;
        std::uint32_t activeItem   = 0;

        bool operator==(const Quickslots &) const = default;

        nlohmann::json Encode() const {
            nlohmann::json weaponSlots = nlohmann::json::array();
            nlohmann::json itemSlots   = nlohmann::json::array();
            const auto reference       = [](const std::string &id) -> nlohmann::json {
                return id.empty() ? nlohmann::json(nullptr) : nlohmann::json(id);
            };
            for (std::size_t slot = 0; slot < kSlotCount; ++slot) {
                weaponSlots.push_back({{"primary", reference(weapons[slot][0])}, {"secondary", reference(weapons[slot][1])}});
                itemSlots.push_back(reference(items[slot]));
            }
            return {{"weapons", std::move(weaponSlots)}, {"items", std::move(itemSlots)}, {"activeWeapon", activeWeapon}, {"activeItem", activeItem}};
        }

        static bool Read(const nlohmann::json &value, Quickslots &out) {
            if (!value.is_object() || value.size() != 4 || !value.contains("weapons") || !value.contains("items") || !value.contains("activeWeapon") || !value.contains("activeItem")) {
                return false;
            }
            const auto &weapons      = value.at("weapons");
            const auto &items        = value.at("items");
            const auto &activeWeapon = value.at("activeWeapon");
            const auto &activeItem   = value.at("activeItem");
            if (!weapons.is_array() || weapons.size() != kSlotCount || !items.is_array() || items.size() != kSlotCount || !activeWeapon.is_number_unsigned() || activeWeapon.get<std::uint64_t>() >= kSlotCount || !activeItem.is_number_unsigned()
                || activeItem.get<std::uint64_t>() >= kSlotCount) {
                return false;
            }
            Quickslots parsed;
            const auto readId = [](const nlohmann::json &id, std::string &target) {
                if (id.is_null()) {
                    return true;
                }
                if (!id.is_string() || id.get_ref<const std::string &>().empty() || id.get_ref<const std::string &>().size() > 64) {
                    return false;
                }
                target = id.get<std::string>();
                return true;
            };
            for (std::size_t slot = 0; slot < kSlotCount; ++slot) {
                const auto &weapon = weapons[slot];
                if (!weapon.is_object() || weapon.size() != 2 || !weapon.contains("primary") || !weapon.contains("secondary") || !readId(weapon.at("primary"), parsed.weapons[slot][0]) || !readId(weapon.at("secondary"), parsed.weapons[slot][1])
                    || !readId(items[slot], parsed.items[slot])) {
                    return false;
                }
            }
            parsed.activeWeapon = activeWeapon.get<std::uint32_t>();
            parsed.activeItem   = activeItem.get<std::uint32_t>();
            out                 = std::move(parsed);
            return true;
        }

        template <typename Visit>
        void ForEachReference(Visit &&visit) {
            for (std::size_t slot = 0; slot < kSlotCount; ++slot) {
                visit(weapons[slot][0]);
                visit(weapons[slot][1]);
                visit(items[slot]);
            }
        }

        template <typename Contains>
        bool ReferencesExist(Contains &&contains) const {
            const auto exists = [&](const std::string &id) {
                return id.empty() || contains(id);
            };
            for (std::size_t slot = 0; slot < kSlotCount; ++slot) {
                if (!exists(weapons[slot][0]) || !exists(weapons[slot][1]) || !exists(items[slot])) {
                    return false;
                }
            }
            return true;
        }
    };
} // namespace KCDC::Shared::Inventory
