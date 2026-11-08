#pragma once

#include <mafianet/bit_stream.h>
#include <networking/replication/network_entity.h>
#include <networking/rpc/rpc_identifier.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Skills, stats and perks: what the server decides about a player's
// progression, and what the owner reports back.
//
// The server authorizes the input and the owner's engine computes the result.
// A gain the owner's game produces is withheld and asked for; the server
// answers with an order the owner applies through the game's own effect path,
// so the curve, the perk multipliers, the perk-point grants and the level-up
// popup all stay native. The owner then reports a digest the server checks
// against what it ordered. There is no native path that lowers a level, so an
// authority that arrived after the fact could never take a gain back.
namespace KCDC::Shared::RPC {
    /** Skills then stats, in `g_ProgressionTracks` order. */
    inline constexpr std::size_t kProgressionTracks = 20;

    /** One perk-point bank per track, plus the main-level bank the engine keys as property 89. */
    inline constexpr std::size_t kPerkTrees           = kProgressionTracks + 1;
    inline constexpr std::uint8_t kMainPerkTree       = static_cast<std::uint8_t>(kProgressionTracks);
    inline constexpr std::uint8_t kNoProgressionTrack = 0xFF;

    /** 960 bits, enough for the 945 rows the game ships. `progression_catalog.h` asserts it at startup. */
    inline constexpr std::size_t kPerkWords = 15;

    /** A receiver must never size an allocation from a number a peer chose. */
    inline constexpr std::size_t kMaxXpGainsPerRequest = 32;
    inline constexpr std::size_t kMaxXpSourceLength    = 48;

    /** Wire bound on one XP amount, in Q7 (xp * 128): far above anything a level costs, well inside 32 bits. */
    inline constexpr std::uint32_t kMaxXpQ7 = 1000000U * 128U;

    /** A perk's catalog index; the index both peers compiled from the same tables. */
    inline constexpr std::uint16_t kNoPerk = 0xFFFF;

    /** The engine's 16-byte perk `CryGUID` without a client-SDK dependency. */
    struct PerkGuid {
        std::uint64_t low  = 0;
        std::uint64_t high = 0;

        bool operator==(const PerkGuid &) const = default;

        bool IsValid() const {
            return low != 0 || high != 0;
        }
    };

    /** One bit per catalog perk. */
    struct PerkSet {
        std::array<std::uint64_t, kPerkWords> words {};

        bool operator==(const PerkSet &) const = default;

        bool Has(std::size_t index) const {
            return index < kPerkWords * 64 && (words[index / 64] & (1ULL << (index % 64))) != 0;
        }

        void Set(std::size_t index, bool on) {
            if (index >= kPerkWords * 64) {
                return;
            }
            if (on) {
                words[index / 64] |= 1ULL << (index % 64);
            }
            else {
                words[index / 64] &= ~(1ULL << (index % 64));
            }
        }

        std::size_t Count() const {
            std::size_t count = 0;
            for (const std::uint64_t word : words) {
                count += static_cast<std::size_t>(std::popcount(word));
            }
            return count;
        }

        /** Calls `visit(index, owned)` for every perk whose bit differs between the two sets, without walking the unchanged ones. */
        template <typename Visit>
        static void ForEachChange(const PerkSet &before, const PerkSet &after, Visit &&visit) {
            for (std::size_t word = 0; word < kPerkWords; ++word) {
                std::uint64_t changed = before.words[word] ^ after.words[word];
                while (changed != 0) {
                    const std::size_t bit = static_cast<std::size_t>(std::countr_zero(changed));
                    changed &= changed - 1;
                    const std::size_t index = word * 64 + bit;
                    visit(index, after.Has(index));
                }
            }
        }

        bool Serialize(MafiaNet::BitStream *bs, bool write) {
            for (std::uint64_t &word : words) {
                if (!bs->Serialize(write, word)) {
                    return false;
                }
            }
            return true;
        }
    };

    /** A track as the soul stores it: whole levels, and XP into the next one in Q7. */
    struct TrackState {
        std::uint32_t level = 0;
        std::uint32_t xpQ7  = 0;

        bool operator==(const TrackState &) const = default;
    };

    /** One gain the owner's game produced and withheld. */
    struct XpGain {
        std::uint8_t track = kNoProgressionTrack;
        std::uint32_t xpQ7 = 0;

        // Whether the game built the effect with its multipliers on. Level
        // advances (`AdvanceToSkillLevel`, quests, teachers) build it off.
        bool scaled = true;

        // The RPG cause that produced the gain -- `Attack`,
        // `LockpickingResult`, `SkillCheck` -- as the cause names itself.
        std::string source;

