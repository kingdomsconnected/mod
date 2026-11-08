#pragma once

#include <mafianet/bit_stream.h>
#include <networking/rpc/rpc_identifier.h>

#include <cmath>

namespace KCDC::Shared::RPC {
    /**
     * Server -> owner: raise this body's health.
     *
     * Only the health half of `player.heal`. Injuries, poisons and bleeding
     * are buffs, and clearing them travels as the buff orders that already
     * exist -- the `remove_injuries` and `remove_all_posions` cures, and the
     * `bleed` tag -- so there is one way to take a status effect off, not two.
     *
     * Applied through `I_Soul::SetState`, the step the game's own full heal
     * (`utils/general/healing.xml`) takes, which is why it only ever raises:
     * a lower target would be damage from nobody, and a dead body is revived
     * by `player.revive`, not healed.
     */
    struct HealPlayer {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::HealPlayer");

        /** The health to raise to, clamped to the body's maximum; negative means all of it. */
        float health = -1.0F;

        bool valid = true;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            valid = bs->Serialize(write, health);
        }

        bool IsSane() const {
            return valid && std::isfinite(health);
        }
    };
} // namespace KCDC::Shared::RPC
