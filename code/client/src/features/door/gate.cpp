#include "gate.h"

#include "sdk/cryengine/3dengine/i_3d_engine.h"
#include "sdk/cryengine/animation/c_skeleton_anim.h"
#include "sdk/cryengine/animation/cry_char_animation_params.h"
#include "sdk/cryengine/entitysystem/i_entity_system.h"

#include <algorithm>
#include <cstddef>

namespace KCDC::Features::Door {
    namespace {
        /** A gate prop has no animation of its own, so the base layer is free. */
        constexpr std::uint32_t kGateLayer = 0;

        /** How often hidden brushes are looked for again, in case HLOD streaming brought a fresh one back. */
        constexpr std::uint64_t kLevelBrushRecheckMs = 1000;

        /**
         * Half the side of the box a displaced brush is looked for in. The
         * catalog aims it at the centre of the brush's world bounds, and the
         * octree keeps any node whose bounds overlap it, so it only has to
         * absorb float noise; the mesh name does the rest.
         */
        constexpr float kLevelBrushReach = 0.5F;

        /**
         * The file name at the end of a mesh path. A render node reports the
         * path the engine loaded, which has `%level%` expanded to the level
         * folder, while the catalog keeps the alias the level stores.
         */
        const char *MeshFileName(const char *path) {
            const char *name = path;
            for (const char *cursor = path; *cursor != '\0'; ++cursor) {
                if (*cursor == '/' || *cursor == '\\') {
                    name = cursor + 1;
                }
            }
            return name;
        }

        /** GetObjectsInBox copies its whole list with no size, so a box that full is refused. */
        constexpr std::uint32_t kMaxOctreeNodes = 4096;

        /**
         * Flags for a clip whose playback position the mod controls.
         *
         * `CA_TRACK_VIEW_EXCLUSIVE` pairs with `SetTrackViewExclusive(1)` and
         * is what keeps a hand-driven clip in the queue; it also gates anim
         * events, without which the portcullis moves silently.
         * `CA_LOOP_ANIMATION` is here to stop a finished clip being popped,
         * not to loop: seeking needs exactly one animation in the layer.
         */
        constexpr std::uint32_t kDrivenFlags = SDK::CA_TRACK_VIEW_EXCLUSIVE | SDK::CA_ALLOW_ANIM_RESTART | SDK::CA_LOOP_ANIMATION;

        int ResolveClip(SDK::IAnimationSet *animationSet, const char *clip) {
            return clip != nullptr && clip[0] != '\0' ? animationSet->GetAnimIDByName(clip) : -1;
        }

        float LoopPhase(std::uint64_t nowLocal, std::uint32_t durationMs) {
            if (durationMs == 0) {
                return 0.0F;
            }
            return static_cast<float>(nowLocal % durationMs) / static_cast<float>(durationMs);
        }
    } // namespace

    void Gate::DeallocReplica(MafiaNet::Connection_RM3 *) {
        Unbind(true);
        delete this;
    }

    bool Gate::ResolveClips(SDK::IEntity *entity, GateIdentity &identity) const {
        identity = GateIdentity {};

        // A `.cdf` reaches slot 0 through CEntity::LoadCharacter, and is null
        // until the area streams in.
        SDK::ICharacterInstance *character = entity != nullptr ? entity->GetCharacter(0) : nullptr;
        SDK::IAnimationSet *animationSet   = character != nullptr ? character->GetIAnimationSet() : nullptr;
        if (animationSet == nullptr) {
            return false;
        }

        const Shared::Game::GateAsset &asset = Asset();
        const int openId                     = ResolveClip(animationSet, asset.openClip);
        const int closeId                    = ResolveClip(animationSet, asset.closeClip);
        if (openId < 0 && closeId < 0) {
            return false;
        }

        identity.openAnimationId       = openId;
        identity.closeAnimationId      = closeId;
        identity.openedIdleAnimationId = ResolveClip(animationSet, asset.openedIdleClip);
        identity.closedIdleAnimationId = ResolveClip(animationSet, asset.closedIdleClip);
        return true;
    }