        bool IsSane() const {
            return track < kProgressionTracks && xpQ7 != 0 && xpQ7 <= kMaxXpQ7 && source.size() <= kMaxXpSourceLength;
        }

        bool Serialize(MafiaNet::BitStream *bs, bool write) {
            std::uint8_t sourceLength = write ? static_cast<std::uint8_t>(source.size() > kMaxXpSourceLength ? kMaxXpSourceLength : source.size()) : 0;
            if (!bs->Serialize(write, track) || !bs->Serialize(write, xpQ7) || !bs->Serialize(write, scaled) || !bs->Serialize(write, sourceLength)) {
                return false;
            }
            if (!write) {
                if (sourceLength > kMaxXpSourceLength) {
                    return false;
                }
                source.assign(sourceLength, '\0');
            }
            for (std::uint8_t i = 0; i < sourceLength; ++i) {
                if (!bs->Serialize(write, source[i])) {
                    return false;
                }
            }
            return true;
        }
    };

    /** Owner to server: the gains its game produced since the last request, withheld until the server orders them. */
    struct XpRequest {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::XpRequest");

        std::vector<XpGain> gains;
        bool valid = true;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            std::uint8_t count = write ? static_cast<std::uint8_t>(gains.size()) : 0;
            if ((write && gains.size() > kMaxXpGainsPerRequest) || !bs->Serialize(write, count)) {
                valid = false;
                return;
            }
            if (!write) {
                if (count > kMaxXpGainsPerRequest) {
                    valid = false;
                    return;
                }
                gains.assign(count, XpGain {});
            }
            for (std::uint8_t i = 0; i < count; ++i) {
                if (!gains[i].Serialize(bs, write)) {
                    valid = false;
                    return;
                }
            }
        }

