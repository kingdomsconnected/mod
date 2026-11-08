#pragma once

#include "shared/item_stack.h"

#include <mafianet/bit_stream.h>
#include <networking/rpc/rpc_identifier.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace KCDC::Shared::RPC {
    /** A receiver must never size an allocation from a number a peer chose. */
    inline constexpr std::size_t kMaxVendorRows        = 128;
    inline constexpr std::size_t kMaxVendorBasketLines = 64;
    /** Server rows named by a basket's sell lines, one entry per row. */
    inline constexpr std::size_t kMaxVendorSoldUnits = 128;

    /**
     * The most one unit may cost, in money units -- the amount of the game's
     * `money` item, which is what the trade screen's own prices count in.
     */
    inline constexpr std::uint32_t kMaxVendorPrice = 1000000;

    /**
     * The most money one deal may move either way.
     *
     * Money is the `money` item, so this also bounds the change one commit
     * can credit, well inside what a server inventory row may hold.
     */
    inline constexpr std::uint64_t kMaxVendorDealMoney = 1000000;

    /** What a vendor is, as both sides agree to spell it. */
    enum class VendorFlags : std::uint8_t {
        None = 0,

        /** The vendor takes the player's items; without it only buying works. */
        Buys = 1 << 0,

        /** The vendor's money never runs out, so it is neither shown nor checked. */
        BottomlessPurse = 1 << 1,
    };

    /** Which way one basket line moves, from the player's side of the counter. */
    enum class VendorSide : std::uint8_t {
        /** The item was the vendor's: the player is buying it. */
        Buy = 1,

        /** The item was the player's: the player is selling it. */
        Sell = 2,
    };

    /** Why a trading session ended. */
    enum class VendorCloseReason : std::uint8_t {
        /** The player closed the trade screen. */
        Closed = 0,

        /** A script closed it, or destroyed the vendor. */
        Server = 1,

        /** The player opened another vendor, which ends this one. */
        Replaced = 2,

        /** The client could not bring the trade screen up at all. */
        Unavailable = 3,

        /** The player left mid-session. Never sent: there is nobody to send it to. */
        Interrupted = 4,
    };

    /** How the server answered one basket. */
    enum class VendorOutcome : std::uint8_t {
        Accepted = 0,

        /** The basket was priced against a list the server has since replaced. */
        StaleList = 1,

        /** A line names an item the vendor neither sells nor buys. */
        NotTraded = 2,

        /** A buy line asks for more than the vendor has. */
        OutOfStock = 3,

        /** The vendor's purse cannot cover what the basket pays out. */
        VendorCannotAfford = 4,

        /** The player's money did not cover the difference. */
        PlayerCannotAfford = 5,

        /** An item the player offered was no longer in their inventory. */
        ItemsMissing = 6,

        /** Another deal at this vendor was still being settled. No longer sent: deals settle in one commit. */
        Busy = 7,

        /** The basket moves more money than one deal may. */
        TooLarge = 8,

        /** The player's inventory is not logged in or cannot act yet. */
        InventoryUnavailable = 9,

        /** What the deal would hand over does not fit the player's inventory. */
        InventoryFull = 10,
    };

    /** One thing the vendor sells: how many it has and what one costs. */
    struct VendorStockRow {
        ItemClassId itemClass;
        std::uint32_t amount = 0;
        std::uint32_t price  = 0;
        InventoryItem properties;

        bool Serialize(MafiaNet::BitStream *bs, bool write) {
            if (!itemClass.Serialize(bs, write) || !bs->Serialize(write, amount) || !bs->Serialize(write, price)) {
                return false;
            }
            if (write) {
                properties.inventoryItemId = 1;
                properties.itemClass       = itemClass;
                properties.amount          = amount;
            }
            return properties.Serialize(bs, write) && properties.itemClass == itemClass && properties.amount == amount;
        }

        bool IsSane() const {
            return itemClass.IsValid() && amount != 0 && amount <= kMaxInventoryStackAmount && price <= kMaxVendorPrice;
        }
    };

    /** One thing the vendor buys, and what it pays for one. */
    struct VendorBuyRow {
        ItemClassId itemClass;
        std::uint32_t price = 0;

        bool Serialize(MafiaNet::BitStream *bs, bool write) {
            return itemClass.Serialize(bs, write) && bs->Serialize(write, price);
        }

        bool IsSane() const {
            return itemClass.IsValid() && price <= kMaxVendorPrice;
        }
    };

    /** One line of what the player agreed to on the trade screen. */
    struct VendorBasketLine {
        ItemClassId itemClass;
        std::uint32_t amount = 0;
        std::uint8_t side    = static_cast<std::uint8_t>(VendorSide::Buy);

        bool Serialize(MafiaNet::BitStream *bs, bool write) {
            return itemClass.Serialize(bs, write) && bs->Serialize(write, amount) && bs->Serialize(write, side);
        }

        bool IsSane() const {
            const bool knownSide = side == static_cast<std::uint8_t>(VendorSide::Buy) || side == static_cast<std::uint8_t>(VendorSide::Sell);
            return itemClass.IsValid() && amount != 0 && amount <= kMaxGiveAmount && knownSide;
        }
    };

    /** Units of one server inventory row a basket hands over. */
    struct VendorSoldUnit {
        /** The server's own row ids are short; anything longer names no row. */
        static constexpr std::size_t kMaxIdLength = 64;

        std::string id;
        std::uint32_t amount = 0;

        bool Serialize(MafiaNet::BitStream *bs, bool write) {
            std::uint8_t length = write ? static_cast<std::uint8_t>(id.size()) : 0;
            if ((write && id.size() > kMaxIdLength) || !bs->Serialize(write, length) || length == 0 || length > kMaxIdLength) {
                return false;
            }
            if (!write) {
                id.resize(length);
            }
            return bs->Serialize(write, id.data(), length) && bs->Serialize(write, amount);
        }

        bool IsSane() const {
            return !id.empty() && id.size() <= kMaxIdLength && amount != 0 && amount <= kMaxGiveAmount;
        }
    };

    namespace Detail {
        /** A bounded run of rows, refused whole the moment one does not read. */
        template <typename Row>
        bool SerializeVendorRows(MafiaNet::BitStream *bs, bool write, std::vector<Row> &rows, std::size_t limit) {
            std::uint16_t count = write ? static_cast<std::uint16_t>(rows.size()) : 0;
            if (write && rows.size() > limit) {
                return false;
            }
            if (!bs->Serialize(write, count)) {
                return false;
            }
            if (!write) {
                if (count > limit) {
                    return false;
                }
                rows.assign(count, Row {});
            }
            for (auto &row : rows) {
                if (!row.Serialize(bs, write)) {
                    return false;
                }
            }
            return true;
        }

        template <typename Row>
        bool AreVendorRowsSane(const std::vector<Row> &rows, std::size_t limit) {
            if (rows.size() > limit) {
                return false;
            }
            for (const auto &row : rows) {
                if (!row.IsSane()) {
                    return false;
                }
            }
            return true;
        }
    } // namespace Detail

    /**
     * Server -> client: trade with this vendor on the game's own screen.
     *
     * A session runs on one player and names the NPC whose soul keeps the
     * shop. The lists arrive separately as a `VendorStock`, always right
     * behind this one, so a refresh after a deal and the first fill are the
     * same message.
     */
    struct VendorOpen {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::VendorOpen");

        std::uint32_t sessionId = 0;

        /** The NPC keeping the shop, or 0 to trade with nobody in particular. */
        std::uint64_t npcNetworkId = 0;

        std::uint8_t flags = 0;
        bool valid         = true;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            valid = bs->Serialize(write, sessionId) && bs->Serialize(write, npcNetworkId) && bs->Serialize(write, flags);
        }

        bool IsSane() const {
            return valid && sessionId != 0;
        }

        bool Has(VendorFlags flag) const {
            return (flags & static_cast<std::uint8_t>(flag)) != 0;
        }
    };

    /**
     * Server -> client: what the vendor sells and buys, and what it holds.
     *
     * `revision` is not decoration. The client echoes it with its basket and
     * the server refuses a basket priced against any other list -- otherwise a
     * deal struck while another player emptied the shelf would be settled at
     * prices, or against stock, the vendor no longer has.
     */
    struct VendorStock {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::VendorStock");

        std::uint32_t sessionId = 0;
        std::uint32_t revision  = 0;

        /** Money units the vendor holds. Meaningless for a bottomless purse. */
        std::uint32_t purse = 0;

        std::vector<VendorStockRow> stock;
        std::vector<VendorBuyRow> buys;
        bool valid = true;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            valid = bs->Serialize(write, sessionId) && bs->Serialize(write, revision) && bs->Serialize(write, purse) && Detail::SerializeVendorRows(bs, write, stock, kMaxVendorRows) && Detail::SerializeVendorRows(bs, write, buys, kMaxVendorRows);
        }

        bool IsSane() const {
            return valid && sessionId != 0 && revision != 0 && Detail::AreVendorRowsSane(stock, kMaxVendorRows) && Detail::AreVendorRowsSane(buys, kMaxVendorRows);
        }
    };

    /**
     * Client -> server: the player pressed Trade.
     *
     * A proposal, not a transfer. The screen's own commit never runs, so
     * nothing has moved on the client when this goes out, and the server
     * re-prices every line against its own lists before anything does.
     */
    struct VendorBasket {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::VendorBasket");

        std::uint32_t sessionId = 0;
        std::uint32_t revision  = 0;
        std::vector<VendorBasketLine> lines;
        /**
         * The server rows the sell lines hand over, one entry per row, read
         * from the owner's projection by the kind of each basket item. The
         * server settles these and nothing else, so the player sells the
         * rusty sword they put down -- a kind of its own, by its health --
         * rather than whichever one the server would pick.
         */
        std::vector<VendorSoldUnit> soldUnits;
        bool valid = true;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            valid = bs->Serialize(write, sessionId) && bs->Serialize(write, revision) && Detail::SerializeVendorRows(bs, write, lines, kMaxVendorBasketLines) && Detail::SerializeVendorRows(bs, write, soldUnits, kMaxVendorSoldUnits);
        }

        bool IsSane() const {
            return valid && sessionId != 0 && !lines.empty() && Detail::AreVendorRowsSane(lines, kMaxVendorBasketLines) && Detail::AreVendorRowsSane(soldUnits, kMaxVendorSoldUnits);
        }
    };

    /** Server -> client: how one basket was settled. */
    struct VendorResult {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::VendorResult");

        std::uint32_t sessionId = 0;
        std::uint8_t outcome    = static_cast<std::uint8_t>(VendorOutcome::Accepted);
        bool valid              = true;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            valid = bs->Serialize(write, sessionId) && bs->Serialize(write, outcome);
        }

        bool IsSane() const {
            return valid && sessionId != 0 && outcome <= static_cast<std::uint8_t>(VendorOutcome::InventoryFull);
        }
    };

    /**
     * Either way: this session is over.
     *
     * Server to client it is an instruction to take the screen down; client to
     * server it is a report that the player walked away from it.
     */
    struct VendorClose {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::VendorClose");

        std::uint32_t sessionId = 0;
        std::uint8_t reason     = static_cast<std::uint8_t>(VendorCloseReason::Closed);
        bool valid              = true;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            valid = bs->Serialize(write, sessionId) && bs->Serialize(write, reason);
        }

        bool IsSane() const {
            return valid && sessionId != 0 && reason <= static_cast<std::uint8_t>(VendorCloseReason::Interrupted);
        }
    };
} // namespace KCDC::Shared::RPC
