#pragma once

#include "shared/features/siege/siege_entity.h"
#include "shared/features/siege/siege_rpc.h"

#include "common/spawn/prop_spawner.h"
#include "sdk/cryengine/animation/i_character_instance.h"
#include "sdk/cryengine/entitysystem/i_entity.h"
#include "sdk/cryengine/physics/i_physical_entity.h"
#include "sdk/types.h"

#include <mafianet/get_time.h>
#include <mafianet/replica_manager3.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <optional>

namespace KCDC::Features::Siege {
    /** A catalog point, in the engine's own frame, as a vector. */
    inline glm::vec3 ToGlm(const Shared::Game::SiegePoint &point) {
        return glm::vec3(point.x, point.y, point.z);
    }

    /**
     * Client binding between a replicated siege engine and the entity that
     * stands for it.
     *
     * The engine is built here, the way a prop is -- a `BasicEntity` loading
     * the asset's model, static, so it collides -- and then driven: the
     * trebuchet's clip is played from the server's step and clock every frame
     * the way TrackView plays it in the cutscenes (see `Door::Gate`), and the
     * stone the crew lays in the sling is a second entity held on the sling's
     * path until the engine's own shot takes it over. A cannon's gun is
     * raised to the elevation its aim solves to, kicks back as it fires, and
     * a wreck is shown as the game's broken gun or, with none, as the engine
     * leaning into the ground.
     *
     * Nothing about an engine is reported back. `SiegeModule` owns the tick,
     * the shots in the air, the operator's keys and the level change.
     */
    class SiegeEngine final: public Shared::Entities::SiegeEngineEntity {
      public:
        /**
         * Queues the built entities for removal rather than removing them: a
         * replica can be destroyed from the loading-time peer pump, where the
         * entity system is part-way through its own work.
         */
        void DeallocReplica(MafiaNet::Connection_RM3 *sourceConnection) override;

        /** Builds, places and animates the engine. Game thread only. */
        void Frame(std::uint64_t nowLocal, std::int64_t serverClockOffset, bool serverClockKnown, float frameSeconds);

        /** Forgets the native side without touching it, for a world that is gone. */
        void Unbind();

        /** The engine's own collider, which a stone it throws has to pass through; null while unbuilt. */
        SDK::IPhysicalEntity *GetPhysics() const;

        /** The entity standing for the engine, 0 while unbuilt. */
        SDK::EntityId GetEntityId() const {
            return _entityId;
        }

        /**
         * The launch the engine's aim solves to from where it stands now, as
         * every client solves it; nothing while it is not laid or cannot
         * reach its aim.
         */
        std::optional<Shared::Game::Siege::Launch> AimedLaunch() const;

        /** How far through its running step the engine is on this machine's clock, 0 to 1; a held step where it stopped. */
        float StepPhase(std::uint64_t nowLocal, std::int64_t serverClockOffset, bool serverClockKnown) const {
            return PhaseAfter(Elapsed(nowLocal, serverClockOffset, serverClockKnown));
        }

        /**
         * How far `point` is from the engine itself along the ground: from its
         * built mesh's bounds, so a player beside a trebuchet's frame is near
         * it however far that is from its base. From its base while unbuilt.
         */
        float GroundDistanceTo(const glm::vec3 &point) const;

        /** Kicks a gun back from its shot, eased home again. */
        void Kick(std::uint64_t nowLocal);

      private:
        /** One clip held at one position. */
        struct Pose {
            int animationId      = -1;
            float normalizedTime = 0.0F;
        };

        void RequestBuild();
        void OnBuilt(KCDC::Common::Spawn::PropSpawnTicket &ticket, bool built);
        void ReleaseBuildTicket();

        /** The mesh the engine is shown as now: the broken one once it is wrecked, when the game has one. */
        const char *ModelNow() const;

        /** How the mesh stands: the engine's pose with the asset's own turn, the barrel's elevation, the kick and a wreck's lean. */
        glm::vec3 MeshPosition() const;
        glm::quat MeshRotation() const;

        /** The live entity this replica built, or nullptr once it is gone. */
        SDK::IEntity *ResolveNative() const;

        /** Resolves the character and its clips once the model is in. False for a static engine or a character still loading. */
        bool BindCharacter(SDK::IEntity *entity);

        /** How long the running step has been going, on this machine's clock; 0 until the server clock is known. */
        std::int64_t Elapsed(std::uint64_t nowLocal, std::int64_t serverClockOffset, bool serverClockKnown) const;

        /** How far into the running step's authored clip the engine is, in authored milliseconds. */
        float AuthoredMs(std::int64_t elapsed) const;

        Pose Resolve(std::int64_t elapsed) const;
        void ApplyPose(const Pose &pose);

        /** Where the stone in the sling should be, in model space, or nothing while there is none to show. */
        std::optional<glm::vec3> SlingPoint(std::int64_t elapsed) const;

        /** Shows, moves or removes the stone in the sling. */
        void PlaceSlingStone(const std::optional<glm::vec3> &point);

        /** Eases a laid barrel towards its aim and the kick back home. */
        void Settle(std::uint64_t nowLocal, float frameSeconds);

        /** The effects of a step this machine watched begin: the fuse as a shot starts, the dust as the engine is wrecked. */
        void OnStateEntered(Shared::Entities::SiegeState entered);

        SDK::EntityId _entityId = 0;
        KCDC::Common::Spawn::PropSpawnTicket *_buildTicket = nullptr;
        MafiaNet::Time _nextBuildAttempt = 0;
        bool _buildRefused               = false;

        /** The mesh the built entity carries, so a wreck's broken mesh is built in its place. */
        const char *_builtModel = nullptr;

        SDK::ICharacterInstance *_character = nullptr;
        int _clipIds[4]                     = {-1, -1, -1, -1};
        int _playingAnimationId             = -1;

        /** Where the playing clip was last seeked to, so a held pose is not seeked again every frame. */
        float _playingTime = -1.0F;

        glm::vec3 _appliedPosition = glm::vec3(0.0F);
        glm::quat _appliedRotation = glm::identity<glm::quat>();

        /** A laid barrel's elevation as shown, in radians, easing towards the aim's. */
        float _shownElevation = 0.0F;

        /** How far the gun stands kicked back, in metres, and when the kick began; 0 for none. */
        float _recoil              = 0.0F;
        std::uint64_t _recoilStart = 0;

        /** The state this machine last saw, and whether it has seen one at all: the first is joined, not watched. */
        Shared::Entities::SiegeState _seenState = Shared::Entities::SiegeState::Idle;
        bool _seen                              = false;

        SDK::EntityId _slingStoneId = 0;
        glm::vec3 _slingStonePoint {0.0F};

        /** A stone that failed to spawn is not asked for again before this, so a refusal is not retried every frame. */
        MafiaNet::Time _slingRetryAt = 0;
    };
} // namespace KCDC::Features::Siege