    void Gate::Unbind(bool restoreWorld) {
        if (restoreWorld && _levelBrushesHidden) {
            HideLevelBrushes(false, 0);
        }
        _levelBrushesHidden    = false;
        _levelBrushesCheckedAt = 0;
        _swept                 = false;

        auto *entitySystem = SDK::IEntitySystem::GetInstance();
        if (restoreWorld && entitySystem != nullptr) {
            if (_entity != nullptr && _character != nullptr && entitySystem->GetEntity(_entityId) == _entity && _entity->GetCharacter(0) == _character) {
                if (SDK::ISkeletonAnim *skeleton = _character->GetISkeletonAnim()) {
                    skeleton->SetTrackViewExclusive(0);
                }
            }
        }
        _entity             = nullptr;
        _entityId           = 0;
        _character          = nullptr;
        _identity           = GateIdentity {};
        _playingAnimationId = -1;
        _placement          = nullptr;
    }

    bool Gate::Bind(SDK::IEntity *entity, const Shared::Game::GatePlacement &placement) {
        if (entity == nullptr) {
            return false;
        }
        if (_entity == entity && _character != nullptr) {
            return true;
        }

        Unbind(true);

        GateIdentity identity;
        if (!ResolveClips(entity, identity)) {
            return false;
        }

        SDK::ICharacterInstance *character = entity->GetCharacter(0);
        SDK::ISkeletonAnim *skeleton       = character != nullptr ? character->GetISkeletonAnim() : nullptr;
        if (skeleton == nullptr) {
            return false;
        }

        // Claimed before anything is started: outside this mode a hand-driven
        // clip is dropped from the queue.
        skeleton->SetTrackViewExclusive(1);

        _entity    = entity;
        _entityId  = entity->GetId();
        _character = character;
        _identity  = identity;
        _placement = &placement;
        HideLevelBrushes(true, 0);
        return true;
    }

    void Gate::HideLevelBrushes(bool hidden, std::uint64_t now) {
        if (_placement == nullptr) {
            return;
        }
        _levelBrushesHidden    = hidden;
        _levelBrushesCheckedAt = now;

        for (const Shared::Game::GateBrush &brush : _placement->displacedBrushes) {
            HideBrush(brush, hidden);
        }
    }

    void Gate::HideBrush(const Shared::Game::GateBrush &brush, bool hidden) {
        auto *engine = SDK::I3DEngine::GetInstance();
        if (engine == nullptr) {
            return;
        }
        const char *fileName = MeshFileName(brush.mesh);

        SDK::AABB box {};
        box.min = SDK::Vec3 {brush.positionX - kLevelBrushReach, brush.positionY - kLevelBrushReach, brush.positionZ - kLevelBrushReach};
        box.max = SDK::Vec3 {brush.positionX + kLevelBrushReach, brush.positionY + kLevelBrushReach, brush.positionZ + kLevelBrushReach};

        // Hidden nodes are returned too (COctreeNode::GetObjects filters no
        // render flag), which is what lets the stand-in be found to show it.
        const std::uint32_t available = engine->GetObjectsInBox(box, nullptr);
        if (available == 0 || available > kMaxOctreeNodes) {
            return;
        }
        _octreeScratch.assign(available, nullptr);
        const std::uint32_t returned = std::min(engine->GetObjectsInBox(box, _octreeScratch.data()), available);

        for (std::uint32_t index = 0; index < returned; ++index) {
            SDK::IRenderNode *node = _octreeScratch[index];
            if (node == nullptr || node->GetOwnerEntity() != nullptr || node->IsHidden() == hidden) {
                continue;
            }
            const char *name = node->GetName();
            if (name == nullptr || _stricmp(MeshFileName(name), fileName) != 0) {
                continue;
            }
            // The node's own physics exists only with
            // wh_e_PernamentPhysicalization off. With it on, the shipped
            // default, the collider is C_PhysicalizationManager's, and that
            // one is never built (level_collider_hooks.cpp).
            node->SetHidden(hidden);
            if (hidden) {
                node->Dephysicalize(false);
            }
            else {
                node->PhysicalizeNow(true);
            }
        }
    }

