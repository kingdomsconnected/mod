#include "siege.h"

#include "siege_module.h"

#include "core/application.h"
#include "common/module_support.h"
#include "common/spawn/spawn_budget.h"

#include "shared/features/siege/siege_ballistics.h"

#include "sdk/cryengine/animation/c_skeleton_anim.h"
#include "sdk/cryengine/animation/cry_char_animation_params.h"
#include "sdk/cryengine/animation/i_animation_set.h"
#include "sdk/cryengine/animation/i_skeleton_anim.h"
#include "sdk/cryengine/entitysystem/i_entity_system.h"

#include <logging/logger.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace KCDC::Features::Siege {
    namespace {
        constexpr const char *kLogger = "Siege";

        /** An engine's model has no animation of its own, so the base layer is free. */
        constexpr std::uint32_t kEngineLayer = 0;

        /**
         * Flags for a clip whose playback position the mod controls.
         * Exclusive mode keeps the clip queued and its sound events active.
         * Looping keeps a finished clip available for seeking.
         */
        constexpr std::uint32_t kDrivenFlags = SDK::CA_TRACK_VIEW_EXCLUSIVE | SDK::CA_ALLOW_ANIM_RESTART | SDK::CA_LOOP_ANIMATION;

        /** The rate the catalog samples the sling path at. */
        constexpr float kSlingSamplesPerMs = 30.0F / 1000.0F;

        /** How long a sling stone the spawner refused waits before it is asked for again. */
        constexpr MafiaNet::Time kSlingRetryMs = 1000;

        /** How fast a gun is laid, in radians a second: a crew with handspikes under the bed. */
        const float kLayRate = glm::radians(12.0F);

        /** The kick: how long the gun takes to run back, then to be run out again, in milliseconds. */
        constexpr float kRecoilOutMs  = 70.0F;
        constexpr float kRecoilHomeMs = 1330.0F;

        /** How long a wreck's dust and a fuse's spark are kept, in milliseconds. */
        constexpr std::uint32_t kWreckEffectMs = 8000;
        constexpr std::uint32_t kFuseEffectMs  = 3000;
    } // namespace

    void SiegeEngine::DeallocReplica(MafiaNet::Connection_RM3 *) {
        // Dropped first, so an engine still queued is never built for a
        // replica that has already gone.
        ReleaseBuildTicket();
        if (auto *app = KCDC::Core::gApplication.get()) {
            app->GetPropService().QueueDespawn(_entityId);
            app->GetPropService().QueueDespawn(_slingStoneId);
        }
        delete this;
    }

    void SiegeEngine::Unbind() {
        // Not removed: the entity system holding these ids is gone too.
        ReleaseBuildTicket();
        _entityId           = 0;
        _slingStoneId       = 0;
        _builtModel         = nullptr;
        _character          = nullptr;
        _playingAnimationId = -1;
        _nextBuildAttempt   = 0;
        _buildRefused       = false;
        std::fill(std::begin(_clipIds), std::end(_clipIds), -1);
    }

    SDK::IEntity *SiegeEngine::ResolveNative() const {
        return KCDC::Common::ResolveEntity(_entityId);
    }

    const char *SiegeEngine::ModelNow() const {
        const char *broken = Asset().brokenModel;
        return state == Shared::Entities::SiegeState::Wrecked && broken[0] != '\0' ? broken : Asset().model;
    }

    glm::quat SiegeEngine::MeshRotation() const {
        const Shared::Game::SiegeEngineAsset &asset = Asset();
        glm::quat turned                            = glm::normalize(rotation);
        if (Shared::Game::Siege::LaysItsBarrel(asset) && state != Shared::Entities::SiegeState::Wrecked) {
            turned = turned * glm::angleAxis(_shownElevation, glm::vec3(1.0F, 0.0F, 0.0F));
        }
        if (state == Shared::Entities::SiegeState::Wrecked && asset.brokenModel[0] == '\0') {
            turned = turned * glm::angleAxis(glm::radians(asset.wreckTiltXDeg), glm::vec3(1.0F, 0.0F, 0.0F)) * glm::angleAxis(glm::radians(asset.wreckTiltYDeg), glm::vec3(0.0F, 1.0F, 0.0F));
        }
        return turned * glm::angleAxis(glm::radians(asset.modelYawDeg), glm::vec3(0.0F, 0.0F, 1.0F));
    }

    glm::vec3 SiegeEngine::MeshPosition() const {
        const Shared::Game::SiegeEngineAsset &asset = Asset();
        const glm::quat facing                      = glm::normalize(rotation);
        glm::vec3 at                                = position;
        if (Shared::Game::Siege::LaysItsBarrel(asset) && state != Shared::Entities::SiegeState::Wrecked) {
            // Raised about the back of its bed, which stays where it is: the
            // mesh's own origin swings up around the pivot with the barrel.
            const glm::vec3 pivot = ToGlm(asset.pivot);
            at += facing * (pivot - glm::angleAxis(_shownElevation, glm::vec3(1.0F, 0.0F, 0.0F)) * pivot);
            at += facing * glm::vec3(0.0F, -_recoil, 0.0F);
        }
        if (state == Shared::Entities::SiegeState::Wrecked && asset.brokenModel[0] == '\0') {
            at.z -= asset.wreckSink;
        }
        return at;
    }

    SDK::IPhysicalEntity *SiegeEngine::GetPhysics() const {
        SDK::IEntity *entity = ResolveNative();
        return entity != nullptr ? entity->GetPhysics() : nullptr;
    }

    std::optional<Shared::Game::Siege::Launch> SiegeEngine::AimedLaunch() const {
        if (!aimed) {
            return std::nullopt;
        }
        Shared::Game::Siege::Launch launch;
        if (Shared::Game::Siege::SolveLaunch(Asset(), position, glm::normalize(rotation), aimTarget, launch) != Shared::Game::Siege::AimResult::Ok) {
            return std::nullopt;
        }
        return launch;
    }

    float SiegeEngine::GroundDistanceTo(const glm::vec3 &point) const {
        SDK::IEntity *entity = ResolveNative();
        if (entity == nullptr) {
            return glm::length(glm::vec2(point - position));
        }
        SDK::AABB bounds {};
        entity->GetWorldBounds(bounds);
        const float dx = std::max({bounds.min.x - point.x, 0.0F, point.x - bounds.max.x});
        const float dy = std::max({bounds.min.y - point.y, 0.0F, point.y - bounds.max.y});
        return std::sqrt(dx * dx + dy * dy);
    }

    void SiegeEngine::Kick(std::uint64_t nowLocal) {
        if (Asset().recoil > 0.0F) {
            _recoilStart = std::max<std::uint64_t>(1, nowLocal);
        }
    }

    void SiegeEngine::RequestBuild() {
        if (_buildTicket != nullptr || _buildRefused) {
            return;
        }
        const MafiaNet::Time now = MafiaNet::GetTime();
        if (now < _nextBuildAttempt) {
            return;
        }
        _nextBuildAttempt = now + KCDC::Common::Spawn::SpawnRetryDelayMs(GetNetworkID());

        KCDC::Common::Spawn::PropSpawnRequest request;
        request.model   = ModelNow();
        request.physics = KCDC::Common::Spawn::PropPhysics::Static;
        request.world   = KCDC::Common::Spawn::WorldPlacement {KCDC::Common::ToNative(MeshPosition()), KCDC::Common::ToNative(MeshRotation())};

        _builtModel      = ModelNow();
        _appliedPosition = MeshPosition();
        _appliedRotation = MeshRotation();
        _buildTicket     = KCDC::Core::gApplication->GetPropSpawner().Request(request, [this](KCDC::Common::Spawn::PropSpawnTicket &ticket, bool built) {
            OnBuilt(ticket, built);
        });
    }

    void SiegeEngine::OnBuilt(KCDC::Common::Spawn::PropSpawnTicket &ticket, bool built) {
        _buildTicket = nullptr;
        if (!built) {
            if (!KCDC::Common::Spawn::IsRetryablePropSpawnResult(ticket.GetResult())) {
                Framework::Logging::GetLogger(kLogger)->error("Could not build {} {}: {}", Asset().name, GetNetworkID(), KCDC::Common::Spawn::PropSpawnResultToString(ticket.GetResult()));
                _buildRefused = true;
            }
            return;
        }
        _entityId = ticket.GetEntityId();
        SiegeModule::DrawFar(_entityId);
        Framework::Logging::GetLogger(kLogger)->info("Built {} {} as entity {} ({})", Asset().name, GetNetworkID(), _entityId, _builtModel);
    }

    void SiegeEngine::ReleaseBuildTicket() {
        if (_buildTicket == nullptr) {
            return;
        }
        if (auto *app = KCDC::Core::gApplication.get()) {
            app->GetPropSpawner().Return(_buildTicket);
        }
        _buildTicket = nullptr;
    }

    bool SiegeEngine::BindCharacter(SDK::IEntity *entity) {
        if (_character != nullptr && entity->GetCharacter(0) == _character) {
            return true;
        }
        _character          = nullptr;
        _playingAnimationId = -1;
        _playingTime        = -1.0F;

        // A `.chr` reaches slot 0 through CEntity::LoadCharacter: the script
        // binding's LoadObject sends `chr`, `cdf` and `cga` there
        // (0x1820F8F10). A static engine has none and needs none.
        SDK::ICharacterInstance *character = entity->GetCharacter(0);
        SDK::IAnimationSet *animationSet   = character != nullptr ? character->GetIAnimationSet() : nullptr;
        SDK::ISkeletonAnim *skeleton       = character != nullptr ? character->GetISkeletonAnim() : nullptr;
        if (animationSet == nullptr || skeleton == nullptr) {
            return false;
        }

        for (std::size_t step = 0; step < 4; ++step) {
            const char *clip = Asset().clips[step];
            _clipIds[step]   = clip[0] != '\0' ? animationSet->GetAnimIDByName(clip) : -1;
        }

        // Claimed before anything is started: outside this mode a hand-driven
        // clip is dropped from the queue.
        skeleton->SetTrackViewExclusive(1);
        _character = character;
        return true;
    }

    std::int64_t SiegeEngine::Elapsed(std::uint64_t nowLocal, std::int64_t serverClockOffset, bool serverClockKnown) const {
        if (!serverClockKnown || stepStartMs == 0) {
            return 0;
        }
        const std::int64_t localStart = static_cast<std::int64_t>(stepStartMs) - serverClockOffset;
        return static_cast<std::int64_t>(nowLocal) - localStart;
    }

    float SiegeEngine::AuthoredMs(std::int64_t elapsed) const {
        const std::uint32_t authored = Asset().durationsMs[static_cast<std::size_t>(StepOf(state))];
        return PhaseAfter(elapsed) * static_cast<float>(authored);
    }

    SiegeEngine::Pose SiegeEngine::Resolve(std::int64_t elapsed) const {
        switch (state) {
        case Shared::Entities::SiegeState::Idle:
        case Shared::Entities::SiegeState::Wrecked: return Pose {_clipIds[static_cast<std::size_t>(Shared::Game::SiegeStep::Idle)], 0.0F};
        case Shared::Entities::SiegeState::Ready: return Pose {_clipIds[static_cast<std::size_t>(Shared::Game::SiegeStep::Load)], 1.0F};
        default: return Pose {_clipIds[static_cast<std::size_t>(StepOf(state))], PhaseAfter(elapsed)};
        }
    }

    void SiegeEngine::ApplyPose(const Pose &pose) {
        if (_character == nullptr || pose.animationId < 0) {
            return;
        }
        SDK::ISkeletonAnim *skeleton = _character->GetISkeletonAnim();
        if (skeleton == nullptr) {
            return;
        }

        const bool restart = pose.animationId != _playingAnimationId || skeleton->GetNumAnimsInFIFO(kEngineLayer) != 1;
        // A settled engine holds one frame: the clip plays at speed 0, so the
        // layer stays where the last seek left it and nothing need be said.
        if (!restart && pose.normalizedTime == _playingTime) {
            return;
        }
        if (restart) {
            // Cleared rather than blended: a transition leaves two animations
            // in the layer, and SetLayerNormalizedTime then does nothing.
            skeleton->ClearFIFOLayer(kEngineLayer);

            SDK::CryCharAnimationParams params(static_cast<std::int32_t>(kEngineLayer), kDrivenFlags);
            params.m_fPlaybackSpeed = 0.0F; // position comes from the seek below
            if (!skeleton->StartAnimationById(pose.animationId, params)) {
                _playingAnimationId = -1;
                return;
            }
            _playingAnimationId = pose.animationId;
        }

        // Engine order: the seek fires the anim events crossed since the last
        // one, SetLayerNormalizedTime moves the skeleton.
        skeleton->ManualSeekAnimationInFIFO(kEngineLayer, 0, pose.normalizedTime, true);
        skeleton->SetLayerNormalizedTime(kEngineLayer, pose.normalizedTime);
        _playingTime = pose.normalizedTime;
    }

    std::optional<glm::vec3> SiegeEngine::SlingPoint(std::int64_t elapsed) const {
        const Shared::Game::SiegeEngineAsset &asset = Asset();
        if (asset.sling.empty()) {
            return std::nullopt;
        }
        const Shared::Game::SiegePoint &rest = asset.sling.front();

        switch (state) {
        case Shared::Entities::SiegeState::Loading:
            // Laid in the sling by the crew near the end of the load.
            if (AuthoredMs(elapsed) < static_cast<float>(asset.projectileLaidMs)) {
                return std::nullopt;
            }
            return ToGlm(rest);
        case Shared::Entities::SiegeState::Ready: return ToGlm(rest);
        case Shared::Entities::SiegeState::Firing: {
            // Swung out on the path the cutscene stone takes until the clip
            // lets it go; from then on the shot draws it.
            const float authored = AuthoredMs(elapsed);
            if (authored >= static_cast<float>(asset.releaseMs)) {
                return std::nullopt;
            }
            const float sample   = authored * kSlingSamplesPerMs;
            const std::size_t at = std::min(static_cast<std::size_t>(sample), asset.sling.size() - 1);
            const std::size_t to = std::min(at + 1, asset.sling.size() - 1);
            const float weight   = std::clamp(sample - static_cast<float>(at), 0.0F, 1.0F);
            const glm::vec3 from = ToGlm(asset.sling[at]);
            const glm::vec3 next = ToGlm(asset.sling[to]);
            return from + (next - from) * weight;
        }
        default: return std::nullopt;
        }
    }

    void SiegeEngine::PlaceSlingStone(const std::optional<glm::vec3> &point) {
        auto *app = KCDC::Core::gApplication.get();
        if (!point) {
            if (_slingStoneId != 0) {
                app->GetPropSpawner().DespawnNow(_slingStoneId);
                _slingStoneId = 0;
            }
            return;
        }

        const glm::vec3 world = position + rotation * *point;
        if (SDK::IEntity *stone = KCDC::Common::ResolveEntity(_slingStoneId)) {
            // Moved only when it has to: a loaded engine holds it still.
            if (world != _slingStonePoint) {
                stone->SetPosRotScale(KCDC::Common::ToNative(world), KCDC::Common::ToNative(glm::normalize(rotation)), SDK::Vec3 {1.0F, 1.0F, 1.0F});
                _slingStonePoint = world;
            }
            return;
        }

        const MafiaNet::Time now = MafiaNet::GetTime();
        if (now < _slingRetryAt) {
            return;
        }
        KCDC::Common::Spawn::PropSpawnRequest request;
        request.model   = Asset().projectileModel;
        request.physics = KCDC::Common::Spawn::PropPhysics::None;
        request.world   = KCDC::Common::Spawn::WorldPlacement {KCDC::Common::ToNative(world), KCDC::Common::ToNative(glm::normalize(rotation))};
        _slingStoneId    = app->GetPropSpawner().SpawnNow(request).prop.entityId;
        _slingStonePoint = world;
        if (_slingStoneId == 0) {
            _slingRetryAt = now + kSlingRetryMs;
        }
        else {
            SiegeModule::DrawFar(_slingStoneId);
        }
    }

    void SiegeEngine::Settle(std::uint64_t nowLocal, float frameSeconds) {
        if (!Shared::Game::Siege::LaysItsBarrel(Asset())) {
            return;
        }
        // Laid towards what the aim solves to; level when there is none.
        const std::optional<Shared::Game::Siege::Launch> launch = AimedLaunch();
        const float wanted                                      = launch ? Shared::Game::Siege::ElevationOf(launch->velocity) : 0.0F;
        const float step                                        = kLayRate * std::max(frameSeconds, 0.0F);
        _shownElevation += std::clamp(wanted - _shownElevation, -step, step);

        if (_recoilStart == 0) {
            _recoil = 0.0F;
            return;
        }
        const float since = static_cast<float>(nowLocal > _recoilStart ? nowLocal - _recoilStart : 0);
        if (since < kRecoilOutMs) {
            _recoil = Asset().recoil * since / kRecoilOutMs;
        }
        else if (since < kRecoilOutMs + kRecoilHomeMs) {
            const float home = 1.0F - (since - kRecoilOutMs) / kRecoilHomeMs;
            _recoil          = Asset().recoil * home * home;
        }
        else {
            _recoil      = 0.0F;
            _recoilStart = 0;
        }
    }

    void SiegeEngine::OnStateEntered(Shared::Entities::SiegeState entered) {
        const Shared::Game::SiegeEngineAsset &asset = Asset();
        SiegeModule &module                         = SiegeModule::Get();
        if (entered == Shared::Entities::SiegeState::Firing && asset.fuseEffect[0] != '\0') {
            // The match to the touch hole, which rides the laid barrel.
            const glm::vec3 local = Shared::Game::Siege::LocalReleasePoint(asset, _shownElevation) - ToGlm(asset.releaseOffset) + ToGlm(asset.touchHole);
            const glm::vec3 at    = position + glm::normalize(rotation) * local;
            module.PlayEffect(asset.fuseEffect, at, kFuseEffectMs);
            module.PlaySound(asset.fuseSound, at);
        }
        if (entered == Shared::Entities::SiegeState::Wrecked) {
            const glm::vec3 at = position + glm::vec3(0.0F, 0.0F, 1.0F);
            module.PlayEffect(asset.wreckEffect, at, kWreckEffectMs);
            module.PlaySound(asset.wreckSound, at);
        }
    }

    void SiegeEngine::Frame(std::uint64_t nowLocal, std::int64_t serverClockOffset, bool serverClockKnown, float frameSeconds) {
        // A step joined in the middle is not one this machine watched begin:
        // its fuse burned and its dust settled before this client was here.
        if (!_seen) {
            _seen      = true;
            _seenState = state;
        }
        else if (state != _seenState) {
            _seenState = state;
            OnStateEntered(state);
        }

        Settle(nowLocal, frameSeconds);

        SDK::IEntity *entity = ResolveNative();
        if (entity != nullptr && _builtModel != ModelNow()) {
            // Wrecked into the broken mesh, or repaired out of it: built again.
            KCDC::Core::gApplication->GetPropSpawner().DespawnNow(_entityId);
            _entityId  = 0;
            _character = nullptr;
            entity     = nullptr;
        }
        if (entity == nullptr) {
            // A level change takes the entity without saying so.
            _entityId  = 0;
            _character = nullptr;
            RequestBuild();
            PlaceSlingStone(std::nullopt);
            return;
        }

        const glm::vec3 meshPosition = MeshPosition();
        const glm::quat meshRotation = MeshRotation();
        if (meshPosition != _appliedPosition || meshRotation != _appliedRotation) {
            entity->SetPosRotScale(KCDC::Common::ToNative(meshPosition), KCDC::Common::ToNative(meshRotation), SDK::Vec3 {1.0F, 1.0F, 1.0F});
            _appliedPosition = meshPosition;
            _appliedRotation = meshRotation;
        }

        const std::int64_t elapsed = Elapsed(nowLocal, serverClockOffset, serverClockKnown);
        // A static engine -- the cannon -- has no clips and no character to find.
        if (Asset().clips[static_cast<std::size_t>(Shared::Game::SiegeStep::Idle)][0] != '\0' && BindCharacter(entity)) {
            ApplyPose(Resolve(elapsed));
        }
        PlaceSlingStone(SlingPoint(elapsed));
    }
} // namespace KCDC::Features::Siege
