#pragma once

#include <cstdint>
#include <mafianet/bit_stream.h>
#include <networking/rpc/rpc_identifier.h>
#include <networking/replication/network_entity.h>
#include <string>

namespace KCDC::Shared::RPC {
    /** The most rows the native choice list is asked to carry in one page. */
    inline constexpr std::size_t kMaxDialogueOptions = 8;

    /** Why a conversation ended, as both sides agree to spell it. */
    enum class DialogueCloseReason : std::uint8_t {
        Completed   = 0,
        Cancelled   = 1,
        Replaced    = 2,
        Interrupted = 3,
    };

    /**
     * Server -> client: draw this page of a conversation.
     *
     * A conversation is a session on one player and is attached to nothing else:
     * whatever it is about lives in the gamemode's own code, so nothing here
     * names an entity.
     *
     * `pageId` is not decoration. The client echoes it with the choice, and the
     * server drops any choice whose page it is no longer showing -- otherwise a
     * player who mashes confirm while the next page is in flight applies their
     * answer to a page that no longer exists.
     *
     * The rows are a fixed run rather than a list, which is how every other
     * bounded payload in this project goes over the wire.
     */
    struct DialogueShowPage {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::DialogueShowPage");

        std::uint32_t sessionId = 0;
        std::uint32_t pageId    = 0;

        /** Which side of the screen the list draws on. */
        bool onRight = false;

        /** The line above the options; empty draws none. */
        std::string line;

        std::uint8_t optionCount = 0;
        std::string optionText[kMaxDialogueOptions];
        bool optionEnabled[kMaxDialogueOptions] {};

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            Framework::Networking::Replication::FieldSerializer fields(bs, write);
            fields.Field(sessionId);
            fields.Field(pageId);
            fields.Field(onRight);
            fields.Field(line);
            fields.Field(optionCount);
            // Every slot, every time: a count-driven loop would let a hostile
            // count walk the reader off the end of the message.
            for (std::size_t index = 0; index < kMaxDialogueOptions; ++index) {
                fields.Field(optionText[index]);
                fields.Field(optionEnabled[index]);
            }
            _complete = fields.Good();
        }

        bool IsSane() const {
            return _complete && sessionId != 0 && optionCount > 0 && optionCount <= kMaxDialogueOptions;
        }

      private:
        bool _complete = true;
    };

    /** Client -> server: the player picked an option on the page they were shown. */
    struct DialogueChoice {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::DialogueChoice");

        std::uint32_t sessionId  = 0;
        std::uint32_t pageId     = 0;
        std::uint8_t optionIndex = 0;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            Framework::Networking::Replication::FieldSerializer fields(bs, write);
            fields.Field(sessionId);
            fields.Field(pageId);
            fields.Field(optionIndex);
            _complete = fields.Good();
        }

        bool IsSane() const {
            return _complete && sessionId != 0 && optionIndex < kMaxDialogueOptions;
        }

      private:
        bool _complete = true;
    };

    /**
     * Either way: this conversation is over.
     *
     * Server to client it is an instruction; client to server it is a report,
     * which is how a cancel or a lost HUD reaches the gamemode.
     */
    struct DialogueClose {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::DialogueClose");

        std::uint32_t sessionId = 0;
        std::uint8_t reason     = static_cast<std::uint8_t>(DialogueCloseReason::Completed);

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            Framework::Networking::Replication::FieldSerializer fields(bs, write);
            fields.Field(sessionId);
            fields.Field(reason);
            _complete = fields.Good();
        }

        bool IsSane() const {
            return _complete && sessionId != 0 && reason <= static_cast<std::uint8_t>(DialogueCloseReason::Interrupted);
        }

      private:
        bool _complete = true;
    };
} // namespace KCDC::Shared::RPC
