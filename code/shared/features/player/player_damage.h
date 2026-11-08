#pragma once

#include <mafianet/bit_stream.h>
#include <networking/replication/network_entity.h>
#include <networking/rpc/rpc_identifier.h>

#include <cmath>
#include <cstdint>
#include <string_view>

namespace KCDC::Shared::Game {
    /**
     * Why a body lost health, in the engine's own numbering.
     *
     * The value is what the game writes into a damage effect
     * (`C_HealthValueEffect +0x44`) and hands to its kill attribution; the
     * spelling is the `DR_*` table at WHGame.dll 0x1817CADDC. Carried as the
     * engine's number rather than remapped, so a new reason in a patch arrives
     * as a number this build prints as `unknown` instead of a wrong name.
     */
    enum class DamageReason : std::uint8_t {
        Unknown      = 0,
        Combat       = 1, // a melee blow, and a bow or crossbow shot
        Gunshot      = 2, // DR_Combat_Gunshot: the missile path picks it off the weapon descriptor
        Starvation   = 3,
        Collision    = 4,
        Scripted     = 5,
        Disintegrate = 6,
        Fall         = 7,
        Poison       = 8,
        Bleeding     = 9,
        SelfHarm     = 10,
    };

    inline constexpr std::uint8_t kMaxDamageReason = static_cast<std::uint8_t>(DamageReason::SelfHarm);

    /** The spelling the scripting API uses. */
    inline constexpr std::string_view DamageReasonName(DamageReason reason) {
        switch (reason) {
        case DamageReason::Combat: return "combat";
        case DamageReason::Gunshot: return "gunshot";
        case DamageReason::Starvation: return "starvation";
        case DamageReason::Collision: return "collision";
        case DamageReason::Scripted: return "scripted";
        case DamageReason::Disintegrate: return "disintegrate";
        case DamageReason::Fall: return "fall";
        case DamageReason::Poison: return "poison";
        case DamageReason::Bleeding: return "bleeding";
        case DamageReason::SelfHarm: return "selfHarm";
        default: return "unknown";
        }
    }

    /** The engine's number as a reason; one this build has no name for reads as `Unknown`. */
    inline constexpr DamageReason ToDamageReason(std::uint32_t engineReason) {
        return engineReason <= kMaxDamageReason ? static_cast<DamageReason>(engineReason) : DamageReason::Unknown;
    }
} // namespace KCDC::Shared::Game

namespace KCDC::Shared::RPC {
    /**
     * Owner -> server: the local body just lost health to something.
     *
     * The hit player's own client is the only one that knows: it resolved the
     * blow against its own body, so the amount is what was really taken, after
     * armour and the defender's own skill -- the numbers the protocol keeps
     * local. Sent before the write that applies it, so a killing blow reaches
     * the server ahead of the death it causes.
     *
     * The server takes the victim from the sending connection. The attacker
     * is this client's claim, which the server checks names a connected player
     * other than the sender.
     */
    struct PlayerDamaged {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::PlayerDamaged");

        /** The player whose blow it was, as this client saw it; 0 for nobody, or for a body that is not a player. */
        std::uint64_t attackerNetworkId = 0;

        /** Health taken, never negative. */
        float amount = 0.0F;

        /** A `body_part.xml` id, or `Game::kNoBodyPart` when the damage landed nowhere in particular. */
        std::uint8_t bodyPart = 0;

        /** A `Game::DamageReason`. */
        std::uint8_t reason = 0;

        /** Bounds what one message may claim; soul health tops out far below. */
        static constexpr float kMaxAmount = 100000.0F;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            Framework::Networking::Replication::FieldSerializer fields(bs, write);
            fields.Field(attackerNetworkId);
            fields.Field(amount);
            fields.Field(bodyPart);
            fields.Field(reason);
            _complete = fields.Good();
        }

        bool IsSane() const {
            return _complete && std::isfinite(amount) && amount > 0.0F && amount <= kMaxAmount && reason <= Game::kMaxDamageReason;
        }

      private:
        bool _complete = true;
    };
} // namespace KCDC::Shared::RPC
