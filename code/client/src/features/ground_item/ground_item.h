#pragma once

#include "shared/features/ground_item/ground_item_entity.h"

#include "sdk/wh/entitymodule/c_item.h"

#include <mafianet/replica_manager3.h>

#include <cstdint>

namespace KCDC::Features::GroundItem {
    /**
     * Client binding between a replicated ground stack and its native item.
     *
     * Every client builds the world item from the replica. The one the server
     * elected to simulate it (`IsSimulating()`) lets native physics move it and
     * writes the pose back; every other client places it where the replica
     * says. `GroundItemEntity` is shared with the server, which has no engine,
     * so the native item is held here rather than there.
     */
    class GroundItem final: public Shared::Entities::GroundItemEntity {
      public:
        // --- Replica3 hooks ---

        void OnDeserialized(bool transformUpdated) override;
        void OnStateForced() override;
        void DeallocReplica(MafiaNet::Connection_RM3 *sourceConnection) override;

        // --- The native item ---

        /** The world stack this replica is bound to, or nullptr. One item-manager lookup. */
        SDK::wh::entitymodule::C_Item *GetNativeItem() const;

        /** The WUID of the world stack this replica built, or 0; no lookup. */
        std::uint64_t GetNativeItemWuid() const {
            return _nativeItem != nullptr ? _nativeItemWuid : 0;
        }

        /** Whether this drop still needs this player's matching hand shadow. */
        bool IsAwaitingHeldShadow(std::uint64_t playerNetworkId, const Shared::RPC::ItemClassId &heldClass) const {
            return retiresHeldItem && sourcePlayerNetworkId == playerNetworkId && item.itemClass == heldClass && !_heldShadowResolved && !_materializationFailed && !_carriedAway;
        }

        /** Whether `entityId` is the pickable this replica's stack lies in the world as. */
        bool IsPickableEntity(SDK::EntityId entityId) const;

        /**
         * Gives this prop's item to a carry taking it up: the item stays where
         * it lies for the pick-up to reach for, and this replica stops
         * presenting it -- or ever building another. Null when there is no
         * item here with a pickable to reach for.
         */
        SDK::wh::entitymodule::C_Item *ReleaseNativeForCarry();

        /** Takes back an item ReleaseNativeForCarry gave up, for a carry that never happened. */
        void RebindNative(SDK::wh::entitymodule::C_Item *item);

        // --- Per-frame ---

        /** The simulator samples native physics into the replica; observers follow the replica. */
        void Frame();

        /**
         * Builds the world item for a stack whose only pose was its
         * construction, and retries one whose pose beat its world item.
         */
        void EnsureMaterialized();

      private:
        /** Builds the world item this replica describes. */
        bool TryMaterialize();
        void RefreshNativeBinding();

        /**
         * Hands this replica the shadow the source player was holding, if the
         * drop retires one. Returns whether the shadow became our stack.
         */
        bool TryAdoptHeldShadow(bool &blocked);

        /**
         * Takes over the item the source player's carry dropped here, for a
         * prop that carry put down. `waiting` says that carry is still in
         * their arms on this machine -- its put-down will drop it shortly --
         * which is a reason to wait rather than to build a second one.
         */
        bool TryAdoptCarriedDrop(bool &waiting);

        /** Creates a fresh stack in world inventory and spawns its pickable. */
        bool CreateWorldItem();

        /** States on `native` the missile properties the wire carried. */
        bool ApplyMissileProperties(SDK::wh::entitymodule::C_Item *native) const;

        /** Writes the replicated pose onto the materialized world item and its physics body; `immediate` skips smoothing. */
        void ApplyRemotePose(bool immediate = false);

        /** Samples the simulated stack and writes its pose into the replica. */
        void Simulate();

        /** Tears down whichever exact holder relationship this stack has. */
        void CleanupNative();
        void RemoveGroundRepresentation();

        /** Tells LocalEvents the stack is really on the ground now. Owed once. */
        void NoteBecamePhysical();

        /** Forgets the sampled motion, for a new simulator or a pose the server wrote. */
        void ResetSampling();

        SDK::wh::entitymodule::C_Item *_nativeItem = nullptr;

        // The engine addresses items by WUID; a stale C_Item * is not something
        // it will report. Kept so the stack can be confirmed to still be the
        // one this replica made before anything is done to it.
        std::uint64_t _nativeItemWuid = 0;

        bool _simulating      = false;
        bool _visualPoseKnown = false;
        // A resting stack is placed once per pose it is given, not per frame.
        bool _poseDirty = true;
        // World inventory is walked to confirm the binding at this pace, not per frame.
        std::uint64_t _nextBindingCheck = 0;
        glm::vec3 _visualPosition {0.0F};
        glm::quat _visualRotation       = glm::identity<glm::quat>();
        std::uint64_t _lastVisualUpdate = 0;

        bool _createdNative               = false;
        // A drop consumes its hand shadow once. Rebuilding its world item
        // must never take a later weapon from the same player's hands.
        bool _heldShadowResolved = false;
        // The native item and profile ApplyRemotePose last requested physics for.
        const SDK::wh::entitymodule::C_Item *_profiledItem = nullptr;
        int _requestedProfile                              = 0;
        // A carry took this prop's item; the replica only waits to be destroyed.
        bool _carriedAway = false;
        // Until when a put-down prop waits for its carry to drop the item.
        std::uint64_t _adoptWaitUntil = 0;
        bool _materializationFailed       = false;
        std::uint8_t _materializeAttempts = 0;

        // Simulator-side settle detection: the SDK exposes no settle callback,
        // so motion is decided by consecutive samples.
        bool _samplePoseKnown = false;
        glm::vec3 _samplePosition {0.0F};
        glm::quat _sampleRotation                  = glm::identity<glm::quat>();
        bool _becamePhysical                       = false;
        std::uint8_t _sourceConsecutiveMotionSteps = 0;
        std::uint8_t _sourceConsecutiveStillSteps  = 0;
    };
} // namespace KCDC::Features::GroundItem
