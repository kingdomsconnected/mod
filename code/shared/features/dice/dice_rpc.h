#pragma once

#include "shared/features/dice/dice_rules.h"

#include <mafianet/bit_stream.h>
#include <networking/rpc/rpc_identifier.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>
#include <string>
#include <array>

namespace KCDC::Shared::RPC {
    /**
     * The most throws one message deals: a throw, and every throw that
     * follows it with no decision in between -- a bust hands the turn over and
     * the next seat's opening throw is dealt with it, which can bust again.
     */
    inline constexpr std::size_t kMaxDiceDeals = 16;

    /** One throw's faces, for the dice of `seat` not set aside, in die order. */
    struct DiceDeal {
        std::uint8_t seat = 0;
        std::vector<std::uint8_t> faces;

        bool Serialize(MafiaNet::BitStream *bs, bool write) {
            std::uint8_t count = static_cast<std::uint8_t>(faces.size());
            if (!bs->Serialize(write, seat) || !bs->Serialize(write, count)) {
                return false;
            }
            if (!write) {
                if (count > Game::Dice::kDiceCount) {
                    return false;
                }
                faces.assign(count, 0);
            }
            for (std::uint8_t &face : faces) {
                if (!bs->Serialize(write, face)) {
                    return false;
                }
            }
            return true;
        }

        bool IsSane() const {
            if (seat > 1 || faces.empty() || faces.size() > Game::Dice::kDiceCount) {
                return false;
            }
            for (const std::uint8_t face : faces) {
                if (face >= Game::Dice::kFaceCount) {
                    return false;
                }
            }
            return true;
        }
    };

    namespace DiceDetail {
        inline bool ValidName(const std::string &name) {
            if (name.size() > 64) {
                return false;
            }
            for (unsigned char ch : name) {
                if (ch < 32 || ch == 127) {
                    return false;
                }
            }
            return true;
        }
        inline bool SerializeName(MafiaNet::BitStream *bs, bool write, std::string &name) {
            std::uint8_t size = static_cast<std::uint8_t>(name.size());
            if ((write && !ValidName(name)) || !bs->Serialize(write, size) || size > 64) {
                return false;
            }
            if (!write) {
                name.resize(size);
            }
            for (char &ch : name) {
                if (!bs->Serialize(write, ch)) {
                    return false;
                }
            }
            return ValidName(name);
        }

        inline bool SerializeDeals(MafiaNet::BitStream *bs, bool write, std::vector<DiceDeal> &deals) {
            std::uint8_t count = static_cast<std::uint8_t>(deals.size());
            if (write && deals.size() > kMaxDiceDeals) {
                return false;
            }
            if (!bs->Serialize(write, count)) {
                return false;
            }
            if (!write) {
                if (count > kMaxDiceDeals) {
                    return false;
                }
                deals.assign(count, DiceDeal {});
            }
            for (DiceDeal &deal : deals) {
                if (!deal.Serialize(bs, write)) {
                    return false;
                }
            }
            return true;
        }

        inline bool AreDealsSane(const std::vector<DiceDeal> &deals) {
            if (deals.size() > kMaxDiceDeals) {
                return false;
            }
            for (const DiceDeal &deal : deals) {
                if (!deal.IsSane()) {
                    return false;
                }
            }
            return true;
        }
    } // namespace DiceDetail

    /**
     * Server -> each of the two players: a match is on.
     *
     * Seat 0 sits on the table's `seat1` tag point and seat 1 on `seat2`, on
     * every machine; a client whose player is seat 1 swaps its table's sides.
     * The table is the dice table nearest `anchor` -- both clients load the
     * same level, so both find the same one. `deals` is the opening throw and
     * any that follow it before a decision.
     */
    struct DiceNames {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::DiceNames");
        std::uint32_t matchId                    = 0;
        std::array<std::string, 2> names;
        bool valid = true;
        void Serialize(MafiaNet::BitStream *bs, bool write) {
            valid = bs->Serialize(write, matchId) && DiceDetail::SerializeName(bs, write, names[0]) && DiceDetail::SerializeName(bs, write, names[1]);
        }
        bool IsSane() const {
            return valid && matchId != 0 && DiceDetail::ValidName(names[0]) && DiceDetail::ValidName(names[1]);
        }
    };

    struct DiceMatchStart {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::DiceMatchStart");

        std::uint32_t matchId           = 0;
        std::uint8_t localSeat          = 0;
        std::uint8_t firstSeat          = 0;
        std::uint64_t opponentNetworkId = 0;
        std::uint32_t targetScore       = 0;
        float anchorX                   = 0.0F;
        float anchorY                   = 0.0F;
        float anchorZ                   = 0.0F;
        std::vector<DiceDeal> deals;
        std::array<std::string, 2> names;
        bool valid = true;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            valid = bs->Serialize(write, matchId) && bs->Serialize(write, localSeat) && bs->Serialize(write, firstSeat) && bs->Serialize(write, opponentNetworkId) && bs->Serialize(write, targetScore) && bs->Serialize(write, anchorX) && bs->Serialize(write, anchorY)
                    && bs->Serialize(write, anchorZ) && DiceDetail::SerializeDeals(bs, write, deals) && DiceDetail::SerializeName(bs, write, names[0]) && DiceDetail::SerializeName(bs, write, names[1]);
        }

