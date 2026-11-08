#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <mafianet/bit_stream.h>

namespace KCDC::Shared::Game {
    /** Which native soul query answered, in `C_Soul` vtable terms. */
    enum class HumanSoulQueryKind : std::uint8_t {
        State,
        StateMax,
        Stat,
        RelativeStat,
        Derived,
        Skill,
        RelativeSkill,
        Modifier
    };

    /**
     * One answer the attacker's soul gave during its own hit calculation.
     *
     * Keyed by everything the native call was given rather than by what it
     * returned, so the victim can look the same question up when its engine
     * asks it again. A key that does not match is a question the attacker
     * never answered, and the hit is rejected rather than guessed at.
     */
    struct HumanSoulQuery {
        HumanSoulQueryKind kind {};
        std::uint32_t selector = 0;
        float base             = 0.0F;

        /**
         * The query context, reduced to roles.
         *
         * `references` names which of none, attacker, victim, weapon, ammo
         * or defense item each context slot pointed at -- never an engine
         * address or a WUID, which mean nothing on the other machine.
         */
        bool hasContext      = false;
        std::uint32_t reason = 0;
        std::array<std::uint8_t, 4> references {};

        float result = 0.0F;

        /** Roles the context slots are allowed to name. */
        static constexpr std::uint8_t kNoReference       = 0;
        static constexpr std::uint8_t kMaxReference      = 5;
        static constexpr std::uint32_t kMaxContextReason = 255;

        /**
         * The same question, ignoring the value being modified.
         *
         * A modifier's `base` is an input threaded through the calculation
         * rather than a property of the soul being asked. In the missile hit
         * path it is the injury chance, which `ResolveVictimDamageEffects`
         * derives from the victim's own defense skill and armour -- values
         * the protocol deliberately keeps local -- so the attacker and the
         * victim compute different bases for the same question, by design.
         */
        bool SameQuery(const HumanSoulQuery &other) const {
            return kind == other.kind && selector == other.selector && hasContext == other.hasContext && reason == other.reason && references == other.references;
        }

        bool SameInput(const HumanSoulQuery &other) const {
            return SameQuery(other) && base == other.base;
        }

        /**
         * Whether the soul left the value alone.
         *
         * `C_Soul::ApplyModifiers` walks the soul's modifier list and clamps;
         * a soul carrying no modifier of that kind returns its input. So an
         * answer equal to its own base says the attacker contributed nothing
         * here, which is a transform the victim can reproduce against any
         * base rather than only against the one that was recorded.
         */
        bool IsIdentity() const {
            return base == result;
        }

        bool IsSane() const {
            // One past the highest row each native table defines: six soul
            // states, ten stats, 218 derived stats and 35 skills, mined from
            // the enum name tables. Modifier kinds have no name table, so
            // that bound is a safety limit rather than an exact count.
            static constexpr std::array<std::uint32_t, 8> kSelectorLimits {6, 6, 10, 10, 218, 35, 35, 512};
            const auto type = static_cast<std::uint8_t>(kind);
            if (type >= kSelectorLimits.size() || selector >= kSelectorLimits[type]) {
                return false;
            }
            if (!std::isfinite(base) || !std::isfinite(result) || std::abs(base) > 1000000.0F || std::abs(result) > 1000000.0F || reason > kMaxContextReason) {
                return false;
            }
            return std::all_of(references.begin(), references.end(), [](std::uint8_t reference) { return reference <= kMaxReference; });
        }

        bool Serialize(MafiaNet::BitStream *bs, bool write) {
            auto type = static_cast<std::uint8_t>(kind);
            bool good = bs->Serialize(write, type) && bs->Serialize(write, selector) && bs->Serialize(write, base) && bs->Serialize(write, hasContext) && bs->Serialize(write, reason);
            for (auto &reference : references) {
                good = good && bs->Serialize(write, reference);
            }
            kind = static_cast<HumanSoulQueryKind>(type);
            return good && bs->Serialize(write, result);
        }
    };

