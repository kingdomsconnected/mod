#pragma once

#include "shared/network_config.h"

#include <mafianet/bit_stream.h>
#include <networking/rpc/rpc_identifier.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace KCDC::Shared::RPC {
    /** The longest nickname the frontend accepts, in bytes; a peer must never choose how much a receiver stores. */
    inline constexpr std::size_t kMaxScoreboardName = 63;

    /** Every connected player fits in one answer, and nothing past that is read. */
    inline constexpr std::size_t kMaxScoreboardRows = static_cast<std::size_t>(Network::MaxPlayers);

    /**
     * A client asks who is on the server.
     *
     * Asked, not pushed: a roster that follows every join to every client
     * costs the square of the player count on each one, and nobody is reading
     * it unless their board is open. So the client asks once when its session
     * starts and then only while the board is up.
     *
     * `knownRevision` is the roster the client already holds. When it is
     * still the server's, the answer carries pings and no names.
     */
    struct ScoreboardRequest {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::ScoreboardRequest");

        std::uint32_t knownRevision = 0;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            _complete = bs->Serialize(write, knownRevision);
        }

        bool IsSane() const {
            return _complete;
        }

      private:
        bool _complete = true;
    };

    /** One connected player: the connection slot names them, the name is empty in a pings-only answer. */
    struct ScoreboardRow {
        std::uint16_t slot = 0;

        /** Round trip in milliseconds, as the server measures it. */
        std::uint16_t ping = 0;
        std::string name;

        bool Serialize(MafiaNet::BitStream *bs, bool write, bool withName) {
            bool ok = bs->Serialize(write, slot) && bs->Serialize(write, ping);
            if (withName) {
                ok = ok && bs->Serialize(write, name);
            }
            return ok;
        }

        bool IsSane() const {
            return name.size() <= kMaxScoreboardName;
        }
    };

    /**
     * The answer: every connected player, and which of them is the asker.
     *
     * `names` false means the rows are the same set the asker already holds
     * under `revision`, in any order, and only the pings are new.
     */
    struct Scoreboard {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::Scoreboard");

        std::uint32_t revision   = 0;
        std::uint16_t localSlot  = 0;
        std::uint16_t maxPlayers = 0;
        bool names               = true;
        std::vector<ScoreboardRow> rows;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            _complete = bs->Serialize(write, revision) && bs->Serialize(write, localSlot) && bs->Serialize(write, maxPlayers) && bs->Serialize(write, names);
            if (!_complete) {
                return;
            }

            if (write && rows.size() > kMaxScoreboardRows) {
                _complete = false;
                return;
            }
            std::uint16_t count = write ? static_cast<std::uint16_t>(rows.size()) : 0;
            if (!bs->Serialize(write, count) || count > kMaxScoreboardRows) {
                _complete = false;
                return;
            }
            if (!write) {
                rows.assign(count, ScoreboardRow {});
            }
            for (ScoreboardRow &row : rows) {
                if (!row.Serialize(bs, write, names)) {
                    _complete = false;
                    return;
                }
            }
        }

        bool IsSane() const {
            if (!_complete || revision == 0) {
                return false;
            }
            for (const ScoreboardRow &row : rows) {
                if (!row.IsSane()) {
                    return false;
                }
            }
            return true;
        }

      private:
        bool _complete = true;
    };
} // namespace KCDC::Shared::RPC