        bool IsSane() const {
            if (!valid || gains.empty() || gains.size() > kMaxXpGainsPerRequest) {
                return false;
            }
            for (const XpGain &gain : gains) {
                if (!gain.IsSane()) {
                    return false;
                }
            }
            return true;
        }
    };

    enum class ProgressionOrderKind : std::uint8_t {
        Xp            = 0, // `value` Q7 XP on `track`, `scaled` picks the effect creator
        // 1 was a level raise through `AdvanceToSkillLevel`; `SetTotalXp` replaced it.
        GrantPerk     = 2, // add `perk` without spending a point
        LearnPerk     = 3, // the perk screen's own learn: spends a point from the perk's tree
        RemovePerk    = 4,
        Respec        = 5, // the engine's own respec: learnable perks out, points recomputed
        AddPerkPoints = 6, // `value` more points in bank `track`
        SetPerkPoints = 7, // bank `track` to exactly `value`
        SetTotalXp    = 8, // absolute unscaled Q7 XP, may lower level and remainder; `announce` lands a raise as a gain, with its level-ups and points
        Count         = 9,
    };

    /**
     * Server to owner: one change to apply through the game's own path.
     *
     * Sequenced so the owner can say which orders its next digest already
     * contains -- which is what lets the server bound a digest by exactly the
     * orders that could have produced it. Travels ReliableOrdered.
     */
    struct ProgressionOrder {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::ProgressionOrder/v2");

        std::uint32_t sequence    = 0;
        ProgressionOrderKind kind = ProgressionOrderKind::Count;
        std::uint8_t track        = kNoProgressionTrack;
        std::uint32_t value       = 0;
        std::uint16_t perk        = kNoPerk;
        bool scaled               = true;  // `Xp`: run the owner's XP multipliers
        bool announce             = false; // `SetTotalXp`: land a raise as a gain rather than a restore
        bool valid                = true;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            std::uint8_t wireKind = static_cast<std::uint8_t>(kind);
            valid                 = bs->Serialize(write, sequence) && bs->Serialize(write, wireKind) && bs->Serialize(write, track) && bs->Serialize(write, value) && bs->Serialize(write, perk) && bs->Serialize(write, scaled) && bs->Serialize(write, announce);
            if (!write) {
                kind = wireKind < static_cast<std::uint8_t>(ProgressionOrderKind::Count) ? static_cast<ProgressionOrderKind>(wireKind) : ProgressionOrderKind::Count;
            }
        }

        bool IsSane() const {
            if (!valid || sequence == 0) {
                return false;
            }
            switch (kind) {
            case ProgressionOrderKind::Xp: return track < kProgressionTracks && value != 0 && value <= kMaxXpQ7;
            case ProgressionOrderKind::SetTotalXp: return track < kProgressionTracks && value <= kMaxXpQ7;
            case ProgressionOrderKind::GrantPerk:
            case ProgressionOrderKind::LearnPerk:
            case ProgressionOrderKind::RemovePerk: return perk < kPerkWords * 64;
            case ProgressionOrderKind::Respec: return true;
            case ProgressionOrderKind::AddPerkPoints:
            case ProgressionOrderKind::SetPerkPoints: return track < kPerkTrees && value <= 0xFFFF;
            default: return false;
            }
        }
    };

    /** Owner to server: the player confirmed a perk on the perk screen, and the engine was told no until the server says yes. */
    struct LearnPerkRequest {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::LearnPerkRequest");

        std::uint16_t perk = kNoPerk;
        bool valid         = true;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            valid = bs->Serialize(write, perk);
        }

        bool IsSane() const {
            return valid && perk < kPerkWords * 64;
        }
    };

    enum class PerkRefusal : std::uint8_t {
        Points    = 0, // no point left in the perk's tree
        Level     = 1, // the tree is below the perk's level
        Parent    = 2, // the perk it follows is not owned
        Exclusive = 3, // its exclusive partner is owned
        Blocked   = 4, // this server blocks it
        Script    = 5, // a resource refused it
        Owned     = 6, // already owned
        Hidden    = 7, // not a perk the perk screen offers
        Count     = 8,
    };

    /** Server to owner: a learn request the server turned down, so the owner's scripts can say why. */
    struct PerkRefused {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::PerkRefused");

        std::uint16_t perk = kNoPerk;
        PerkRefusal reason = PerkRefusal::Count;
        bool valid         = true;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            std::uint8_t wireReason = static_cast<std::uint8_t>(reason);
            valid                   = bs->Serialize(write, perk) && bs->Serialize(write, wireReason);
            if (!write) {
                reason = wireReason < static_cast<std::uint8_t>(PerkRefusal::Count) ? static_cast<PerkRefusal>(wireReason) : PerkRefusal::Count;
            }
        }

        bool IsSane() const {
            return valid && perk < kPerkWords * 64 && reason != PerkRefusal::Count;
        }
    };

    /** Server to every client: whether their games may earn XP at all, and which perks the engine may never add on its own. */
    struct ProgressionPolicy {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::ProgressionPolicy");

        bool nativeXp = true;
        PerkSet blocked;
        bool valid = true;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            valid = bs->Serialize(write, nativeXp) && blocked.Serialize(bs, write);
        }

        bool IsSane() const {
            return valid;
        }
    };

    /** Owner to server, on change: the whole of the local body's progression. */
    struct ProgressionDigest {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::ProgressionDigest");

        std::uint32_t revision = 0;

        // The last order this digest already contains. Zero before the first.
        std::uint32_t appliedOrder = 0;

        std::array<TrackState, kProgressionTracks> tracks {};
        std::array<std::uint16_t, kPerkTrees> perkPoints {};
        std::uint8_t mainLevel = 0;
        PerkSet perks;
        bool valid = true;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            valid = bs->Serialize(write, revision) && bs->Serialize(write, appliedOrder) && bs->Serialize(write, mainLevel);
            for (TrackState &track : tracks) {
                valid = valid && bs->Serialize(write, track.level) && bs->Serialize(write, track.xpQ7);
            }
            for (std::uint16_t &points : perkPoints) {
                valid = valid && bs->Serialize(write, points);
            }
            valid = valid && perks.Serialize(bs, write);
        }

        bool IsSane() const {
            if (!valid || revision == 0) {
                return false;
            }
            for (const TrackState &track : tracks) {
                if (track.level > 255 || track.xpQ7 > kMaxXpQ7) {
                    return false;
                }
            }
            return true;
        }
    };
} // namespace KCDC::Shared::RPC

namespace KCDC::Shared::Game {
    /**
     * What everyone else sees of a player's progression: levels and perks,
     * never XP or points.
     *
     * Server-written -- the server's checked copy of the owner's digest -- and
     * carried as a server field, so the owner never echoes a stale one and
     * never receives it either: its own soul is the live copy.
     */
    struct ProgressionView {
        std::uint8_t mainLevel = 0;
        std::array<std::uint8_t, RPC::kProgressionTracks> levels {};
        RPC::PerkSet perks;

        bool operator==(const ProgressionView &) const = default;

        void Serialize(Framework::Networking::Replication::FieldSerializer &fields) {
            fields.Field(mainLevel);
            for (std::uint8_t &level : levels) {
                fields.Field(level);
            }
            for (std::uint64_t &word : perks.words) {
                fields.Field(word);
            }
        }
    };
} // namespace KCDC::Shared::Game
