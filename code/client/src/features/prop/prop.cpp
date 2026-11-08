#include "prop.h"
#include "prop_service.h"

#include "common/module_support.h"
#include "common/spawn/spawn_budget.h"

#include "core/application.h"

#include "sdk/cryengine/entitysystem/i_entity_system.h"
#include "sdk/cryengine/physics/pe_action_set_velocity.h"
#include "sdk/cryengine/physics/pe_body_actions.h"
#include "sdk/cryengine/physics/pe_params_part.h"

#include "shared/pose_math.h"

#include "shared/game/prop_models.h"
#include "shared/streamed_assets/streamed_asset_policy.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <logging/logger.h>
#include <string_view>

namespace KCDC::Features::Prop {
    namespace {
        constexpr const char *kLogger = "Prop";

        /** The same interval a horse holds its spawn on. */
        constexpr MafiaNet::Time kBuildRetryIntervalMs = 1000;

        // A push lands after the next physics step, and a body lying still is asleep until then, so the simulator does not take it for resting before this.
        constexpr MafiaNet::Time kMinMotionMs = 300;

        // Past this many waiting for a body, the oldest goes.
        constexpr std::size_t kMaxPendingMotions = 8;

        // An observer moves its copy towards the stream over part of the transform interval, as a falling ground item does.
        constexpr float kFollowSmoothingMs = 100.0F;

        // A body is still while it is asleep or moves less than this in a frame, and resting once it has been still this long. The engine wakes a body lying on the ground for a frame at a time as it settles, without moving it, so being awake alone does not end a rest.
        constexpr float kStillStep         = 0.0005F;
        constexpr MafiaNet::Time kSettleMs = 500;

        // A resting body has to really move -- this far, or this much of a turn (about a degree) -- before it is moving again, so a stop is reported once.
        constexpr float kRestDisplacement = 0.02F;
        constexpr float kRestTurn         = 4.0e-5F;

        /** Gives a body exactly `target` kilograms, its parts in the proportions they have: the engine scales a body's mass with the scale it is placed at. */
        void ScaleBodyMass(SDK::IPhysicalEntity &body, float target) {
            SDK::pe_status_dynamics dynamics;
            if (body.GetStatus(&dynamics) == 0 || !(dynamics.mass > 0.0F) || std::abs(dynamics.mass - target) <= target * 0.005F) {
                return;
            }
            const float ratio = target / dynamics.mass;
            SDK::pe_status_nparts parts;
            const int count = body.GetStatus(&parts);
            for (int index = 0; index < count; ++index) {
                SDK::pe_params_part part;
                part.ipart = index;
                if (body.GetParams(&part) == 0 || !std::isfinite(part.mass)) {
                    continue;
                }
                SDK::pe_params_part scaled;
                scaled.ipart = index;
                scaled.mass  = part.mass * ratio;
                body.SetParams(&scaled);
            }
        }

        KCDC::Common::Spawn::PropPhysics ToNativePhysics(Shared::Entities::PropPhysics physics) {
            switch (physics) {
            case Shared::Entities::PropPhysics::Rigid: return KCDC::Common::Spawn::PropPhysics::Rigid;
            case Shared::Entities::PropPhysics::Static: return KCDC::Common::Spawn::PropPhysics::Static;
            case Shared::Entities::PropPhysics::None: return KCDC::Common::Spawn::PropPhysics::None;
            }
            return KCDC::Common::Spawn::PropPhysics::Static;
        }
    } // namespace

    void Prop::DeallocReplica(MafiaNet::Connection_RM3 *) {
        // Dropped first, so a prop that was still queued is never built for a
        // replica that has already gone.
        ReleaseBuildTicket();
        KCDC::Core::gApplication->GetPropService().QueueDespawn(_entityId);
        SDK::IRenderNode *brush        = nullptr;
        SDK::IPhysicalEntity *collider = nullptr;
        _brush.Release(brush, collider);
        KCDC::Core::gApplication->GetPropService().QueueRelease(brush, collider);
        delete this;
    }

    void Prop::OnConstructed() {
        _dirty = true;
    }

    void Prop::OnDeserialized(bool) {
        _dirty = true;
    }