    /**
     * Every attacker-soul answer one accepted hit needed, plus the four
     * resource values the attacker held while it was calculated.
     *
     * Bounded on purpose: an unbounded record would let one client dictate
     * arbitrary work on another. A calculation that runs past the bound, or
     * that answers the same question twice with different numbers, is
     * discarded rather than truncated.
     */
    struct HumanSoulAction {
        static constexpr std::size_t kMaxQueries    = 64;
        static constexpr std::size_t kResourceCount = 4;

        std::uint32_t incarnation       = 0;
        std::uint32_t revision          = 0;
        std::uint32_t targetIncarnation = 0;
        std::uint8_t epoch              = 0;
        std::uint8_t targetEpoch        = 0;
        std::array<float, kResourceCount> resources {};
        std::array<HumanSoulQuery, kMaxQueries> queries {};
        std::uint8_t count = 0;
        bool complete      = false;

        /** False when the record can no longer describe the calculation. */
        bool Add(const HumanSoulQuery &query) {
            if (!query.IsSane()) {
                return false;
            }
            for (std::size_t i = 0; i < count; ++i) {
                if (queries[i].SameInput(query)) {
                    return queries[i].result == query.result;
                }
            }
            if (count == queries.size()) {
                return false;
            }
            queries[count++] = query;
            return true;
        }

        /** What the record can say about a question being asked again. */
        enum class Lookup : std::uint8_t {
            /** The attacker never answered this question. */
            Missing,
            /** Same question, same input: the recorded answer applies. */
            Recorded,
            /**
             * Same question, different input, and the attacker's soul left
             * its own input alone. Reproduce that by leaving this one alone.
             */
            Unmodified,
            /**
             * Same question, different input, and the attacker's soul did
             * transform it. Modifiers add, scale, clamp or interpolate, so
             * one recorded sample cannot be moved to a different input --
             * this is the case the protocol genuinely cannot carry.
             */
            Rebased,
        };

        Lookup Find(const HumanSoulQuery &query, float &result) const {
            bool related  = false;
            bool identity = true;
            for (std::size_t i = 0; i < count; ++i) {
                if (!queries[i].SameQuery(query)) {
                    continue;
                }
                if (queries[i].SameInput(query)) {
                    result = queries[i].result;
                    return Lookup::Recorded;
                }
                related  = true;
                identity = identity && queries[i].IsIdentity();
            }
            if (!related) {
                return Lookup::Missing;
            }
            return identity ? Lookup::Unmodified : Lookup::Rebased;
        }

        bool IsSane() const {
            // An empty record is legitimate -- a calculation can reach its
            // result without asking the attacker's soul anything -- so long
            // as the victim replay asks nothing either. What is rejected is
            // an incomplete capture, a record fenced against no body, or one
            // that answers the same question twice.
            if (!complete || incarnation == 0 || revision == 0 || targetIncarnation == 0 || count > queries.size()) {
                return false;
            }
            for (auto value : resources) {
                if (!std::isfinite(value) || value < 0.0F || value > 1000000.0F) {
                    return false;
                }
            }
            for (std::size_t i = 0; i < count; ++i) {
                if (!queries[i].IsSane()) {
                    return false;
                }
                for (std::size_t j = 0; j < i; ++j) {
                    if (queries[i].SameInput(queries[j])) {
                        return false;
                    }
                }
            }
            return true;
        }

        bool Serialize(MafiaNet::BitStream *bs, bool write) {
            bool good = bs->Serialize(write, incarnation) && bs->Serialize(write, revision) && bs->Serialize(write, targetIncarnation) && bs->Serialize(write, epoch) && bs->Serialize(write, targetEpoch) && bs->Serialize(write, complete) && bs->Serialize(write, count);
            if (!good || count > queries.size()) {
                return false;
            }
            for (auto &resource : resources) {
                good = good && bs->Serialize(write, resource);
            }
            for (std::size_t i = 0; i < count; ++i) {
                good = good && queries[i].Serialize(bs, write);
            }
            return good;
        }
    };
} // namespace KCDC::Shared::Game
