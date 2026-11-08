#pragma once

#include "shared/item_stack.h"

#include <mafianet/bit_stream.h>
#include <networking/rpc/rpc_identifier.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace KCDC::Shared::RPC {
    /**
     * The most item classes one victim offers up. The loot grid only ever
     * shows eleven, drawn at random from what it is handed.
     */
    inline constexpr std::size_t kMaxPickpocketStock = 32;

    /** The loot grid's tiles: two sets, of six and five, in `OpenLootUI`. */
    inline constexpr std::size_t kMaxPickpocketTaken = 11;

    /** How an attempt ended, as the thief's minigame resolved it. */
    enum class PickpocketOutcome : std::uint8_t {
        /** The thief closed the loot screen, having taken what the lines say, or nothing. */
        Stole = 0,

        /** The victim noticed: the charge failed its threshold, or the loot screen failed. */
        Caught = 1,

        /** Anything else: the thief walked off, lost sight of the victim, or was interrupted. */
        Abandoned = 2,
    };

    /** Why the server would not let an attempt go on. */
    enum class PickpocketRefusal : std::uint8_t {
        /** Not refused: the stock is attached. */
        None = 0,

        /** A `playerPickpocketStart` handler returned false. */
        Refused = 1,

        /** The victim is gone or busy with another thief. */
        Unavailable = 2,

        /** The two bodies are further apart than a pickpocket can reach. */
        TooFar = 3,
    };

    /** One item class and how many of it. */
    struct PickpocketItem {
        ItemClassId itemClass;
        std::uint32_t amount = 0;

        bool Serialize(MafiaNet::BitStream *bs, bool write) {
            return itemClass.Serialize(bs, write) && bs->Serialize(write, amount);
        }

        /** Bounded by what one give or take may carry, because each line settles as one. */
        bool IsSane() const {
            return itemClass.IsValid() && amount != 0 && amount <= kMaxGiveAmount;
        }
    };

    namespace Detail {
        /** A bounded run of item lines, refused whole the moment one does not read. */
        inline bool SerializePickpocketItems(MafiaNet::BitStream *bs, bool write, std::vector<PickpocketItem> &items, std::size_t limit) {
            std::uint8_t count = write ? static_cast<std::uint8_t>(items.size()) : 0;
            if (write && items.size() > limit) {
                return false;
            }
            if (!bs->Serialize(write, count)) {
                return false;
            }
            if (!write) {
                if (count > limit) {
                    return false;
                }
                items.assign(count, PickpocketItem {});
            }
            for (auto &item : items) {
                if (!item.Serialize(bs, write)) {
                    return false;
                }
            }
            return true;
        }

        inline bool ArePickpocketItemsSane(const std::vector<PickpocketItem> &items, std::size_t limit) {
            if (items.size() > limit) {
                return false;
            }
            for (const auto &item : items) {
                if (!item.IsSane()) {
                    return false;
                }
            }
            return true;
        }
    } // namespace Detail

    /**
     * Thief -> server: my game has started picking this player's pocket.
     *
     * Sent as the native charge begins rather than before it, so the game's
     * own input flow is untouched. The loot screen cannot open until the
     * verdict comes back: that is where the victim's pockets arrive from.
     */
    struct PickpocketRequest {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::PickpocketRequest");

        /** The thief's own id for this attempt; every later message about it echoes it. */
        std::uint32_t requestId = 0;

        std::uint64_t victimNetworkId = 0;
        bool valid                    = true;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            valid = bs->Serialize(write, requestId) && bs->Serialize(write, victimNetworkId);
        }

        bool IsSane() const {
            return valid && requestId != 0 && victimNetworkId != 0;
        }
    };

    /**
     * Server -> thief: go on with this stock, or stop.
     *
     * A refusal ends the thief's minigame the native way, before its loot
     * screen could open.
     */
    struct PickpocketVerdict {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::PickpocketVerdict");

        std::uint32_t requestId = 0;
        std::uint8_t refusal    = static_cast<std::uint8_t>(PickpocketRefusal::None);
        std::vector<PickpocketItem> items;
        bool valid = true;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            valid = bs->Serialize(write, requestId) && bs->Serialize(write, refusal) && Detail::SerializePickpocketItems(bs, write, items, kMaxPickpocketStock);
        }

        bool IsSane() const {
            return valid && requestId != 0 && refusal <= static_cast<std::uint8_t>(PickpocketRefusal::TooFar) && Detail::ArePickpocketItemsSane(items, kMaxPickpocketStock);
        }

        bool Accepted() const {
            return refusal == static_cast<std::uint8_t>(PickpocketRefusal::None);
        }
    };

    /**
     * Thief -> server: how the attempt ended, and what the loot screen took.
     *
     * A claim, not a transfer. The thief's game moved nothing: the server
     * checks every line against the stock it handed out, takes it from the
     * victim, and grants the thief only what really came out.
     */
    struct PickpocketResult {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::PickpocketResult");

        std::uint32_t requestId = 0;
        std::uint8_t outcome    = static_cast<std::uint8_t>(PickpocketOutcome::Abandoned);
        std::vector<PickpocketItem> taken;
        bool valid = true;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            valid = bs->Serialize(write, requestId) && bs->Serialize(write, outcome) && Detail::SerializePickpocketItems(bs, write, taken, kMaxPickpocketTaken);
        }

        /** Only a theft carries lines. */
        bool IsSane() const {
            const bool knownOutcome = outcome <= static_cast<std::uint8_t>(PickpocketOutcome::Abandoned);
            const bool linesFit     = taken.empty() || outcome == static_cast<std::uint8_t>(PickpocketOutcome::Stole);
            return valid && requestId != 0 && knownOutcome && linesFit && Detail::ArePickpocketItemsSane(taken, kMaxPickpocketTaken);
        }
    };
} // namespace KCDC::Shared::RPC