    void Prop::OnStateForced() {
        if (WasPoseForced()) {
            _poseForced = true;
        }
        _dirty = true;
    }

    void Prop::Unbind() {
        // The request named a world that no longer exists; the rebuild after
        // the level change asks again.
        ReleaseBuildTicket();

        // Not DestroyNative: the entity system holding these ids is gone too,
        // and so is the octree the brush was registered in.
        _entityId = 0;
        _brush.Forget();
        _builtModelIndex = Shared::Entities::PropEntity::kNoModel;
        _builtStreamedModel.clear();
        _builtScale       = 0.0F;
        _nextBuildAttempt = 0;
        _buildDeferred    = false;
        _buildRefused     = false;
        _dirty            = true;
        DropBody();
    }

    void Prop::QueueMotion(const Shared::RPC::PropMotion &motion, MafiaNet::Time receivedAt) {
        if (_pendingMotions.size() >= kMaxPendingMotions) {
            _pendingMotions.erase(_pendingMotions.begin());
        }
        _pendingMotions.push_back({motion, receivedAt});
        _dirty = true;
    }

    void Prop::DropBody() {
        _pendingMotions.clear();
        _partMasses.clear();
        _roleTaken   = false;
        _visualKnown = false;
    }

    void Prop::SetBodyMass(SDK::IPhysicalEntity &body, bool real) {
        if (_partMasses.empty()) {
            SDK::pe_status_nparts parts;
            const int count = body.GetStatus(&parts);
            for (int index = 0; index < count; ++index) {
                SDK::pe_params_part part;
                part.ipart = index;
                _partMasses.push_back(body.GetParams(&part) != 0 && std::isfinite(part.mass) ? part.mass : 0.0F);
            }
        }
        for (std::size_t index = 0; index < _partMasses.size(); ++index) {
            SDK::pe_params_part part;
            part.ipart = static_cast<std::int32_t>(index);
            part.mass  = real ? _partMasses[index] : 0.0F;
            body.SetParams(&part);
        }
    }

    void Prop::FrameBody(SDK::IEntity &entity) {
        SDK::IPhysicalEntity *body = entity.GetPhysics();
        if (body == nullptr) {
            return;
        }

        const MafiaNet::Time now = MafiaNet::GetTime();
        std::erase_if(_pendingMotions, [now](const QueuedMotion &queued) {
            return now - queued.receivedAt > kMotionShelfLifeMs;
        });

        // A handover, or the first frame of a body: it starts from the pose the replica holds, still.
        const bool simulating = IsSimulating();
        if (!_roleTaken || simulating != _simulating) {
            _simulating = simulating;
            _roleTaken  = true;
            SetBodyMass(*body, simulating);
            SDK::pe_action_reset stop;
            body->Action(&stop);
            entity.SetPosRotScale(KCDC::Common::ToNative(position), KCDC::Common::ToNative(glm::normalize(rotation)), KCDC::Common::ToNative(axisScale * scale));
            _visualPosition  = position;
            _visualRotation  = glm::normalize(rotation);
            _visualKnown     = true;
            _appliedPosition = position;
            _appliedRotation = rotation;
            _lastMotionAt    = now;
            _stillSince      = now;
            Framework::Logging::GetLogger(kLogger)->debug("Prop {} body is {} here", GetNetworkID(), simulating ? "simulated" : "followed");
        }

        if (simulating) {
            Simulate(entity, *body);
        }
        else {
            Follow(entity);
        }
    }

