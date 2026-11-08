#pragma once

#include "shared/features/prop/prop_entity.h"
#include "shared/features/prop/prop_rpc.h"

#include "common/spawn/prop_spawner.h"
#include "common/spawn/spawn_budget.h"
#include "common/spawn/static_brush.h"
#include "sdk/cryengine/3dengine/i_render_node.h"
#include "sdk/cryengine/3dengine/i_stat_obj.h"
#include "sdk/cryengine/entitysystem/i_entity.h"
#include "sdk/cryengine/physics/i_physical_entity.h"
#include "sdk/types.h"

#include <mafianet/get_time.h>
#include <mafianet/replica_manager3.h>

#include <atomic>
#include <cstdint>
#include <vector>

namespace KCDC::Features::Prop {
    /**
     * Client binding between a replicated prop and the mesh it stands for.
     *
     * This keeps a native object standing where the replica says -- and
     * builds a new one whenever the model, the physics or the scale changes,
     * since the engine reads all three when it builds one and never again.
     *
     * A rigid prop's body is run by one client, the one the server's
     * delegation elected and made the owner. There it has the mass its mesh
     * gives it, takes the script's motions and writes its pose and rest
     * upstream; everywhere else every part has no mass, so it cannot move on
     * its own, and it is placed where the stream says.
     *
     * What it builds depends on whether it can move. A rigid prop is a
     * `BasicEntity`, the only form with a body that falls and is pushed. Any
     * other static mesh is a `CBrush` render node, the form the level keeps
     * its own furniture in: no entity record and no script table, drawn and
     * culled by the 3D engine to the same distance as the level's own copy of
     * that mesh, and given a static collider the prop owns rather than the
     * brush, which the engine's on-demand physics would drop.
     * That is what lets a map of thousands of objects stand on every client.
     *
     * `PropService` owns the registration, the tick and the level change.
     */
    class Prop final: public Shared::Entities::PropEntity {
      public:
        /**
         * Queues the built entity for removal rather than removing it: a
         * replica can be destroyed from the loading-time peer pump, where the
         * entity system is part-way through its own work.
         */
        void DeallocReplica(MafiaNet::Connection_RM3 *sourceConnection) override;

        // All raised from the peer pump, so all only set the flags the tick reads.
        void OnConstructed() override;
        void OnDeserialized(bool transformUpdated) override;

        /** The server moved the body this client simulates -- a reset, a script's move -- and it has to take the pose. */
        void OnStateForced() override;

        /**
         * Builds, rebuilds and places this prop. A brush is built here and
         * charged to `budget`; an exhausted budget leaves it for a later
         * frame. Game thread only.
         */
        void Frame(KCDC::Common::Spawn::SpawnBudget &budget);

        /** Forgets the native side without touching it, for a world that is gone. */
        void Unbind();

        /**
         * Takes a motion the server sent for this prop's body. It is done to
         * the body once one stands here and this client simulates it, on a
         * later `Frame`; one that has waited longer than `kMotionShelfLifeMs`
         * for that is dropped. Game thread only.
         */
        void QueueMotion(const Shared::RPC::PropMotion &motion, MafiaNet::Time receivedAt);

        /** How long a motion waits for the body it is for, as `PropService` also holds one for a replica not yet here. */
        static constexpr MafiaNet::Time kMotionShelfLifeMs = 5000;

        /** The entity a rigid prop stands as; 0 for one built as a brush, or not built. */
        SDK::EntityId GetEntityId() const {
            return _entityId;
        }

        /** Whether it stands as a brush, which has a pose but no entity to attach anything to. */
        bool IsBrush() const {
            return _brush.IsBuilt();
        }

      private:
        /**
         * Builds what the replica describes: a brush now, when `budget` allows,
         * or a request to the spawner for an entity, which arrives in
         * `OnBuilt` on a later frame.
         */
        void RequestBuild(KCDC::Common::Spawn::SpawnBudget &budget);

        /** Whether a mesh with this physics stands as a brush rather than an entity. */
        static bool BuildsAsBrush(const std::string &model, Shared::Entities::PropPhysics physics);

        /** Builds the brush and records what it was built from. False when the engine refused it. */
        bool BuildBrush(const std::string &model);

        /** Records what the standing object was built from, for `NeedsRebuild`. */
        void RecordBuilt();
        void OnBuilt(KCDC::Common::Spawn::PropSpawnTicket &ticket, bool built);

        /** Cancels a request that has not run, or forgets one that has. */
        void ReleaseBuildTicket();

