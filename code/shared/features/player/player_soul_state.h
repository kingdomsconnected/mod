#pragma once

#include "shared/features/player/player_stat.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>

namespace KCDC::Shared::Game {
    // Replicates resource pairs and effective soul-query results, without
    // native soul storage or progression.
    struct HumanSoulState {
        /** One of the replicated resources and the capacity behind it. */
        struct Resource {
            float current                           = 0.0F;
            float maximum                           = 0.0F;
            bool operator==(const Resource &) const = default;
        };

        // Native `E_SoulState` order. Karma and alcoholism are deliberately
        // left behind: they are save-side bookkeeping, and `GetStateMax`
        // answers FLT_MAX for alcoholism, which no bounded field can carry.
        static constexpr std::size_t kHealth        = 0;
        static constexpr std::size_t kStamina       = 1;
        static constexpr std::size_t kExhaust       = 2;
        static constexpr std::size_t kHunger        = 3;
        static constexpr std::size_t kResourceCount = 4;

        // The leading `kDerivedPlayerStats` rows, and the slot each takes in
        // `conditions`.
        static constexpr std::array<std::uint32_t, kPlayerConditionCount> kConditionSelectors = [] {
            std::array<std::uint32_t, kPlayerConditionCount> selectors {};
            for (std::size_t i = 0; i < selectors.size(); ++i) {
                selectors[i] = kDerivedPlayerStats[i].nativeId;
            }
            return selectors;
        }();
        static constexpr std::size_t kBleeding      = 0;
        static constexpr std::size_t kSleeping      = 1;
        static constexpr std::size_t kConsciousness = 2;
        static constexpr std::size_t kDrunkenness   = 3;
        static constexpr std::size_t kPoisoning     = 4;

        // Fencing, survival, defense, sword, heavy weapons, marksmanship,
        // dagger, large weapons, unarmed: the `E_SoulSkill` rows a hit or a
        // draw is evaluated against.
        static constexpr std::array<std::uint32_t, 9> kSkillSelectors {2, 14, 15, 16, 17, 19, 22, 23, 24};

        /** Strength, agility, vitality -- `E_SoulStat` rows 0 through 2. */
        static constexpr std::size_t kStatCount = 3;

        // Physics profile 255 means unavailable. Preserve that sentinel
        // without applying it to the native body.
        static constexpr std::uint8_t kMaxPhysicsProfile     = 6;
        static constexpr std::uint8_t kUnknownPhysicsProfile = 255;

        // Protocol validation limit; not an attainable level cap.
        static constexpr float kMaxNativeValue = 1000000.0F;

        bool valid = false;

        // Incarnation advances on actor/soul replacement or revival. Revision
        // advances on state changes and accepted death capture. Both skip
        // zero.
        std::uint32_t incarnation = 0;
        std::uint32_t revision    = 0;

        std::uint8_t physicsProfile = kUnknownPhysicsProfile;
        std::array<Resource, kResourceCount> resources {};
        float healthyStamina = 0.0F;
        std::array<float, kConditionSelectors.size()> conditions {};
        std::array<float, kStatCount> stats {};
        std::array<float, kStatCount> relativeStats {};
        std::array<float, kSkillSelectors.size()> skills {};
        std::array<float, kSkillSelectors.size()> relativeSkills {};

        bool operator==(const HumanSoulState &) const = default;

        /** The next value of a counter that reserves zero for "never set". */
        static std::uint32_t Advance(std::uint32_t counter) {
            return ++counter == 0 ? 1 : counter;
        }

        /** Wrap-safe "came after", for counters that never return to zero. */
        static bool IsNewer(std::uint32_t candidate, std::uint32_t previous) {
            return candidate != previous && static_cast<std::int32_t>(candidate - previous) > 0;
        }

