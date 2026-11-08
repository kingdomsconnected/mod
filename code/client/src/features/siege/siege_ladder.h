#pragma once

#include "shared/features/siege/siege_ladder_entity.h"

#include "sdk/cryengine/entitysystem/i_entity.h"
#include "sdk/types.h"

#include <mafianet/replica_manager3.h>

#include <cstdint>

namespace KCDC::Features::Siege {
    /**
     * Client binding between a replicated siege ladder and the game's own
     * `Ladder` entity that stands for it.
     *
     * Built the way the level loader builds the battle prefab's: the class
     * spawned uninitialised, its `Properties` -- mesh, climb height, the
     * palisade top, the get-on distance -- written the way the loader assigns
     * them, then initialised, so `Ladder.lua`'s own `OnSpawn` loads and
     * physicalizes it. The climb, its prompt and the step off at the top are
     * then the game's. Its GUID is derived from the network id, so a remote
     * player's replicated climb finds this machine's copy of it by GUID as it
     * would a ladder placed in the level.
     *
     * Every frame the ladder is turned about its foot to the tilt its state
     * and the server clock give, and the climb is offered only while it
     * stands. A ladder that starts to fall throws the local player off it.
     * `SiegeLadderModule` owns the tick, the prompts and the level change.
     */
    class SiegeLadder final: public Shared::Entities::SiegeLadderEntity {
      public:
        /** Tags every ladder GUID this mod derives, so none is mistaken for a level's. */
        static constexpr SDK::EntityGUID kGuidTag  = 0xF5A7000000000000ULL;
        static constexpr SDK::EntityGUID kGuidMask = 0x0000FFFFFFFFFFFFULL;

        /** Queues the built entity for removal: a replica can be destroyed from the loading-time peer pump. */
        void DeallocReplica(MafiaNet::Connection_RM3 *sourceConnection) override;

        /** Builds, tilts and gates the ladder. Game thread only. */
        void Frame(std::uint64_t nowLocal, std::int64_t serverClockOffset, bool serverClockKnown);

        /** Forgets the native side without touching it, for a world that is gone. */
        void Unbind();

        /** The live `Ladder` entity, or null while unbuilt. */
        SDK::IEntity *ResolveNative() const;

        SDK::EntityId GetEntityId() const {
            return _entityId;
        }

        /**
         * Whether this machine offers the game's climb on it. False while it
         * does not stand, and while the local player stands at its top to push
         * it: the push prompt takes the use key there.
         */
        void SetClimbOffered(bool offered);

        /** How long the running step has been going on this machine's clock; 0 while settled or before the server clock is known. */
        std::int64_t Elapsed(std::uint64_t nowLocal, std::int64_t serverClockOffset, bool serverClockKnown) const;

      private:
        bool TryBuild();

        /** Shows the broken mesh, or the whole one again. */
        void ShowModel(bool broken);

        /** Throws the local player off this ladder if they are on it. */
        void DropLocalClimber();

        /**
         * Fits the climb to the wall it stands against. `Ladder.lua`'s
         * `height` is where the game's climb ends and its step off begins,
         * whatever is there, and the climber has no collider until the step
         * off is done -- so a ladder taller than its wall would carry them
         * past an eave or over a roof into a building. The highest rung with
         * a clear body above it and open walk beyond becomes the height; with
         * none, the top is blocked and the climb goes back down.
         */
        void FitClimb(SDK::IEntity &entity);

        SDK::EntityId _entityId           = 0;
        std::uint64_t _nextBuildAttempt   = 0;
        bool _buildRefused                = false;

        float _appliedTilt   = 0.0F;
        bool _tiltApplied    = false;
        bool _climbOffered   = true;
        bool _climbApplied   = false;
        bool _brokenShown    = false;
        bool _climbWasOffered = false;

        /** Whether `FitClimb` ran for the built entity: the climb is offered only after. */
        bool _climbFitted = false;

        /** The step whose fall this machine already threw its player off, so it is done once. */
        std::uint64_t _droppedForStep = 0;

        /** The fall whose crash this machine played, and whether it saw that fall begin. */
        std::uint64_t _crashedForStep = 0;
        bool _crashWatched            = false;
    };
} // namespace KCDC::Features::Siege
