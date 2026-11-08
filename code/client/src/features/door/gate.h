#pragma once

#include "shared/features/door/gate_entity.h"
#include "shared/game/gate_catalog.h"

#include "sdk/cryengine/3dengine/i_render_node.h"
#include "sdk/cryengine/animation/i_animation_set.h"
#include "sdk/cryengine/animation/i_character_instance.h"
#include "sdk/cryengine/animation/i_skeleton_anim.h"
#include "sdk/cryengine/entitysystem/i_entity.h"
#include "sdk/cryengine/entitysystem/i_entity_system.h"
#include "sdk/types.h"

#include <mafianet/replica_manager3.h>

#include <cstdint>
#include <vector>

namespace KCDC::Features::Door {
    /**
     * The clip ids of one gate's character, read once at bind.
     *
     * They cannot change while the character is loaded, and each costs a
     * lookup through the animation set, so they are taken together and kept.
     * The clips' lengths are not here: they are the catalog's, the same
     * numbers the server times the cycle with.
     */
    struct GateIdentity {
        int openAnimationId       = -1;
        int closeAnimationId      = -1;
        int openedIdleAnimationId = -1;
        int closedIdleAnimationId = -1;
    };

    /**
     * Client binding between a replicated gate and the level's own entity.
     *
     * The level builds the gate -- a `GeomEntity` holding a skinned character
     * -- so nothing here creates anything. This owns the resolved native
     * handles and the per-frame job of putting the gate where the server's
     * cycle says it should be, and it keeps the level's static stand-in for
     * the gate hidden while it is bound, so one gateway never shows two --
     * along with any NPC-only wall the gate's layer brought in, which would
     * stop horses at a gate standing open.
     *
     * The cycle is never streamed: `GateEntity` carries one edge, and this
     * turns it into a position locally every frame. Playback follows the
     * engine's own driver for these assets -- TrackView-exclusive mode, one
     * looping clip on a cleared layer, seeked each frame.
     */
    class Gate final: public Shared::Entities::GateEntity {
      public:
        void DeallocReplica(MafiaNet::Connection_RM3 *sourceConnection) override;

        /**
         * Resolves the level entity and its clips. Idempotent once bound.
         * False while the entity has no character, the normal state for an
         * area still streaming in.
         */
        bool Bind(SDK::IEntity *entity, const Shared::Game::GatePlacement &placement);

        /**
         * Forgets the native side, so the next tick resolves it again. With
         * `restoreWorld` it also gives back what binding took -- the collision
         * layer's state, TrackView control and the hidden brushes. Without,
         * the world those belonged to is already gone.
         */
        void Unbind(bool restoreWorld = false);

        bool IsBound() const {
            return _character != nullptr;
        }

        /**
         * How often an unbound gate may be looked for by sweeping the whole
         * entity system. The GUID lookup is tried every frame; only a pooled
         * entity, which that lookup cannot see, needs the sweep.
         */
        static constexpr std::uint64_t kSweepIntervalMs = 2000;

        /**
         * Whether this gate is due a sweep: at once when it has just arrived
         * or come unbound, so a late joiner is not left looking at the
         * stand-in, and then at most every `kSweepIntervalMs`.
         */
        bool SweepDue(std::uint64_t now) const {
            return !_swept || now - _sweptAt >= kSweepIntervalMs;
        }

        void NoteSwept(std::uint64_t now) {
            _swept   = true;
            _sweptAt = now;
        }

        /**
         * Puts the gate where `nowLocal` says it should be.
         *
         * `serverClockOffset` is the peer's difference to the server clock, so
         * a server-stamped cycle start becomes a local one. Until it is known
         * the caller passes false and a moving gate holds its start pose.
         *
         * The bound handles are checked against the live world first: the
         * gate's layer is quest-switched, so the engine can destroy the entity
         * or reload its character between ticks. A stale binding is dropped
         * and the module resolves it again.
         */
        void Drive(std::uint64_t nowLocal, std::int64_t serverClockOffset, bool serverClockKnown);

      private:
        /** One clip held at one position: everything a frame needs to state. */
        struct Pose {
            int animationId      = -1;
            float normalizedTime = 0.0F;
        };

        /** How long the running motion has been going, on this machine's clock; 0 until the server clock is known. */
        std::int64_t Elapsed(std::uint64_t nowLocal, std::int64_t serverClockOffset, bool serverClockKnown) const;

        /** Where the gate should be, from the replicated state alone. */
        Pose Resolve(std::uint64_t nowLocal, std::int64_t elapsed) const;

        /** The clip and direction that carry `motionState`'s motion. */
        Pose Motion(Shared::Entities::GateState motionState, float phase) const;

        /** The clip ids this gate's asset names, in the character's own set. */
        bool ResolveClips(SDK::IEntity *entity, GateIdentity &identity) const;

        void ApplyPose(const Pose &pose);

        /**
         * Hides the render nodes of the brushes this gate displaces, or shows
         * them again: its static stand-in, and the NPC-only wall its layer
         * brings in. Their colliders are never built (level_collider_hooks.cpp).
         *
         * A brush has no identity that survives HLOD streaming, so each is
         * found every time by its mesh where it stands; while hidden that is
         * repeated every `kLevelBrushRecheckMs`, because a fresh instance
         * comes back shown.
         */
        void HideLevelBrushes(bool hidden, std::uint64_t now);

        /** Hides or shows every render node of one displaced brush. */
        void HideBrush(const Shared::Game::GateBrush &brush, bool hidden);

        SDK::IEntity *_entity               = nullptr;
        SDK::EntityId _entityId             = 0;
        SDK::ICharacterInstance *_character = nullptr;
        GateIdentity _identity;

        /** So a frame that changes nothing does not restart the clip. */
        int _playingAnimationId                       = -1;
        const Shared::Game::GatePlacement *_placement = nullptr;

        bool _levelBrushesHidden             = false;
        std::uint64_t _levelBrushesCheckedAt = 0;

        bool _swept            = false;
        std::uint64_t _sweptAt = 0;

        /** Reused between octree queries so a check does not allocate. */
        std::vector<SDK::IRenderNode *> _octreeScratch;
    };
} // namespace KCDC::Features::Door
