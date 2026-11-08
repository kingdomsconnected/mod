#pragma once

#include "shared/features/vendor/vendor_rpc.h"
#include "shared/game/item_classes.h"

#include <mafianet/types.h>

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace KCDC::Features::Vendor {
    /** One thing a vendor sells. */
    struct StockRow {
        const Shared::Game::ItemClassEntry *item = nullptr;
        std::uint32_t amount                     = 0;
        std::uint32_t price                      = 0;
        nlohmann::json metadata                  = nlohmann::json::object();
    };

    /** One thing a vendor buys. */
    struct BuyRow {
        const Shared::Game::ItemClassEntry *item = nullptr;
        std::uint32_t price                      = 0;
    };

    /** One settled line, as the `vendorTrade` event reports it. */
    struct TradeLine {
        const Shared::Game::ItemClassEntry *item = nullptr;
        std::uint32_t amount                     = 0;
        std::uint32_t price                      = 0;
        nlohmann::json metadata                  = nlohmann::json::object();
    };

    /**
     * Server authority for vendors: what each sells and buys, what it holds,
     * and every deal struck at one.
     *
     * A vendor is attached to nothing. It is a price list and a purse, and a
     * session puts one in front of a player on the game's own trade screen,
     * with an NPC's soul keeping the shop when the script names one. That
     * keeps the stall, the smith and the wandering pedlar the same object:
     * where the counter is, and who stands behind it, is the gamemode's
     * business.
     *
     * The client's screen is a preview. It prices from the lists sent here and
     * turns Trade into a basket, and the native commit never runs -- so no
     * item and no coin moves on the client until this has settled the basket
     * against its own lists.
     *
     * A deal is one commit on the player's inventory: the exact units
     * they sold and the money they owe leave, what they bought and any change
     * arrive, or nothing happens. The owner's projection then shows the new
     * revision. The vendor's lists and purse are script state and move on
     * only after that commit.
     *
     * Several players may browse one vendor at once. Every deal moves the
     * list revision on, so a basket priced against a shelf somebody else has
     * since emptied is refused rather than half-honoured.
     */
    class VendorService final {
      public:
        static VendorService &Get();

        /** Registers the RPC handlers. Idempotent. */
        void Install();

        /** Creates a vendor with empty lists. A purse of `std::nullopt` never runs out. */
        std::uint32_t Create(std::string name, std::optional<std::uint32_t> purse, bool buys);

        /** Ends every session at the vendor and forgets it. */
        bool Destroy(std::uint32_t vendorId);

        /** Replaces what the vendor sells. False for a vendor that does not exist or a list too long for the wire. */
        bool SetStock(std::uint32_t vendorId, std::vector<StockRow> stock);

        /** Replaces what the vendor buys. */
        bool SetBuys(std::uint32_t vendorId, std::vector<BuyRow> buys);

        bool SetPurse(std::uint32_t vendorId, std::optional<std::uint32_t> purse);
        std::optional<std::uint32_t> GetPurse(std::uint32_t vendorId) const;

        bool Exists(std::uint32_t vendorId) const {
            return _vendors.find(vendorId) != _vendors.end();
        }

        /**
         * Puts the vendor's trade screen in front of a player.
         *
         * `npcNetworkId` names the NPC whose soul keeps the shop; 0 trades with
         * nobody in particular. A player already trading has that session
         * closed first, because the game has one trade screen.
         *
         * Returns the session id, or 0 when the vendor or the player is gone.
         */
        std::uint32_t Open(std::uint32_t vendorId, std::uint64_t playerId, std::uint64_t npcNetworkId);

        /** Ends a session and takes the screen down. */
        bool Close(std::uint32_t sessionId, Shared::RPC::VendorCloseReason reason);

        /** The session a player is in, or 0. */
        std::uint32_t SessionOf(std::uint64_t playerId) const;

        /** The vendor a session trades with, or 0. */
        std::uint32_t VendorOf(std::uint32_t sessionId) const;

        /** Drops a player's session without messaging them; for disconnects. */
        void Forget(std::uint64_t playerId);

      private:
        struct Vendor {
            std::string name;
            std::optional<std::uint32_t> purse;

            /** Off turns selling off entirely, whatever `buys` lists. */
            bool buysFromPlayers = true;

            std::vector<StockRow> stock;
            std::vector<BuyRow> buys;

            /** Moves on with every change a basket could have been priced against. */
            std::uint32_t revision = 1;
        };

        struct Session {
            std::uint32_t vendorId     = 0;
            std::uint64_t playerId     = 0;
            std::uint64_t npcNetworkId = 0;
        };

        struct Settlement;

        void OnBasket(const Shared::RPC::VendorBasket &basket, MafiaNet::Packet *packet);
        void OnClose(const Shared::RPC::VendorClose &message, MafiaNet::Packet *packet);

        /** Re-prices a basket against the vendor's own lists into `settlement`; anything but Accepted refuses it. */
        Shared::RPC::VendorOutcome Price(const Vendor &vendor, const Shared::RPC::VendorBasket &basket, Settlement &settlement) const;

        /** Commits a priced basket to the player's inventory; anything but Accepted moved nothing. */
        Shared::RPC::VendorOutcome Settle(const Shared::RPC::VendorBasket &basket, Settlement &settlement);

        /** Moves the vendor's lists and purse on after a committed deal, and reports it. */
        void Settled(const Settlement &settlement);

        void Changed(std::uint32_t vendorId, Vendor &vendor);
        void SendStock(std::uint32_t sessionId, const Session &session, const Vendor &vendor) const;
        void SendResult(std::uint64_t playerId, std::uint32_t sessionId, Shared::RPC::VendorOutcome outcome) const;
        Vendor *Find(std::uint32_t vendorId);
        Session *FindSession(std::uint32_t sessionId);

        std::unordered_map<std::uint32_t, Vendor> _vendors;
        std::unordered_map<std::uint32_t, Session> _sessions;
        std::unordered_map<std::uint64_t, std::uint32_t> _byPlayer;
        std::uint32_t _nextVendor  = 1;
        std::uint32_t _nextSession = 1;
        bool _installed            = false;
    };
} // namespace KCDC::Features::Vendor