    void Prop::Simulate(SDK::IEntity &entity, SDK::IPhysicalEntity &body) {
        const MafiaNet::Time now = MafiaNet::GetTime();

        // The server moved it: stopped, and where it says.
        if (_poseForced.exchange(false)) {
            SDK::pe_action_reset stop;
            body.Action(&stop);
            entity.SetPosRotScale(KCDC::Common::ToNative(position), KCDC::Common::ToNative(glm::normalize(rotation)), KCDC::Common::ToNative(axisScale * scale));
            _lastMotionAt = now;
        }

        for (const QueuedMotion &queued : _pendingMotions) {
            const Shared::RPC::PropMotion &motion = queued.motion;
            switch (motion.Kind()) {
            case Shared::RPC::PropMotionKind::Impulse: {
                SDK::pe_action_impulse push;
                push.impulse = KCDC::Common::ToNative(motion.linear);
                if (glm::dot(motion.angular, motion.angular) > 0.0F) {
                    push.angImpulse = KCDC::Common::ToNative(motion.angular);
                }
                if (motion.hasPoint) {
                    push.point = KCDC::Common::ToNative(motion.point);
                }
                body.Action(&push);
                break;
            }
            case Shared::RPC::PropMotionKind::Velocity: {
                SDK::pe_action_set_velocity velocity;
                velocity.v = KCDC::Common::ToNative(motion.linear);
                velocity.w = KCDC::Common::ToNative(motion.angular);
                body.Action(&velocity);
                break;
            }
            case Shared::RPC::PropMotionKind::Wake:
            case Shared::RPC::PropMotionKind::Sleep: {
                SDK::pe_action_awake awake;
                awake.bAwake = motion.Kind() == Shared::RPC::PropMotionKind::Wake ? 1 : 0;
                body.Action(&awake);
                break;
            }
            }
            _lastMotionAt = now;
            Framework::Logging::GetLogger(kLogger)->debug("Prop {} motion (kind {}) done to its body", GetNetworkID(), motion.kind);
        }
        _pendingMotions.clear();

        // The pose goes upstream as it is; the rest, on the reliable channel, once the body has been still a while.
        const glm::vec3 sampledPosition = KCDC::Common::ToGlm(entity.GetWorldPos());
        const glm::quat sampledRotation = glm::normalize(KCDC::Common::ToGlm(entity.GetWorldRotation()));
        if (!Shared::Game::IsWorldPosition(sampledPosition) || !Shared::Game::IsUsableRotation(sampledRotation)) {
            return;
        }
        SDK::pe_status_awake awake;
        const bool asleep = body.GetStatus(&awake) == 0;
        const bool still  = now - _lastMotionAt >= kMinMotionMs && (asleep || (glm::distance(position, sampledPosition) < kStillStep && 1.0F - std::abs(glm::dot(glm::normalize(rotation), sampledRotation)) < kRestTurn));
        position          = sampledPosition;
        rotation          = sampledRotation;
        _appliedPosition  = sampledPosition;
        _appliedRotation  = sampledRotation;
        if (!still) {
            _stillSince = now;
        }

        if (resting) {
            if (glm::distance(restPosition, sampledPosition) > kRestDisplacement || 1.0F - std::abs(glm::dot(restRotation, sampledRotation)) > kRestTurn) {
                resting = false;
            }
            return;
        }
        if (now - _stillSince >= kSettleMs) {
            resting      = true;
            restPosition = sampledPosition;
            restRotation = sampledRotation;
            Framework::Logging::GetLogger(kLogger)->debug("Prop {} came to rest at {:.2f}, {:.2f}, {:.2f}", GetNetworkID(), sampledPosition.x, sampledPosition.y, sampledPosition.z);
        }
    }

    void Prop::Follow(SDK::IEntity &entity) {
        const MafiaNet::Time now = MafiaNet::GetTime();
        const glm::quat target   = glm::normalize(rotation);
        if (resting || !_visualKnown) {
            _visualPosition = position;
            _visualRotation = target;
            _visualKnown    = true;
        }
        else {
            const float blend = std::clamp(static_cast<float>(now - _lastFollowAt) / kFollowSmoothingMs, 0.0F, 1.0F);
            _visualPosition   = glm::mix(_visualPosition, position, blend);
            _visualRotation   = glm::normalize(glm::slerp(_visualRotation, target, blend));
        }
        _lastFollowAt = now;
        if (_visualPosition == _appliedPosition && _visualRotation == _appliedRotation) {
            return;
        }
        entity.SetPosRotScale(KCDC::Common::ToNative(_visualPosition), KCDC::Common::ToNative(_visualRotation), KCDC::Common::ToNative(axisScale * scale));
        _appliedPosition = _visualPosition;
        _appliedRotation = _visualRotation;
    }

