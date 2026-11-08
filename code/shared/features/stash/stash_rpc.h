#pragma once

#include "shared/item_stack.h"

#include <mafianet/bit_stream.h>
#include <networking/rpc/rpc_identifier.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace KCDC::Shared::RPC {
    /** The rows one container holds at most. */
    inline constexpr std::size_t kMaxStashItems = 128;

    /** The server answers by locking the stash to this player. */
    struct StashOpened {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::StashOpened");

        std::uint64_t stashNetworkId = 0;
        bool valid                   = true;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            valid = bs->Serialize(write, stashNetworkId);
        }

        bool IsSane() const {
            return valid && stashNetworkId != 0;
        }
    };

    /** The player closed it; the server releases it to the next one. */
    struct StashClosed {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::StashClosed");

        std::uint64_t stashNetworkId = 0;
        bool valid                   = true;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            valid = bs->Serialize(write, stashNetworkId);
        }

        bool IsSane() const {
            return valid && stashNetworkId != 0;
        }
    };

    /** What a player asks to do to a container, in the words a script sees. */
    enum class StashAction : std::uint8_t {
        Open,
        Unlock,
        Lockpick
    };

    inline const char *StashActionName(StashAction action) {
        switch (action) {
        case StashAction::Open: return "open";
        case StashAction::Unlock: return "unlock";
        case StashAction::Lockpick: return "lockpick";
        }
        return "invalid";
    }

    inline bool IsStashAction(StashAction action) {
        return action >= StashAction::Open && action <= StashAction::Lockpick;
    }

    /**
     * Client -> server, before anything happens: the local player pressed a
     * container's prompt. The client holds the game's own handler until the
     * verdict arrives, so a refused open never shows the transfer screen.
     */
    struct StashInteractRequest {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::StashInteractRequest");

        std::uint64_t stashNetworkId = 0;
        std::uint32_t requestId      = 0;
        StashAction action           = StashAction::Open;
        bool valid                   = true;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            std::uint8_t wire = static_cast<std::uint8_t>(action);
            valid             = bs->Serialize(write, stashNetworkId) && bs->Serialize(write, requestId) && bs->Serialize(write, wire);
            if (!write) {
                action = static_cast<StashAction>(wire);
            }
        }

        bool IsSane() const {
            return valid && stashNetworkId != 0 && requestId != 0 && IsStashAction(action);
        }
    };

    /** Server -> the asking client: whether to run the handler it is holding. */
    struct StashInteractVerdict {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::StashInteractVerdict");

        std::uint64_t stashNetworkId = 0;
        std::uint32_t requestId      = 0;
        bool allowed                 = false;
        bool valid                   = true;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            valid = bs->Serialize(write, stashNetworkId) && bs->Serialize(write, requestId) && bs->Serialize(write, allowed);
        }

        bool IsSane() const {
            return valid && stashNetworkId != 0 && requestId != 0;
        }
    };

    /** How a granted unlock ended on the client. */
    enum class StashLockEvent : std::uint8_t {
        Unlocked,
        Lockpicked
    };

    /**
     * Client -> server: the container's own Lua unlocked it -- a key turned,
     * or the lockpick minigame succeeded. Accepted only against a grant the
     * server gave this connection for the same container.
     */
    struct StashLockReport {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::StashLockReport");

        std::uint64_t stashNetworkId = 0;
        StashLockEvent event         = StashLockEvent::Unlocked;
        bool valid                   = true;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            std::uint8_t wire = static_cast<std::uint8_t>(event);
            valid             = bs->Serialize(write, stashNetworkId) && bs->Serialize(write, wire);
            if (!write) {
                event = static_cast<StashLockEvent>(wire);
            }
        }

        bool IsSane() const {
            return valid && stashNetworkId != 0 && (event == StashLockEvent::Unlocked || event == StashLockEvent::Lockpicked);
        }
    };
} // namespace KCDC::Shared::RPC