        /** Game thread only. */
        void DestroyNative();

        /** The live entity this replica built, or nullptr once it is gone. */
        SDK::IEntity *ResolveNative() const;

        bool NeedsRebuild() const;

        /** The catalog row's file stem, or the streamed path; for the log. */
        std::string ModelName() const;
        void ApplyPose();

        // --- A rigid prop's body ---------------------------------------------

        /** Takes the simulator's part or an observer's, and runs it for one frame. */
        void FrameBody(SDK::IEntity &entity);

        /** The simulator: does the queued motions, then writes the body's pose -- and whether it rests -- upstream. */
        void Simulate(SDK::IEntity &entity, SDK::IPhysicalEntity &body);

        /** An observer: places the copy where the simulator's stream says, smoothed while it moves. */
        void Follow(SDK::IEntity &entity);

        /** Gives every part the mass its mesh gave it, or none at all, which pins it where it is placed. */
        void SetBodyMass(SDK::IPhysicalEntity &body, bool real);

        /** Forgets the body and every motion: the world they were for is gone. */
        void DropBody();

        struct QueuedMotion {
            Shared::RPC::PropMotion motion;
            MafiaNet::Time receivedAt = 0;
        };

        // Motions waiting for a body this client simulates, oldest first.
        std::vector<QueuedMotion> _pendingMotions;

        // Each part's own mass, read once the body is built, so an observer can take it away and a new simulator give it back.
        std::vector<float> _partMasses;

        // The part this client last took for the body standing now, and whether it has taken one.
        bool _simulating = false;
        bool _roleTaken  = false;

        // Set from the peer pump by a forced pose, cleared once the simulator's body has taken it.
        std::atomic<bool> _poseForced {false};

        // When the simulator last started the body moving: a push lands after the next physics step, and a body lying still is asleep until then. And since when it has been still, which is how long before it is resting.
        MafiaNet::Time _lastMotionAt = 0;
        MafiaNet::Time _stillSince   = 0;

        // Where an observer last placed its copy, which it moves towards the streamed pose.
        glm::vec3 _visualPosition    = glm::vec3(0.0F);
        glm::quat _visualRotation    = glm::identity<glm::quat>();
        MafiaNet::Time _lastFollowAt = 0;
        bool _visualKnown            = false;

        SDK::EntityId _entityId = 0;

        /** The brush a static prop stands as, with the collider it owns; not built otherwise. */
        KCDC::Common::Spawn::StaticBrush _brush;

        KCDC::Common::Spawn::PropSpawnTicket *_buildTicket = nullptr;

        // What the in-flight request was built from. The wire can change the
        // model, physics or scale between asking and being called back, and
        // recording the new values against the old body would leave
        // `NeedsRebuild` satisfied by a prop that is not what was asked for.
        std::uint32_t _requestedModelIndex = Shared::Entities::PropEntity::kNoModel;
        std::string _requestedStreamedModel;
        Shared::Entities::PropPhysics _requestedPhysics = Shared::Entities::PropPhysics::Static;
        float _requestedScale                           = 0.0F;
        glm::vec3 _requestedPosition                    = glm::vec3(0.0F);
        glm::quat _requestedRotation                    = glm::identity<glm::quat>();

        // What the standing entity was built from, so a change on the wire is
        // a comparison rather than a flag every writer has to remember to set.
        std::uint32_t _builtModelIndex = Shared::Entities::PropEntity::kNoModel;
        std::string _builtStreamedModel;
        Shared::Entities::PropPhysics _builtPhysics = Shared::Entities::PropPhysics::Static;
        float _builtScale                           = 0.0F;
        glm::vec3 _builtAxisScale {1.0F};
        glm::vec3 _requestedAxisScale {1.0F};
        float _builtMass     = 0.0F;
        float _requestedMass = 0.0F;

        glm::vec3 _appliedPosition = glm::vec3(0.0F);
        glm::quat _appliedRotation = glm::identity<glm::quat>();

        // Set from the peer pump and cleared on the game thread once the prop
        // stands where the wire says. Scenery is the type there is most of and
        // that changes least, so the tick is driven by updates arriving rather
        // than by looking at every prop every frame.
        std::atomic<bool> _dirty {true};

        // A world that is not up yet is held and retried on an interval; a
        // request that is wrong is refused. Both are logged once.
        MafiaNet::Time _nextBuildAttempt = 0;
        bool _buildDeferred              = false;
        bool _buildRefused               = false;
    };
} // namespace KCDC::Features::Prop