        bool IsSane() const {
            return valid && matchId != 0 && localSeat <= 1 && firstSeat <= 1 && opponentNetworkId != 0 && targetScore != 0 && std::isfinite(anchorX) && std::isfinite(anchorY) && std::isfinite(anchorZ) && !deals.empty() && DiceDetail::AreDealsSane(deals)
                   && DiceDetail::ValidName(names[0]) && DiceDetail::ValidName(names[1]);
        }
    };

    /**
     * Player -> server: what I want to do with the dice I hold.
     *
     * The client's game does nothing until the server commits it: the press
     * is held back, so the next throw is never on this machine before the
     * decision that leads to it.
     */
    struct DiceMove {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::DiceMove");

        std::uint32_t matchId = 0;

        /** How many moves of this match the client has applied; a move made against an older table is refused. */
        std::uint32_t moveIndex = 0;
        std::uint8_t kind       = 0;
        std::uint8_t heldMask   = 0;
        bool valid              = true;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            valid = bs->Serialize(write, matchId) && bs->Serialize(write, moveIndex) && bs->Serialize(write, kind) && bs->Serialize(write, heldMask);
        }

        bool IsSane() const {
            return valid && matchId != 0 && kind <= static_cast<std::uint8_t>(Game::Dice::MoveKind::Pass) && (heldMask & ~Game::Dice::kAllDiceMask) == 0;
        }
    };

    /**
     * Server -> both players: a move happened, and the throws it leads to.
     *
     * The mover's client plays it through its own key handler, the other's
     * replays it on the opponent seat; both hold the faces for the throws
     * that follow until their engine rolls.
     */
    struct DiceCommit {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::DiceCommit");

        std::uint32_t matchId   = 0;
        std::uint32_t moveIndex = 0;
        std::uint8_t seat       = 0;
        std::uint8_t kind       = 0;
        std::uint8_t heldMask   = 0;
        std::uint32_t total0    = 0;
        std::uint32_t total1    = 0;
        std::vector<DiceDeal> deals;
        bool valid = true;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            valid = bs->Serialize(write, matchId) && bs->Serialize(write, moveIndex) && bs->Serialize(write, seat) && bs->Serialize(write, kind) && bs->Serialize(write, heldMask) && bs->Serialize(write, total0) && bs->Serialize(write, total1) && DiceDetail::SerializeDeals(bs, write, deals);
        }

        bool IsSane() const {
            return valid && matchId != 0 && seat <= 1 && kind <= static_cast<std::uint8_t>(Game::Dice::MoveKind::Pass) && (heldMask & ~Game::Dice::kAllDiceMask) == 0 && DiceDetail::AreDealsSane(deals);
        }
    };

    /** Server -> the mover: that move was not legal against the table as it stands. */
    struct DiceRefuse {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::DiceRefuse");

        std::uint32_t matchId   = 0;
        std::uint32_t moveIndex = 0;
        bool valid              = true;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            valid = bs->Serialize(write, matchId) && bs->Serialize(write, moveIndex);
        }

        bool IsSane() const {
            return valid && matchId != 0;
        }
    };

    /** Player -> server: my side of the match is over -- I gave up, or my game could not play it. */
    struct DiceLeave {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::DiceLeave");

        std::uint32_t matchId = 0;
        std::uint8_t reason   = 0;
        bool valid            = true;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            valid = bs->Serialize(write, matchId) && bs->Serialize(write, reason);
        }

        bool IsSane() const {
            return valid && matchId != 0 && reason <= static_cast<std::uint8_t>(Game::Dice::EndReason::Stopped);
        }
    };

    /** Server -> both players: the match is over. `winnerSeat` is `kNoSeat` when nobody won. */
    struct DiceMatchEnd {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::DiceMatchEnd");

        std::uint32_t matchId  = 0;
        std::uint8_t reason    = 0;
        std::uint8_t winnerSeat = Game::Dice::kNoSeat;
        std::uint32_t total0   = 0;
        std::uint32_t total1   = 0;
        bool valid             = true;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            valid = bs->Serialize(write, matchId) && bs->Serialize(write, reason) && bs->Serialize(write, winnerSeat) && bs->Serialize(write, total0) && bs->Serialize(write, total1);
        }

        bool IsSane() const {
            return valid && matchId != 0 && reason <= static_cast<std::uint8_t>(Game::Dice::EndReason::Stopped) && (winnerSeat <= 1 || winnerSeat == Game::Dice::kNoSeat);
        }
    };
} // namespace KCDC::Shared::RPC
