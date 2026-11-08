#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace KCDC::Shared::Game {
    struct PlayerStatEntry {
        std::string_view name;
        std::string_view value;
        std::uint32_t nativeId;
        std::string_view description;
    };

    // Resource order is E_SoulState order; these are the writable states.
    inline constexpr std::array kPlayerStats {
        PlayerStatEntry {"Health", "health", 0, "Current health, in native units. Zero can kill."},
        PlayerStatEntry {"Stamina", "stamina", 1, "Current stamina; the game continues spending and regenerating it."},
        PlayerStatEntry {"Exhaust", "exhaust", 2, "Current energy reserve; higher means better rested."},
        PlayerStatEntry {"Hunger", "hunger", 3, "Current nourishment; higher means better fed."},
    };

    // `E_SoulDerivedStat` rows. The first kPlayerConditionCount are the
    // gameplay conditions carried in `HumanSoulState::conditions`; the rest
    // are script readings carried in `PlayerStatReadings`.
    inline constexpr std::size_t kPlayerConditionCount = 5;
    inline constexpr std::array kDerivedPlayerStats {
        PlayerStatEntry {"Bleeding", "bleeding", 35, "Total bleeding strength."},
        PlayerStatEntry {"Sleeping", "sleeping", 54, "Sleepiness; above zero means asleep."},
        PlayerStatEntry {"Consciousness", "consciousness", 55, "Consciousness; zero means knocked out."},
        PlayerStatEntry {"Drunkenness", "drunkenness", 63, "Native drunkenness value."},
        PlayerStatEntry {"Poisoning", "poisoning", 64, "Native poisoning value; not every named poison changes this value."},
        PlayerStatEntry {"Charisma", "charisma", 0, "Effective charisma."},
        PlayerStatEntry {"Visibility", "visibility", 3, "Effective visibility."},
        PlayerStatEntry {"Conspicuousness", "conspicuousness", 5, "Effective conspicuousness."},
        PlayerStatEntry {"Noise", "noise", 29, "Effective noise."},
        PlayerStatEntry {"Dirtiness", "dirtiness", 72, "Native dirtiness reading; reading it does not clean the body or equipment."},
        PlayerStatEntry {"Bloodiness", "bloodiness", 88, "Native bloodiness reading."},
        PlayerStatEntry {"Smell", "smell", 73, "Native smell reading."},
        PlayerStatEntry {"SmellIntensity", "smellIntensity", 74, "Native smell intensity."},
        PlayerStatEntry {"Fragrance", "fragrance", 75, "Native fragrance reading."},
        PlayerStatEntry {"CarriedWeight", "carriedWeight", 40, "Carried weight in the same units as InventoryCapacity."},
        PlayerStatEntry {"InventoryCapacity", "inventoryCapacity", 23, "Native carrying capacity, including applicable equipment effects."},
        PlayerStatEntry {"Encumbrance", "encumbrance", 77, "Native encumbrance reading."},
        PlayerStatEntry {"Hangover", "hangover", 113, "Native hangover reading."},
        PlayerStatEntry {"Alcoholism", "alcoholism", 109, "Computed alcoholism reading, not the persistent soul resource."},
        PlayerStatEntry {"ArmorRating", "armorRating", 90, "Effective armor rating."},
        PlayerStatEntry {"OverallArmorDefense", "overallArmorDefense", 25, "Overall armor defense."},
        PlayerStatEntry {"OverallWeaponAttack", "overallWeaponAttack", 26, "Overall weapon attack."},
        PlayerStatEntry {"NormalizedRunSpeed", "normalizedRunSpeed", 31, "Normalized run speed; not world-space velocity."},
        PlayerStatEntry {"RunSpeedBase", "runSpeedBase", 32, "Native base run speed."},
        PlayerStatEntry {"Morale", "morale", 42, "Native morale reading."},
    };

    template <std::size_t Count>
    constexpr std::size_t FindPlayerStat(const std::array<PlayerStatEntry, Count> &entries, std::string_view value) {
        for (std::size_t i = 0; i < Count; ++i) {
            if (entries[i].value == value) {
                return i;
            }
        }
        return Count;
    }

    /**
     * The derived stats past the gameplay conditions, read by scripts only.
     *
     * Kept out of `HumanSoulState` so they do not advance its revision: each
     * value is its own field on the state channel, so the delta serializer
     * sends only the ones that moved. The owner samples them on the soul's
     * resample interval and quantizes them, so sub-step jitter in a reading
     * like visibility does not produce a delta at all.
     */
    struct PlayerStatReadings {
        static constexpr std::size_t kCount = kDerivedPlayerStats.size() - kPlayerConditionCount;

        // Readings are native units of very different magnitudes; a
        // hundredth is below what any of them presents to a script.
        static constexpr float kStep = 0.01F;

        // Protocol validation limit, the same as `HumanSoulState`'s.
        static constexpr float kMaxNativeValue = 1000000.0F;

        std::array<float, kCount> values {};

        bool operator==(const PlayerStatReadings &) const = default;

        static float Quantize(float value) {
            return std::round(value / kStep) * kStep;
        }

        // Computed values can legitimately be negative.
        bool IsSane() const {
            for (const float value : values) {
                if (!std::isfinite(value) || std::abs(value) > kMaxNativeValue) {
                    return false;
                }
            }
            return true;
        }

        template <typename Fields> void SerializeFields(Fields &fields) {
            for (auto &value : values) {
                fields.Field(value);
            }
        }
    };
} // namespace KCDC::Shared::Game