    SDK::IEntity *Prop::ResolveNative() const {
        auto *entitySystem   = SDK::IEntitySystem::GetInstance();
        SDK::IEntity *entity = entitySystem != nullptr && _entityId != 0 ? entitySystem->GetEntity(_entityId) : nullptr;
        return entity != nullptr && !entity->IsGarbage() ? entity : nullptr;
    }

    bool Prop::NeedsRebuild() const {
        return _builtModelIndex != modelIndex || _builtStreamedModel != streamedModel || _builtPhysics != physics || _builtScale != scale || _builtAxisScale != axisScale || _builtMass != mass;
    }

    std::string Prop::ModelName() const {
        if (!streamedModel.empty()) {
            return streamedModel;
        }
        const Shared::Game::PropModelEntry *model = Shared::Game::PropModelAt(modelIndex);
        return model != nullptr ? std::string(model->name) : std::string();
    }

    void Prop::DestroyNative() {
        // The body is gone, and the one built next takes its part afresh. Motions not yet done wait for that one.
        _partMasses.clear();
        _roleTaken   = false;
        _visualKnown = false;
        _brush.Destroy();

        if (_entityId == 0) {
            return;
        }

        if (auto *entitySystem = SDK::IEntitySystem::GetInstance()) {
            entitySystem->RemoveEntity(_entityId, false);
        }
        _entityId = 0;
    }

    bool Prop::BuildsAsBrush(const std::string &model, Shared::Entities::PropPhysics physics) {
        // A brush's collider is a static one, and only a `.cgf` is a stat
        // object; anything else loads as a character, which needs an entity.
        constexpr std::string_view kStatObj = ".cgf";
        if (physics == Shared::Entities::PropPhysics::Rigid || model.size() < kStatObj.size()) {
            return false;
        }
        return std::equal(kStatObj.begin(), kStatObj.end(), model.end() - static_cast<std::ptrdiff_t>(kStatObj.size()), [](char a, char b) {
            return a == static_cast<char>(std::tolower(static_cast<unsigned char>(b)));
        });
    }

    void Prop::RecordBuilt() {
        // What was asked for, not what the wire says now: a change that landed
        // while the request was queued leaves the prop needing a rebuild, and
        // that is exactly what NeedsRebuild should then see.
        _builtModelIndex    = _requestedModelIndex;
        _builtStreamedModel = _requestedStreamedModel;
        _builtPhysics       = _requestedPhysics;
        _builtScale         = _requestedScale;
        _builtAxisScale     = _requestedAxisScale;
        _builtMass          = _requestedMass;
        _buildDeferred      = false;

        // The build already placed it at the transform the request carried;
        // the first Frame compares against that, so a prop that never moves
        // is never written to, and one that moved while queued is.
        _appliedPosition = _requestedPosition;
        _appliedRotation = _requestedRotation;
    }

    bool Prop::BuildBrush(const std::string &model) {
        // The mesh and its final transform in one call, so the collider is
        // built where the prop stands rather than at the origin and moved.
        const SDK::Matrix34 matrix = SDK::Matrix34FromTransform(KCDC::Common::ToNative(_requestedPosition), KCDC::Common::ToNative(glm::normalize(_requestedRotation)), KCDC::Common::ToNative(_requestedAxisScale * _requestedScale));
        switch (_brush.Build(model.c_str(), matrix, _requestedPhysics != Shared::Entities::PropPhysics::None)) {
        case KCDC::Common::Spawn::StaticBrush::BuildResult::NoEngine: return false;
        case KCDC::Common::Spawn::StaticBrush::BuildResult::MeshRefused:
            Framework::Logging::GetLogger(kLogger)->error("Could not build prop {} ('{}'): the game would not load the mesh", GetNetworkID(), model);
            _buildRefused = true;
            return false;
        case KCDC::Common::Spawn::StaticBrush::BuildResult::BuiltWithoutCollider: Framework::Logging::GetLogger(kLogger)->warn("Prop {} ('{}') stands without collision: the mesh has no physics geometry", GetNetworkID(), model); break;
        case KCDC::Common::Spawn::StaticBrush::BuildResult::Built: break;
        }
        RecordBuilt();
        return true;
    }