        // Accept newer incarnations or revisions; equal revisions require
        // identical state.
        bool Follows(const HumanSoulState &previous) const {
            if (previous.incarnation == 0) {
                return true;
            }
            if (incarnation != previous.incarnation) {
                return IsNewer(incarnation, previous.incarnation);
            }
            return *this == previous || IsNewer(revision, previous.revision);
        }

        /** Whether a profile travelled, or the body had none to report. */
        bool HasPhysicsProfile() const {
            return physicsProfile != kUnknownPhysicsProfile;
        }

        bool IsSane() const {
            if (HasPhysicsProfile() && physicsProfile > kMaxPhysicsProfile) {
                return false;
            }
            if (valid && (incarnation == 0 || revision == 0)) {
                return false;
            }
            const auto bounded = [](float value) { return std::isfinite(value) && value >= 0.0F && value <= kMaxNativeValue; };
            if (!bounded(healthyStamina)) {
                return false;
            }
            for (const auto &resource : resources) {
                if (!bounded(resource.current) || !bounded(resource.maximum)) {
                    return false;
                }
            }
            // Health capacity must be positive once RPG tables load. Stamina
            // capacity may reach zero before current stamina during death.
            if (valid && resources[kHealth].maximum <= 0.0F) {
                return false;
            }
            for (const auto &values : {std::span<const float>(conditions), std::span<const float>(stats), std::span<const float>(relativeStats), std::span<const float>(skills), std::span<const float>(relativeSkills)}) {
                if (!std::all_of(values.begin(), values.end(), bounded)) {
                    return false;
                }
            }
            return true;
        }

        /**
         * Whether this snapshot differs from `previous` in a way others have
         * to see the moment it happens, rather than at the next resample.
         *
         * A body replaced, withdrawn or re-profiled; a capacity changed; a
         * hit taken; a resource run dry or recovered from empty; the body
         * starting or ceasing to be able to act. What is left is drift --
         * stamina spent and regained, exhaustion and hunger creeping, health
         * regenerating -- which changes every frame while it lasts and which
         * nothing reads closer than a bar does.
         */
        bool IsEdgeFrom(const HumanSoulState &previous) const {
            if (valid != previous.valid || incarnation != previous.incarnation || physicsProfile != previous.physicsProfile || CanAct() != previous.CanAct()) {
                return true;
            }
            if (resources[kHealth].current < previous.resources[kHealth].current) {
                return true;
            }
            for (std::size_t i = 0; i < kResourceCount; ++i) {
                if (resources[i].maximum != previous.resources[i].maximum || (resources[i].current > 0.0F) != (previous.resources[i].current > 0.0F)) {
                    return true;
                }
            }
            return false;
        }

        /** Whether the body behind this snapshot can be driven at all. */
        bool CanAct() const {
            return valid && resources[kHealth].current > 0.0F && conditions[kConsciousness] > 0.0F && conditions[kSleeping] <= 0.0F;
        }

        /** What everyone else's bar draws, from the pair behind it. */
        float HealthPercent() const {
            return valid && resources[kHealth].maximum > 0.0F ? std::clamp(100.0F * resources[kHealth].current / resources[kHealth].maximum, 0.0F, 100.0F) : 0.0F;
        }

        // One fixed field sequence, shared by construction and the delta
        // channel so the two cannot drift apart.
        template <typename Fields> void SerializeFields(Fields &fields) {
            fields.Field(valid);
            fields.Field(incarnation);
            fields.Field(revision);
            fields.Field(physicsProfile);
            for (auto &resource : resources) {
                fields.Field(resource.current);
                fields.Field(resource.maximum);
            }
            fields.Field(healthyStamina);
            for (auto &value : conditions) {
                fields.Field(value);
            }
            for (auto &value : stats) {
                fields.Field(value);
            }
            for (auto &value : relativeStats) {
                fields.Field(value);
            }
            for (auto &value : skills) {
                fields.Field(value);
            }
            for (auto &value : relativeSkills) {
                fields.Field(value);
            }
        }
    };
} // namespace KCDC::Shared::Game