    Gate::Pose Gate::Motion(Shared::Entities::GateState motionState, float phase) const {
        const bool opening  = motionState == Shared::Entities::GateState::Opening;
        const int forwardId = opening ? _identity.openAnimationId : _identity.closeAnimationId;
        if (forwardId >= 0) {
            return Pose {forwardId, phase};
        }

        // The drawbridge ships only a lifting clip, so lowering it is that clip
        // run from 1 to 0 -- free, since playback is driven by time anyway.
        const int reverseId = opening ? _identity.closeAnimationId : _identity.openAnimationId;
        return Pose {reverseId, 1.0F - phase};
    }

    std::int64_t Gate::Elapsed(std::uint64_t nowLocal, std::int64_t serverClockOffset, bool serverClockKnown) const {
        // Held at the start rather than guessed at until the peer has a clock
        // differential.
        if (!serverClockKnown) {
            return 0;
        }
        const std::int64_t localStart = static_cast<std::int64_t>(cycleStartMs) - serverClockOffset;
        return static_cast<std::int64_t>(nowLocal) - localStart;
    }

    Gate::Pose Gate::Resolve(std::uint64_t nowLocal, std::int64_t elapsed) const {
        if (Shared::Entities::IsGateMoving(state)) {
            return Motion(state, PhaseAfter(elapsed));
        }

        const bool open  = state == Shared::Entities::GateState::Open;
        const int idleId = open ? _identity.openedIdleAnimationId : _identity.closedIdleAnimationId;
        if (idleId >= 0) {
            // Scenery, so it runs off the local clock; nobody can see the phase.
            return Pose {idleId, LoopPhase(nowLocal, open ? Asset().openedIdleDurationMs : Asset().closedIdleDurationMs)};
        }

        // Nothing shipped for this side: stand on the end of the motion that
        // arrives there.
        return Motion(open ? Shared::Entities::GateState::Opening : Shared::Entities::GateState::Closing, 1.0F);
    }

    void Gate::ApplyPose(const Pose &pose) {
        if (pose.animationId < 0) {
            return;
        }

        SDK::ISkeletonAnim *skeleton = _character->GetISkeletonAnim();
        if (skeleton == nullptr) {
            return;
        }

        if (pose.animationId != _playingAnimationId || skeleton->GetNumAnimsInFIFO(kGateLayer) != 1) {
            // Cleared rather than blended: a transition leaves two animations
            // in the layer, and SetLayerNormalizedTime then does nothing.
            skeleton->ClearFIFOLayer(kGateLayer);

            SDK::CryCharAnimationParams params(static_cast<std::int32_t>(kGateLayer), kDrivenFlags);
            params.m_fPlaybackSpeed = 0.0F; // position comes from the seek below
            if (!skeleton->StartAnimationById(pose.animationId, params)) {
                _playingAnimationId = -1;
                return;
            }
            _playingAnimationId = pose.animationId;
        }

        // Engine order: the seek fires the anim events crossed since the last
        // one, SetLayerNormalizedTime moves the skeleton.
        skeleton->ManualSeekAnimationInFIFO(kGateLayer, 0, pose.normalizedTime, true);
        skeleton->SetLayerNormalizedTime(kGateLayer, pose.normalizedTime);
    }

    void Gate::Drive(std::uint64_t nowLocal, std::int64_t serverClockOffset, bool serverClockKnown) {
        if (_character == nullptr) {
            return;
        }

        // Both handles belong to the engine, which may have freed either since
        // the last tick. The id lookup is an array read, GetCharacter a slot.
        auto *entitySystem = SDK::IEntitySystem::GetInstance();
        if (entitySystem == nullptr || entitySystem->GetEntity(_entityId) != _entity || _entity->GetCharacter(0) != _character) {
            Unbind(true);
            return;
        }

        const std::int64_t elapsed = Elapsed(nowLocal, serverClockOffset, serverClockKnown);
        ApplyPose(Resolve(nowLocal, elapsed));
        if (nowLocal - _levelBrushesCheckedAt >= kLevelBrushRecheckMs) {
            HideLevelBrushes(true, nowLocal);
        }
    }
} // namespace KCDC::Features::Door