    void Prop::RequestBuild(KCDC::Common::Spawn::SpawnBudget &budget) {
        // One request in flight at a time; the spawner calls back when it runs.
        // A spent budget leaves the prop dirty, so the next frame asks again.
        if (_buildTicket != nullptr || budget.Exhausted()) {
            return;
        }

        const MafiaNet::Time now = MafiaNet::GetTime();
        if (now < _nextBuildAttempt) {
            return;
        }
        _nextBuildAttempt = now + KCDC::Common::Spawn::SpawnRetryDelayMs(GetNetworkID());

        KCDC::Common::Spawn::PropSpawnRequest request;
        if (!streamedModel.empty()) {
            // The server checked it against what it streams; the shape is checked again here, on
            // the machine whose loader it reaches.
            if (!KCDC::Shared::StreamedAssets::Policy::IsStreamedModelPath(streamedModel)) {
                Framework::Logging::GetLogger(kLogger)->error("Prop {} names '{}', which is not a streamed model path", GetNetworkID(), streamedModel);
                _buildRefused = true;
                return;
            }
            // A mesh published after this client mounted its paks: the prop stands once the
            // generation that carries it is in use, which the retry interval waits out.
            if (!KCDC::Core::gApplication->GetStreamedAssetService().HasEntry(streamedModel)) {
                if (!_buildDeferred) {
                    _buildDeferred = true;
                    Framework::Logging::GetLogger(kLogger)->info("Holding the build of prop {} until '{}' is streamed in", GetNetworkID(), streamedModel);
                }
                return;
            }
            request.model = streamedModel;
        }
        else {
            const Shared::Game::PropModelEntry *model = Shared::Game::PropModelAt(modelIndex);
            if (model == nullptr) {
                Framework::Logging::GetLogger(kLogger)->error("Prop {} names catalog row {}, which this build's catalog does not have", GetNetworkID(), modelIndex);
                _buildRefused = true;
                return;
            }
            request.model = std::string(model->path);
        }
        request.physics = ToNativePhysics(physics);
        request.scale   = scale * std::max({axisScale.x, axisScale.y, axisScale.z});
        request.mass    = mass;
        request.world   = KCDC::Common::Spawn::WorldPlacement {KCDC::Common::ToNative(position), KCDC::Common::ToNative(glm::normalize(rotation))};

        // Request rather than QueueSpawn: it hands back an entity nothing else
        // owns. A queued one joins the debug panel's list, whose "remove all"
        // would then take it away behind the server's back.
        _requestedModelIndex    = modelIndex;
        _requestedStreamedModel = streamedModel;
        _requestedPhysics       = physics;
        _requestedScale         = scale;
        _requestedAxisScale     = axisScale;
        _requestedMass          = mass;
        _requestedPosition      = position;
        _requestedRotation      = rotation;

        if (BuildsAsBrush(request.model, physics)) {
            budget.Charge();
            BuildBrush(request.model);
            return;
        }

        _buildTicket = KCDC::Core::gApplication->GetPropSpawner().Request(request, [this](KCDC::Common::Spawn::PropSpawnTicket &ticket, bool built) {
            OnBuilt(ticket, built);
        });
    }

