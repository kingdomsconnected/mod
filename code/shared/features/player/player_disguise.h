#pragma once

#include "shared/streamed_assets/streamed_asset_policy.h"
#include "shared/game/prop_models.h"

#include <networking/replication/network_entity.h>

#include <mafianet/bit_stream.h>
#include <networking/rpc/rpc_identifier.h>

#include <cstddef>
#include <string>

namespace KCDC::Shared::Game {
    /**
     * A static mesh one player's body is drawn as instead of itself.
     *
     * Every copy of the body draws it the same way -- the owner's own, which
     * is the local player, and each puppet elsewhere: the character slot stops
     * rendering, the mesh goes into a slot of its own on the same entity, and
     * the body's collision cylinder is resized around the mesh. Nothing else
     * about the body changes. It still walks, fights, bleeds and dies as a
     * person, which is the point: the disguise is only what everyone sees and
     * what a trace meets.
     *
     * Empty is no disguise.
     */
    struct PlayerDisguise {
        /** Bounds what a peer can make a receiver store; the catalog's longest path is well inside it, and so is the longest streamed one. */
        static constexpr std::size_t kMaxModelLength = Framework::Utils::StreamedAssets::kMaxPathLength;

        /** An `objects/...cgf` path from the prop catalog, a streamed `objects/kcdc/<resource>/...cgf`, or empty. */
        std::string model;

        bool IsEmpty() const {
            return model.empty();
        }

        bool operator==(const PlayerDisguise &other) const = default;

        // One body for the replicated field and the RPC; see CharacterAppearance.
        void Serialize(Framework::Networking::Replication::FieldSerializer &fields) {
            fields.Field(model);
        }

        /**
         * Whether the mesh is one the catalog carries, or has the shape of a
         * streamed one. Checked by every receiver rather than trusted:
         * `LoadGeometry` on a path the paks do not have leaves the body
         * invisible and its collider unchanged, so a receiver also waits for
         * a streamed mesh to be mounted before it loads it.
         */
        bool IsSane() const {
            return model.empty() || (model.size() <= kMaxModelLength && (FindPropModel(model) != nullptr || IsStreamed()));
        }

        bool IsStreamed() const {
            return KCDC::Shared::StreamedAssets::Policy::IsStreamedModelPath(model);
        }
    };
} // namespace KCDC::Shared::Game

namespace KCDC::Shared::RPC {
    /**
     * What the server wants one player's body drawn as, sent only to that
     * player. The owner is authoritative for its own body, so this is a
     * request it republishes as `HumanEntity::disguise`, the way an
     * appearance is.
     */
    struct SetPlayerDisguise {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::SetPlayerDisguise");

        Shared::Game::PlayerDisguise disguise;

        bool valid = true;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            Framework::Networking::Replication::FieldSerializer fields(bs, write);
            disguise.Serialize(fields);
            valid = fields.Good();
        }

        bool IsSane() const {
            return valid && disguise.IsSane();
        }
    };
} // namespace KCDC::Shared::RPC
