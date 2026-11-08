#pragma once

#include "shared/features/player/player_combat_reaction.h"

#include <mafianet/bit_stream.h>
#include <mafianet/string.h>
#include <networking/replication/network_entity.h>
#include <networking/rpc/rpc_identifier.h>

#include <cstdint>
#include <string>

namespace KCDC::Shared::RPC {
    // NPCs have their own sender identity; player presentation admission must
    // never replace it with the connection's player.
    struct NpcCombatReaction {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::NpcCombatReaction");
        CombatReaction reaction;
        void Serialize(MafiaNet::BitStream *bs, bool write) {
            reaction.Serialize(bs, write);
        }
    };

    /**
     * Client -> server: the local player pressed use on a server-owned NPC.
     *
     * An event rather than a state: an interaction happens once. The server
     * fills in who did it from the sending connection, so a client can only
     * ever speak for itself.
     */
    struct NpcInteract {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::NpcInteract");

        std::uint64_t npcNetworkId = 0;

        /** Filled in by the server, never by a client. */
        std::uint64_t playerNetworkId = 0;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            Framework::Networking::Replication::FieldSerializer fields(bs, write);
            fields.Field(npcNetworkId);
            fields.Field(playerNetworkId);
            _complete = fields.Good();
        }

        bool IsSane() const {
            return _complete && npcNetworkId != 0;
        }

      private:
        bool _complete = true;
    };

    /**
     * Client -> server: the local player's blow on an NPC copy that landed
     * natively here. The referee settles contacts on NPCs whose combat
     * actor is built and arrows on simulated NPCs; this carries the rest.
     */
    struct NpcDamage {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::NpcDamage");

        std::uint64_t npcNetworkId = 0;

        /** The NPC life the blow landed on; a blow on the previous one is dropped. */
        std::uint32_t victimEpoch = 0;

        /** Health the attacker's own calculation took off. */
        float amount = 0.0F;

        /** Bounds what one message may claim; a bigger hit arrives as several. */
        static constexpr float kMaxAmount = 10000.0F;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            Framework::Networking::Replication::FieldSerializer fields(bs, write);
            fields.Field(npcNetworkId);
            fields.Field(victimEpoch);
            fields.Field(amount);
            _complete = fields.Good();
        }

        bool IsSane() const {
            return _complete && npcNetworkId != 0 && amount > 0.0F && amount <= kMaxAmount;
        }

      private:
        bool _complete = true;
    };

    /**
     * Server -> clients: something this NPC does once.
     *
     * Everything an NPC *is* travels as replicated state; this is for the
     * things that only happen -- a gesture, a line, a flinch. Carrying a
     * sequence rather than relying on arrival lets a receiver that has already
     * played one ignore its repeat, and a receiver that joined later ignore all
     * of them.
     */
    struct NpcAction {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::NpcAction");

        enum Kind : std::uint8_t {
            /** Play an emote from the shipped catalog. */
            Emote = 0,
            /** Say a line: `text` over the body, through the game's own speech. */
            Speak = 1,
            /** Flinch, without the ledger having changed. */
            Flinch = 2,
            Count,
        };

        std::uint64_t npcNetworkId = 0;
        std::uint8_t kind          = Emote;
        std::uint16_t animationId  = 0;
        std::uint32_t sequence     = 0;
        std::string text;

        /** Bounds what one line may cost a receiver to hold. */
        static constexpr std::size_t kMaxTextLength = 256;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            Framework::Networking::Replication::FieldSerializer fields(bs, write);
            fields.Field(npcNetworkId);
            fields.Field(kind);
            fields.Field(animationId);
            fields.Field(sequence);
            MafiaNet::RakString wireText(text.c_str());
            fields.Field(wireText);
            if (!write) {
                text = wireText.C_String();
            }
            _complete = fields.Good();
        }

        bool IsSane() const {
            return _complete && npcNetworkId != 0 && kind < Count && text.size() <= kMaxTextLength;
        }

      private:
        bool _complete = true;
    };
} // namespace KCDC::Shared::RPC
