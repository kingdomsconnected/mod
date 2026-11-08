#pragma once

#include "shared/features/door/door_entity.h"

#include "sdk/cryengine/entitysystem/i_entity.h"
#include "sdk/types.h"

#include <mafianet/replica_manager3.h>

#include <cstdint>

namespace KCDC::Features::Door {
    /**
     * Client binding between a replicated door and its native entity.
     *
     * The level already built its own doors, so for those this only carries
     * what the server decided and remembers whether the native door has been
     * told. A door a script or a world resource hung (`IsSpawned`) has no
     * entity of the level's: this builds one, an `AnimDoor` under the GUID
     * the server minted, and takes it down again.
     */
    class Door final: public Shared::Entities::DoorEntity {
      public:
        void DeallocReplica(MafiaNet::Connection_RM3 *sourceConnection) override;

        /** True when the native door still has to be told what the server says. */
        bool NeedsApply(SDK::IEntity *nativeEntity) const {
            return nativeEntity != nullptr && (nativeEntity != _boundEntity || _appliedToken != stateToken);
        }

        /**
         * Whether the native door has already been told a state and has not
         * been rebuilt since, so a change reaches it as something happening
         * rather than as a pose to arrive at.
         */
        bool IsBoundTo(SDK::IEntity *nativeEntity) const {
            return nativeEntity != nullptr && nativeEntity == _boundEntity;
        }

        /**
         * The native door was told this token. The level rebuilding it, or
         * the world changing under it, unbinds it through Unbind; the door
         * moving on its own is caught at the edge and marked Strayed.
         */
        bool IsSettled() const {
            return _boundEntity != nullptr && _appliedToken == stateToken;
        }

        /** Records that the native door now matches this token. */
        void MarkApplied(SDK::IEntity *nativeEntity) {
            _boundEntity  = nativeEntity;
            _appliedToken = stateToken;
            _strayed      = false;
        }

        /** Forgets the binding, so the next tick tells the door again. */
        void Unbind() {
            _boundEntity  = nullptr;
            _appliedToken = 0;
            _strayed      = false;
        }

        /**
         * The native door left the server's state without the server hearing
         * of it: the world moved it, or the player's report of moving it was
         * lost. The next tick swings it back, where the player can see it,
         * reversing a swing still under way.
         */
        void MarkStrayed() {
            _appliedToken = 0;
            _strayed      = true;
        }

        bool IsStrayed() const {
            return _strayed;
        }

        /**
         * Builds the native AnimDoor of a hung door if it is not standing:
         * the leaf, the animation set and helpers its mesh swings by, and its
         * lock, assigned onto AnimDoor.lua's `Properties` before the entity
         * initialises, as the level loader does. Gives up for good on an
         * unknown model or a class the game refuses. Game thread.
         */
        void BuildNative();

        /** Takes the AnimDoor this built down. Game thread. */
        void DestroyNative();

        /** Forgets the AnimDoor this built without touching it, for a world that is gone. */
        void ForgetNative() {
            _builtEntityId = 0;
            _buildAttempts = 0;
        }

        /** Backs off a door whose script refused the state. */
        bool ReadyToRetry(std::uint64_t now) const {
            return now >= _retryAt;
        }

        void NoteApplyFailed(std::uint64_t now);

        void NoteApplySucceeded() {
            _retryAt        = 0;
            _failedAttempts = 0;
        }

      private:
        /** The AnimDoor a hung door stands as, built here; 0 for a level door. */
        SDK::EntityId _builtEntityId = 0;
        std::uint32_t _buildAttempts = 0;
        bool _buildRefused           = false;

        SDK::IEntity *_boundEntity    = nullptr;
        std::uint32_t _appliedToken   = 0;
        std::uint32_t _failedAttempts = 0;
        std::uint64_t _retryAt        = 0;
        bool _strayed                 = false;
    };
} // namespace KCDC::Features::Door
