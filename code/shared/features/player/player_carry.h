#pragma once

#include <mafianet/bit_stream.h>
#include <networking/rpc/rpc_identifier.h>

#include "shared/item_stack.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>

namespace KCDC::Shared::RPC {
    /**
     * A client reporting that its player picked a body up or let one go.
     *
     * Edge-triggered, like `HorseMount`: the continuous answer to "who is
     * carrying whom" is the carrier's `HumanEntity::carriedNetworkId`, and
     * this is what moves it. Only the carried body is taken from the message
     * -- the carrier is the sending connection's own avatar, so a client can
     * only ever report its own shoulder.
     *
     * The carrier's game has already started the carry when this is sent:
     * its grab prompt is the game's own and gated there. What the server
     * adds is whether the gamemode allows it; a refusal comes back as
     * `PutDownCarried`.
     */
    struct PlayerCarry {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::PlayerCarry");

        /** The carried player's NetworkID. On a put-down this is the body being let go. */
        std::uint64_t carriedNetworkId = 0;

        /** True on the grab, false once the body is off the shoulder. */
        bool carrying = false;

        bool valid = true;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            valid = bs->Serialize(write, carriedNetworkId) && bs->Serialize(write, carrying);
        }

        bool IsSane() const {
            return valid && carriedNetworkId != 0;
        }
    };

    /**
     * The server telling a carrier's client to let the body go.
     *
     * Sent only to the carrier, the one peer whose game holds the carry; the
     * put-down it plays is reported back as a `PlayerCarry` like any other.
     * `immediate` skips the put-down clip -- for a carry that must end now,
     * such as one the gamemode refused or a body that is being revived.
     */
    struct PutDownCarried {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::PutDownCarried");

        bool immediate = false;

        bool valid = true;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            valid = bs->Serialize(write, immediate);
        }

        bool IsSane() const {
            return valid;
        }
    };
    /**
     * The server telling a player's client to pick an item up and carry it in
     * their arms -- a basket, firewood, a sack -- the way the game's own
     * carryable items and piles are carried.
     *
     * Sent only to that player, whose game makes the item and plays the
     * pick-up; the carry it starts is reported back as `PlayerCarryItem` like
     * one the player started at a pile. The class is checked on the server
     * against what the player's body has a carrying walk for.
     */
    struct CarryItem {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::CarryItem");

        ItemClassId itemClass;

        /**
         * The carryable ground item to pick up, which the server has claimed
         * for this player, or zero to make the item straight in the hands.
         */
        std::uint64_t groundItemNetworkId = 0;

        bool valid = true;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            valid = itemClass.Serialize(bs, write) && bs->Serialize(write, groundItemNetworkId);
        }

        bool IsSane() const {
            return valid && itemClass.IsValid();
        }
    };

    /**
     * A client reporting that its player took an item into their arms or
     * stopped carrying one.
     *
     * Edge-triggered like `PlayerCarry`; the held answer is the carrier's
     * `HumanEntity::carriedItemClass`. The carrier's game has already started
     * the carry when this is sent -- from `CarryItem`, or from a pile the
     * player used -- so a refusal comes back as `PutDownCarriedItem`.
     */
    struct PlayerCarryItem {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::PlayerCarryItem");

        /** What is carried; on a put-down, what was. */
        ItemClassId itemClass;

        /** True once the pick-up started, false once the item is out of the arms. */
        bool carrying = false;

        /** On a pick-up, the carryable ground item it took up, or zero. */
        std::uint64_t groundItemNetworkId = 0;

        /**
         * On a put-down, whether the item went to the ground -- the put-down,
         * or a carry let go -- rather than into a pile or back where it was.
         */
        bool dropped = false;

        /**
         * With `carrying`, that the put-down clip has started: the item is
         * still in the arms, and leaves them when the put-down is reported.
         * Sent as the clip starts so every copy plays it alongside the
         * carrier, rather than only once the carrier's has finished.
         */
        bool puttingDown = false;

        /**
         * With `dropped`, where the item lies as it leaves the hands, when the
         * carrier's game knew. The prop is made there, so the item it takes
         * over is not moved; without it the server places the prop itself.
         */
        bool dropPoseKnown     = false;
        glm::vec3 dropPosition = glm::vec3(0.0F);
        glm::quat dropRotation = glm::identity<glm::quat>();

        bool valid = true;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            valid = itemClass.Serialize(bs, write) && bs->Serialize(write, carrying) && bs->Serialize(write, groundItemNetworkId) && bs->Serialize(write, dropped) && bs->Serialize(write, puttingDown) && bs->Serialize(write, dropPoseKnown) && bs->Serialize(write, dropPosition.x) && bs->Serialize(write, dropPosition.y) && bs->Serialize(write, dropPosition.z) && bs->Serialize(write, dropRotation.x) && bs->Serialize(write, dropRotation.y) && bs->Serialize(write, dropRotation.z) && bs->Serialize(write, dropRotation.w);
        }

        bool IsSane() const {
            return valid && itemClass.IsValid();
        }
    };

    /**
     * The server telling a carrier's client to put the carried item down.
     *
     * `immediate` skips the put-down clip and lets go at once, for a carry
     * the gamemode refused. The item goes back to the ground and is gone a
     * moment later: the server keeps none of it.
     */
    struct PutDownCarriedItem {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::PutDownCarriedItem");

        bool immediate = false;

        /**
         * The carry was refused rather than ended: an item taken up from a
         * ground item goes back to it instead of becoming a new one.
         */
        bool refused = false;

        bool valid = true;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            valid = bs->Serialize(write, immediate) && bs->Serialize(write, refused);
        }

        bool IsSane() const {
            return valid;
        }
    };
    /**
     * A client asking to carry a carryable ground item: its player chose the
     * game's pick-up prompt over it.
     *
     * Asked rather than done, unlike a pile: the prop is the server's, so the
     * server claims it, asks `playerCarryingItem`, and answers with
     * `CarryItem` naming it.
     */
    struct RequestCarryGroundItem {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::RequestCarryGroundItem");

        std::uint64_t groundItemNetworkId = 0;

        bool valid = true;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            valid = bs->Serialize(write, groundItemNetworkId);
        }

        bool IsSane() const {
            return valid && groundItemNetworkId != 0;
        }
    };
} // namespace KCDC::Shared::RPC