    void Prop::OnBuilt(KCDC::Common::Spawn::PropSpawnTicket &ticket, bool built) {
        // The ticket has run either way, so it never fires again.
        _buildTicket = nullptr;

        const std::string modelName = ModelName();

        if (!built) {
            if (KCDC::Common::Spawn::IsRetryablePropSpawnResult(ticket.GetResult())) {
                // Once rather than once a second: a level load would otherwise
                // cost a log line for every prop it is holding.
                if (!_buildDeferred) {
                    _buildDeferred = true;
                    Framework::Logging::GetLogger(kLogger)->info("Holding the build of prop {} ('{}'): {}", GetNetworkID(), modelName, KCDC::Common::Spawn::PropSpawnResultToString(ticket.GetResult()));
                }
                return;
            }

            Framework::Logging::GetLogger(kLogger)->error("Could not build prop {} ('{}'): {}", GetNetworkID(), modelName, KCDC::Common::Spawn::PropSpawnResultToString(ticket.GetResult()));
            _buildRefused = true;
            return;
        }

        _entityId = ticket.GetEntityId();
        if (SDK::IEntity *entity = ResolveNative()) {
            entity->SetPosRotScale(KCDC::Common::ToNative(_requestedPosition), KCDC::Common::ToNative(glm::normalize(_requestedRotation)), KCDC::Common::ToNative(_requestedAxisScale * _requestedScale));

            // A script pushing a body needs to know what it is moving: the mass it was given -- again, once it stands at its scale -- or the one the game worked out from its collision.
            SDK::pe_status_dynamics dynamics;
            SDK::IPhysicalEntity *body = _requestedPhysics == Shared::Entities::PropPhysics::Rigid ? entity->GetPhysics() : nullptr;
            if (body != nullptr && _requestedMass > 0.0F) {
                ScaleBodyMass(*body, _requestedMass);
            }
            if (body != nullptr && body->GetStatus(&dynamics) != 0) {
                Framework::Logging::GetLogger(kLogger)->info("Prop {} ('{}') stands as a rigid body of {:.1f} kg{}", GetNetworkID(), modelName, dynamics.mass, _requestedMass > 0.0F ? ", as its script set" : ", from its collision");
            }
        }
        RecordBuilt();
    }

    void Prop::ReleaseBuildTicket() {
        if (_buildTicket == nullptr) {
            return;
        }
        if (auto *app = KCDC::Core::gApplication.get()) {
            app->GetPropSpawner().Return(_buildTicket);
        }
        _buildTicket = nullptr;
    }

    void Prop::ApplyPose() {
        if (_brush.IsBuilt()) {
            // The collider is built again where it now stands. Props are
            // scenery, and move rarely.
            _brush.SetMatrix(SDK::Matrix34FromTransform(KCDC::Common::ToNative(position), KCDC::Common::ToNative(glm::normalize(rotation)), KCDC::Common::ToNative(axisScale * scale)));
            _appliedPosition = position;
            _appliedRotation = rotation;
            return;
        }

        SDK::IEntity *entity = ResolveNative();
        if (entity == nullptr) {
            return;
        }

        // One call rather than three, so a prop being moved is never seen with
        // this frame's position and the last frame's rotation.
        entity->SetPosRotScale(KCDC::Common::ToNative(position), KCDC::Common::ToNative(glm::normalize(rotation)), KCDC::Common::ToNative(axisScale * scale));
        _appliedPosition = position;
        _appliedRotation = rotation;
    }

    void Prop::Frame(KCDC::Common::Spawn::SpawnBudget &budget) {
        // A standing prop that no update has touched has nothing to do, which
        // is what there is most of. The flag stays set while a build is being
        // held, so a prop waiting for its world still gets its retry, and
        // for a rigid prop's body, which is simulated or followed every frame.
        if (_buildRefused || !_dirty) {
            return;
        }

        // A level change takes the entity without saying so, so the binding is
        // confirmed before it is used rather than polled for in between. The
        // brush is this prop's own and goes only through DestroyNative.
        if (_entityId != 0 && ResolveNative() == nullptr) {
            _entityId = 0;
        }

        const bool standing = _entityId != 0 || _brush.IsBuilt();
        if (standing && NeedsRebuild()) {
            DestroyNative();

            // The interval is for a world that was not ready, and this one
            // plainly is; leaving it would blank a resized prop for a second.
            _nextBuildAttempt = 0;
        }

        if (_entityId == 0 && !_brush.IsBuilt()) {
            // Still dirty on the way out, so the pose is stated once it stands.
            RequestBuild(budget);
            return;
        }

        // Who runs a rigid body can change without an update arriving -- the election tells the owner directly -- so it is looked at every frame.
        if (_builtPhysics == Shared::Entities::PropPhysics::Rigid) {
            if (SDK::IEntity *entity = ResolveNative()) {
                FrameBody(*entity);
            }
            return;
        }

        if (position != _appliedPosition || rotation != _appliedRotation) {
            ApplyPose();
        }
        _dirty = false;
    }
} // namespace KCDC::Features::Prop
