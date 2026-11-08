#pragma once

#include "shared/features/animation/animation_playback.h"
#include "shared/features/animation/face_state.h"

#include <mafianet/bit_stream.h>
#include <networking/replication/network_entity.h>
#include <networking/rpc/rpc_identifier.h>

namespace KCDC::Shared::RPC {
    /**
     * What the server wants a player's own body to play.
     *
     * Everybody else reads it off `HumanEntity::animation`, but that field is
     * server-only and an entity's owner is sent neither replication channel,
     * so the one peer whose body it is has to be told directly. The payload
     * is the same struct, whole, so the owner and its observers cannot read
     * two different requests.
     */
    struct PlayerAnimation {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::PlayerAnimation");

        Shared::Game::AnimationPlayback playback;

        bool valid = true;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            Framework::Networking::Replication::FieldSerializer fields(bs, write);
            playback.Serialize(fields);
            valid = fields.Good();
        }

        bool IsSane() const {
            return valid && playback.IsSane();
        }
    };

    /**
     * What the server wants a player's own face to do. Told directly for the
     * reason `PlayerAnimation` is: everybody else reads `HumanEntity::face`,
     * a field the owner is never sent.
     */
    struct PlayerFace {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::PlayerFace");

        Shared::Game::FaceState face;

        bool valid = true;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            Framework::Networking::Replication::FieldSerializer fields(bs, write);
            face.Serialize(fields);
            valid = fields.Good();
        }

        bool IsSane() const {
            return valid && face.IsSane();
        }
    };

    /**
     * The animation a request made a body play has ended, as seen by the one
     * client whose body counts: a player's owner, or an NPC's simulator.
     *
     * Sent once per request from the Mannequin edge that ended it, never
     * from a status read. The server checks the sender against the entity
     * and `revision` against the request still current, and raises
     * `playerAnimationEnd` / `npcAnimationEnd` from it.
     */
    struct AnimationEnded {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::AnimationEnded");

        std::uint64_t entityNetworkId = 0;
        std::uint32_t revision        = 0;
        Shared::Game::AnimationEndReason reason = Shared::Game::AnimationEndReason::Finished;

        bool valid = true;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            Framework::Networking::Replication::FieldSerializer fields(bs, write);
            auto wireReason = static_cast<std::uint8_t>(reason);
            fields.Field(entityNetworkId);
            fields.Field(revision);
            fields.Field(wireReason);
            if (!write) {
                reason = wireReason < static_cast<std::uint8_t>(Shared::Game::AnimationEndReason::Count) ? static_cast<Shared::Game::AnimationEndReason>(wireReason) : Shared::Game::AnimationEndReason::Count;
            }
            valid = fields.Good();
        }

        bool IsSane() const {
            return valid && entityNetworkId != 0 && revision != 0 && Shared::Game::IsReportableAnimationEnd(reason);
        }
    };
} // namespace KCDC::Shared::RPC
