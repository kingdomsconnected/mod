#pragma once

#include "shared/features/player/player_entity.h"
#include "shared/item_stack.h"

#include <networking/replication/delegation.h>
#include <networking/replication/network_entity.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace KCDC::Shared::Entities {
    /** Server-created, spatially streamed representation of one world stack. */
    class GroundItemEntity: public Framework::Networking::Replication::NetworkEntity {
      public:
        static constexpr const char *kTypeName = "KCDC::GroundItem";

        /**
         * A floor, not the distance a stack is seen from.
         *
         * `InterestGrid::Rebuild` keeps an entity at `max(viewerRange, entity
         * range)`, and a viewer carries `HumanEntity::kStreamRange`, so
         * anything at or under 250m streams at 250m regardless.
         *
         * What the number does decide is `_maxEntityRange` -- the maximum over
         * every live entity, which is the radius the grid is *queried* at for
         * every viewer. Ground items are creatable without bound, by a script
         * and by every player who drops something, so a generous range here
         * would widen that query for the whole session and buy no extra
         * visibility at all. The same 150m a prop takes, for the same reason.
         */
        static constexpr float kStreamRange = 150.0F;

        /**
         * Where the server starts numbering the stacks it authors itself.
         *
         * `inventoryItemId` is the identity of the stack in whichever
         * inventory reported it, minted by that client from 1 upward, so a
         * server-authored stack takes its own range rather than a number some
         * client is already using. Nothing resolves an item *by* this id
         * across the wire -- it is identity, not an address -- but two stacks
         * sharing one would still be a confusing thing to read in a log.
         */
        static constexpr std::uint64_t kServerAuthoredItemIdBase = 1ULL << 62;

        Shared::RPC::InventoryItem item;
        std::uint64_t sourcePlayerNetworkId = 0;
        // Only the source client may bind this recovery to its native impact
        // item. Other clients build their own presentation of the same stack.
        std::uint64_t recoveredItemWuid     = 0;
        bool retiresHeldItem                = false;

        /**
         * A prop people carry in their arms rather than stock: taking it up
         * is the game's own carry, never an inventory pickup. One a player put
         * down is built on every client from the item that carry dropped
         * there -- see `sourcePlayerNetworkId`.
         */
        bool carryable = false;

        /**
         * What a script or a World Builder export named the stack, so a script
         * finds it again. Server-only: it never travels, and a client's copy
         * reads empty.
         */
        std::string name;

        /**
         * Set when the simulating client's physics has settled, beside the
         * settled pose. Carried on the reliable state channel rather than the
         * transform one: the transform channel is UnreliableSequenced and
         * deduped, so a stack that stops moving sends its final pose once, and
         * a receiver that dropped it would keep an in-flight pose for good.
         */
        bool resting           = false;
        glm::vec3 restPosition = glm::vec3(0.0F);
        glm::quat restRotation = glm::identity<glm::quat>();

        /**
         * The nearest client simulates a falling stack's physics; once it
         * rests the server takes it back (`GroundItemService::Update`) and a
         * resting stack is never handed out again, so its pose -- the one
         * pickup reach is checked against -- is the server's own. Every server
         * write to a delegated stack is followed by `ForceState()`.
         */
        static constexpr Framework::Networking::Replication::DelegationPolicy kDelegationPolicy {
            /* acquireRange */ 40.0F,
            /* releaseRange */ 60.0F,
            /* weight */ 0,
            /* graceMs */ 2000,
            /* requireSameVirtualWorld */ true,
        };

        const Framework::Networking::Replication::DelegationPolicy *GetDelegationPolicy() const override {
            return resting ? nullptr : &kDelegationPolicy;
        }

        // A simulator moves a stack; only the server takes one off the ground.
        bool CanOwnerDestroy() const override {
            return false;
        }

        void SerializeForcedState(Framework::Networking::Replication::FieldSerializer &fields) override {
            Framework::Networking::Replication::NetworkEntity::SerializeForcedState(fields);
            SerializeRest(fields);
        }

        void OnSerializeConstruction(Framework::Networking::Replication::FieldSerializer &fields) override {
            SerializeItem(fields);
            fields.Field(sourcePlayerNetworkId);
            fields.Field(recoveredItemWuid);
            fields.Field(retiresHeldItem);
            fields.Field(carryable);
            fields.Field(resting);
        }

        void SerializeFields(Framework::Networking::Replication::FieldSerializer &fields) override {
            SerializeRest(fields);
        }

        void SerializeTransform(Framework::Networking::Replication::FieldSerializer &fields) override {
            fields.FloatBits(position.x, HumanEntity::kWorldMin, HumanEntity::kWorldMax, HumanEntity::kPositionBits);
            fields.FloatBits(position.y, HumanEntity::kWorldMin, HumanEntity::kWorldMax, HumanEntity::kPositionBits);
            fields.FloatBits(position.z, HumanEntity::kWorldMin, HumanEntity::kWorldMax, HumanEntity::kPositionBits);
            fields.FloatBits(rotation.x, -1.0F, 1.0F, HumanEntity::kRotationBits);
            fields.FloatBits(rotation.y, -1.0F, 1.0F, HumanEntity::kRotationBits);
            fields.FloatBits(rotation.z, -1.0F, 1.0F, HumanEntity::kRotationBits);
            fields.FloatBits(rotation.w, -1.0F, 1.0F, HumanEntity::kRotationBits);
        }

      private:
        void SerializeRest(Framework::Networking::Replication::FieldSerializer &fields) {
            fields.Field(resting);
            fields.Field(restPosition.x);
            fields.Field(restPosition.y);
            fields.Field(restPosition.z);
            fields.Field(restRotation.x);
            fields.Field(restRotation.y);
            fields.Field(restRotation.z);
            fields.Field(restRotation.w);
        }

        void SerializeItem(Framework::Networking::Replication::FieldSerializer &fields) {
            std::string custom   = fields.Writing() ? item.PublicCustom() : std::string();
            std::uint16_t length = static_cast<std::uint16_t>(std::min<std::size_t>(custom.size(), 32768));
            fields.BitsRange<std::uint16_t>(length, 0, 32768);
            length = std::min<std::uint16_t>(length, 32768);
            custom.resize(length);
            for (char &byte : custom) {
                fields.Field(byte);
            }
            const bool validCustom = fields.Writing() || item.ReadPublicCustom(custom);
            fields.Field(item.inventoryItemId);
            fields.Field(item.itemClass.low);
            fields.Field(item.itemClass.high);
            fields.Field(item.amount);
            fields.Field(item.quality);
            fields.Field(item.health);
            fields.Field(item.condition);
            fields.Field(item.phase);
            fields.Field(item.ownerId);
            fields.Field(item.stolenFromId);
            fields.Field(item.ownershipFlags);
            fields.Field(item.equipped);
            fields.Field(item.equipmentSlot);
            fields.Field(item.poisonBuff.low);
            fields.Field(item.poisonBuff.high);
            fields.Field(item.poisonCharges);
            // Bounded as `InventoryItem::Serialize` bounds it; a stack the
            // server sends never carries more (`InventoryItem::IsSane`).
            std::uint8_t buffCount = static_cast<std::uint8_t>(std::min<std::size_t>(item.onEquipBuffs.size(), Shared::RPC::kMaximumOnEquipBuffs));
            fields.BitsRange<std::uint8_t>(buffCount, 0, Shared::RPC::kMaximumOnEquipBuffs);
            buffCount = std::min(buffCount, Shared::RPC::kMaximumOnEquipBuffs);
            if (!fields.Writing()) {
                item.onEquipBuffs.assign(buffCount, Shared::RPC::ItemClassId {});
            }
            item.onEquipBuffs.resize(buffCount);
            for (auto &buff : item.onEquipBuffs) {
                fields.Field(buff.low);
                fields.Field(buff.high);
            }
            if (!validCustom) {
                item.amount = 0;
            }
        }
    };
} // namespace KCDC::Shared::Entities
