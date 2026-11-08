#include <utils/safe_win32.h>

#include "siege_ladder.h"

#include "core/application.h"
#include "features/audio/audio_service.h"
#include "features/siege/siege_module.h"
#include "common/module_support.h"
#include "common/spawn/prop_spawner.h"
#include "shared/pose_math.h"
#include "sdk/cryengine/entitysystem/i_entity_class.h"
#include "sdk/cryengine/entitysystem/i_entity_system.h"
#include "sdk/cryengine/physics/c_physical_world.h"
#include "sdk/cryengine/scriptsystem/i_script_table.h"
#include "sdk/wh/entitymodule/c_action_actor.h"
#include "sdk/wh/entitymodule/c_actor_action_ladder.h"
#include "sdk/wh/entitymodule/c_actor_state_expansion.h"
#include "sdk/wh/entitymodule/c_human.h"

#include <logging/logger.h>

#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <algorithm>
#include <cstdio>
#include <optional>
#include <string>

namespace KCDC::Features::Siege {
    namespace {
        constexpr const char *kLogger = "SiegeLadder";

        /** A build refused for want of a world is tried again this often. */
        constexpr std::uint64_t kBuildRetryMs = 1000;

        /** The crash `battleLadder.xml` plays as a pushed ladder lands, and the dust its particle link throws up. */
        constexpr const char *kCrashSound  = "q_M11_gate_crash_open2";
        constexpr const char *kCrashEffect = "WH_Particels.other.q_conquest_gatecrush";
        constexpr std::uint32_t kCrashEffectMs = 6000;

        /** A tilt this close to the one applied is not applied again. */
        constexpr float kTiltEpsilon = 1e-4F;

        /**
         * The climb is fitted once the local player is this near the foot:
         * close enough that the wall's colliders are in, far enough that it
         * is done before anyone reaches the ladder.
         */
        constexpr float kFitRange = 40.0F;

        /** Where a climber's body hangs off the rungs, away from the wall, and how tall it is. */
        constexpr float kClimberStandOff = 0.45F;
        constexpr float kClimberHeight   = 1.85F;

        /** The climb's line starts this far up the ladder, clear of the ground at its foot. */
        constexpr float kClimbLineStart = 0.3F;

        /** `Ladder.lua`'s `verticalDistanceBetweenRungs`: the climb is fitted a rung at a time. */
        constexpr float kRungSpacing = 0.25F;

        /** Below this, no step off is looked for. */
        constexpr float kMinClimb = 1.0F;

        /** How far above and below the walk the step off expects a floor is searched for one. */
        constexpr float kFloorAbove = 1.0F;
        constexpr float kFloorBelow = 0.6F;

        /**
         * A walk is level: the steepest floor a step off lands on is about
         * 25 degrees, so a roof partway up a wall is not one.
         */
        constexpr float kFloorNormalZ = 0.9F;

        /**
         * And it is wide and deep enough to stand on, as far in as the step
         * off carries the body: floor under every point of a patch this wide
         * across the ladder and this deep beyond the walk, none of it further
         * than `kWalkStep` above or below the rest -- not the edge of a roof
         * or a ledge, with a wall behind it that only looks solid.
         */
        constexpr float kWalkHalfWidth = 0.35F;
        constexpr float kWalkDepth     = 0.8F;
        constexpr float kWalkStep      = 0.2F;

        /** Open air over the walk, so the step off is onto a wall top and not into a storey. */
        constexpr float kSkyClearance = 4.0F;

        /** The height the way from the top rung to the walk is traced at. */
        constexpr float kChestHeight = 1.2F;

        /** How far below the walk the wall must still be there under the step off: a window's sill is not a rim. */
        constexpr float kRimDepth = 0.4F;

        /**
         * The first thing a straight line from `from` by `step` meets,
         * skipping the ladder. `flags` picks the colliders seen: the ground
         * query's for a floor, every collider for what is in the way, so a
         * wall the player's own collider would pass still stops a ray.
         */
        std::optional<SDK::SRayHit> Cast(SDK::CPhysicalWorld &physics, const glm::vec3 &from, const glm::vec3 &step, unsigned int flags, SDK::IPhysicalEntity *ladder) {
            SDK::IPhysicalEntity *skip[1] = {ladder};
            SDK::SRayHit hit {};
            if (physics.RayWorldIntersection(KCDC::Common::ToNative(from), KCDC::Common::ToNative(step), SDK::CPhysicalWorld::kGroundObjectTypes, flags, &hit, 1, ladder != nullptr ? skip : nullptr, ladder != nullptr ? 1 : 0) > 0) {
                return hit;
            }
            return std::nullopt;
        }

        bool Blocked(SDK::CPhysicalWorld &physics, const glm::vec3 &from, const glm::vec3 &step, SDK::IPhysicalEntity *ladder) {
            return Cast(physics, from, step, SDK::CPhysicalWorld::kPickRayFlags, ladder).has_value();
        }
    } // namespace

    void SiegeLadder::DeallocReplica(MafiaNet::Connection_RM3 *) {
        if (auto *app = KCDC::Core::gApplication.get()) {
            app->GetPropService().QueueDespawn(_entityId);
        }
        delete this;
    }

    void SiegeLadder::Unbind() {
        // Not removed: the entity system holding the id is gone too.
        _entityId         = 0;
        _nextBuildAttempt = 0;
        _buildRefused     = false;
        _tiltApplied      = false;
        _climbApplied     = false;
        _brokenShown      = false;
        _climbFitted      = false;
    }

    SDK::IEntity *SiegeLadder::ResolveNative() const {
        return _entityId != 0 ? KCDC::Common::ResolveEntity(_entityId) : nullptr;
    }

    std::int64_t SiegeLadder::Elapsed(std::uint64_t nowLocal, std::int64_t serverClockOffset, bool serverClockKnown) const {
        if (stepStartMs == 0 || !serverClockKnown) {
            return 0;
        }
        return static_cast<std::int64_t>(nowLocal) + serverClockOffset - static_cast<std::int64_t>(stepStartMs);
    }

    bool SiegeLadder::TryBuild() {
        auto *entitySystem = SDK::IEntitySystem::GetInstance();
        auto *registry     = entitySystem != nullptr ? entitySystem->GetClassRegistry() : nullptr;
        if (registry == nullptr || !Shared::Game::IsFinite(position)) {
            return false;
        }
        SDK::IEntityClass *entityClass = registry->FindClass("Ladder");
        if (entityClass == nullptr) {
            return false;
        }
        const Shared::Game::SiegeLadderAsset &asset = Asset();

        char name[64];
        std::snprintf(name, sizeof(name), "KCDC_Ladder_%llu", static_cast<unsigned long long>(GetNetworkID()));

        SDK::SEntitySpawnParams params {};
        params.entityClass = entityClass;
        params.guid        = kGuidTag | (static_cast<SDK::EntityGUID>(GetNetworkID()) & kGuidMask);
        params.name        = name;
        params.position    = KCDC::Common::ToNative(position);
        params.rotation    = KCDC::Common::ToNative(LadderRotation() * glm::angleAxis(LeanRadians(), glm::vec3(1.0F, 0.0F, 0.0F)));
        params.flags       = SDK::ENTITY_FLAG_CASTSHADOW;

        SDK::IEntity *entity = entitySystem->SpawnEntity(params, false);
        if (entity == nullptr) {
            _buildRefused = true;
            Framework::Logging::GetLogger(kLogger)->error("SpawnEntity refused the Ladder for ladder {}", GetNetworkID());
            return false;
        }
        const SDK::EntityId entityId = entity->GetId();

        // The battle prefab's own properties, before the script's OnSpawn
        // reads them.
        SDK::IScriptTable *script = KCDC::Common::EntityScriptTable(entity);
        auto properties           = script != nullptr ? script->GetTable("Properties") : SDK::ScriptTablePtr {};
        if (properties) {
            KCDC::Common::AssignDeclaredProperty(*properties, "fileModel", asset.model);
            KCDC::Common::AssignDeclaredProperty(*properties, "height", asset.height);
            KCDC::Common::AssignDeclaredProperty(*properties, "bTopOnPalisade", true);
            KCDC::Common::AssignDeclaredProperty(*properties, "bDrawWeaponOntop", true);
            KCDC::Common::AssignDeclaredProperty(*properties, "bSaved_by_game", false);
        }

        if (!entitySystem->InitEntity(entity, params)) {
            // The native initializer owns failure cleanup.
            _buildRefused = true;
            Framework::Logging::GetLogger(kLogger)->error("The Ladder for ladder {} refused to initialise", GetNetworkID());
            return false;
        }
        _entityId     = entityId;
        _tiltApplied  = false;
        _climbApplied = false;
        _brokenShown  = false;
        _climbFitted  = false;
        // The siege ladder's mesh is a compound, and `Ladder.lua`'s own
        // Physicalize leaves a compound with no collider part: the mesh's
        // own Physicalize gives it one, as a prop's is given.
        KCDC::Common::Spawn::PropSpawner::EnsureCompoundPhysics(entity, 1.0F);
        Framework::Logging::GetLogger(kLogger)->info("Built {} ladder {} as entity {}", asset.name, GetNetworkID(), entityId);
        return true;
    }

    void SiegeLadder::ShowModel(bool broken) {
        SDK::IEntity *entity = ResolveNative();
        if (entity == nullptr || broken == _brokenShown) {
            return;
        }
        entity->LoadGeometry(0, broken ? Asset().brokenModel : Asset().model);
        _brokenShown = broken;
    }

    void SiegeLadder::FitClimb(SDK::IEntity &entity) {
        SDK::CPhysicalWorld *physics = SDK::CPhysicalWorld::GetInstance();
        SDK::IScriptTable *script    = KCDC::Common::EntityScriptTable(&entity);
        auto properties              = script != nullptr ? script->GetTable("Properties") : SDK::ScriptTablePtr {};
        if (physics == nullptr || !properties) {
            return;
        }
        SDK::IPhysicalEntity *self = entity.GetPhysics();
        const float lean           = LeanRadians();
        const float full           = Asset().height;

        // Measured on the standing frame, whatever the tilt is now. The walk
        // the step off lands on sits where the prefab's push stand does
        // against the rung at the top.
        const glm::vec3 vertical(0.0F, 0.0F, 1.0F);
        const glm::vec3 up      = glm::normalize(PointAlong(lean, 1.0F) - position);
        const glm::vec3 walk    = PushFrom() - Top();
        const float walkReach   = glm::length(glm::vec2(walk));
        const glm::vec3 toWall  = walkReach > 1e-3F ? glm::vec3(glm::vec2(walk) / walkReach, 0.0F) : glm::vec3(0.0F);

        // How high the body goes before something is over it: an eave, a
        // gallery, a roof the ladder was leant through.
        float top = full;
        if (const auto hit = Cast(*physics, position + up * kClimbLineStart - toWall * kClimberStandOff, up * (full + kClimberHeight - kClimbLineStart), SDK::CPhysicalWorld::kPickRayFlags, self)) {
            top = std::min(top, kClimbLineStart + hit->distance - kClimberHeight);
        }

        const glm::vec3 across = glm::cross(toWall, vertical);
        const auto levelWalk   = [&](const glm::vec3 &feet, const glm::vec3 &inner) {
            for (const glm::vec3 &row : {feet, inner}) {
                for (const float side : {-kWalkHalfWidth, 0.0F, kWalkHalfWidth}) {
                    if (row == feet && side == 0.0F) {
                        continue;
                    }
                    const glm::vec3 point = row + across * side;
                    const auto under      = Cast(*physics, point + vertical * kFloorAbove, -vertical * (kFloorAbove + kWalkStep), SDK::CPhysicalWorld::kGroundRayFlags, self);
                    if (!under || under->normal.z < kFloorNormalZ || std::abs(under->point.z - feet.z) > kWalkStep) {
                        return false;
                    }
                }
            }
            return true;
        };

        // From that rung down, the first with somewhere to step off onto: a
        // level walk as deep as the step off goes, open air over it, nothing
        // in the way to it, and the wall still under its rim.
        float fitted = 0.0F;
        for (float height = std::floor(top / kRungSpacing) * kRungSpacing; height >= kMinClimb; height -= kRungSpacing) {
            const glm::vec3 rung    = PointAlong(lean, height);
            const glm::vec3 landing = rung + walk;
            const auto floor        = Cast(*physics, landing + vertical * kFloorAbove, -vertical * (kFloorAbove + kFloorBelow), SDK::CPhysicalWorld::kGroundRayFlags, self);
            if (!floor || floor->normal.z < kFloorNormalZ) {
                continue;
            }
            const glm::vec3 feet(landing.x, landing.y, floor->point.z);
            const glm::vec3 inner = feet + toWall * kWalkDepth;
            if (!levelWalk(feet, inner) || Blocked(*physics, feet + vertical * 0.1F, vertical * kSkyClearance, self) || Blocked(*physics, inner + vertical * 0.1F, vertical * kSkyClearance, self)
                || Blocked(*physics, rung + vertical * kChestHeight, inner - rung, self)) {
                continue;
            }
            if (!Blocked(*physics, glm::vec3(rung.x, rung.y, feet.z - kRimDepth) - toWall * kClimberStandOff, toWall * (walkReach + kClimberStandOff), self)) {
                continue;
            }
            fitted = height;
            break;
        }

        // With nowhere to step off, `bTopBlocked` makes the top of the climb
        // the game's own dead end, and the climb stops below whatever is over it.
        const bool topBlocked = fitted == 0.0F;
        const float height    = topBlocked ? std::max(kMinClimb, top) : fitted;
        properties->SetValue("height", height);
        properties->SetValue("bTopBlocked", topBlocked);
        _climbFitted = true;
        Framework::Logging::GetLogger(kLogger)->info("Ladder {} climbs {:.2f} of {:.2f} m{}", GetNetworkID(), height, full, topBlocked ? " with its top blocked" : "");
    }

    void SiegeLadder::SetClimbOffered(bool offered) {
        _climbOffered = offered;
    }

    void SiegeLadder::DropLocalClimber() {
        SDK::wh::entitymodule::C_Human *actor = KCDC::Common::LocalHumanActor();
        if (actor == nullptr || _entityId == 0 || KCDC::Core::gApplication == nullptr) {
            return;
        }
        SDK::wh::entitymodule::C_ActorActionLadder *climb = KCDC::Core::gApplication->GetPlayerService().LadderActionFor(actor->GetEntityId());
        if (climb == nullptr || climb->GetLadderEntityId() != _entityId) {
            return;
        }
        SDK::wh::entitymodule::C_ActionActor *queue                  = actor->GetActionActor();
        SDK::wh::entitymodule::C_ActorStateExpansion *stateExpansion = queue != nullptr ? queue->GetActorStateExpansion() : nullptr;
        if (stateExpansion == nullptr) {
            return;
        }
        // The game's own fall off a ladder: the climb ends and the body drops
        // from where it hung, taking the fall the height gives.
        const bool dropped = stateExpansion->DropFromLadder();
        Framework::Logging::GetLogger(kLogger)->info("Ladder {} threw the local player off ({})", GetNetworkID(), dropped ? "dropped" : "refused");
    }

    void SiegeLadder::Frame(std::uint64_t nowLocal, std::int64_t serverClockOffset, bool serverClockKnown) {
        SDK::IEntity *entity = ResolveNative();
        if (entity == nullptr) {
            if (_entityId != 0) {
                // Gone with a layer or the level; built again below.
                Unbind();
            }
            if (_buildRefused || nowLocal < _nextBuildAttempt) {
                return;
            }
            _nextBuildAttempt = nowLocal + kBuildRetryMs;
            if (!TryBuild()) {
                return;
            }
            entity = ResolveNative();
            if (entity == nullptr) {
                return;
            }
        }

        const std::int64_t elapsed = Elapsed(nowLocal, serverClockOffset, serverClockKnown);
        const float tilt           = TiltAfter(elapsed);
        if (!_tiltApplied || std::abs(tilt - _appliedTilt) > kTiltEpsilon) {
            entity->SetRotation(KCDC::Common::ToNative(LadderRotation() * glm::angleAxis(tilt, glm::vec3(1.0F, 0.0F, 0.0F))));
            _appliedTilt = tilt;
            _tiltApplied = true;
        }

        ShowModel(state == Shared::Entities::SiegeLadderState::Broken);

        // Off as it starts to fall or breaks, once per step: the blade meeting
        // it is what takes the climber's hold.
        const bool falling = (state == Shared::Entities::SiegeLadderState::Pushed && elapsed >= static_cast<std::int64_t>(kPushContactMs)) || state == Shared::Entities::SiegeLadderState::Broken;
        const std::uint64_t step = state == Shared::Entities::SiegeLadderState::Broken ? 1 : stepStartMs;
        if (falling && _droppedForStep != step) {
            _droppedForStep = step;
            DropLocalClimber();
        }

        // The crash where it lands, once per fall this machine watched: a
        // ladder streamed in already lying makes no sound.
        if (state == Shared::Entities::SiegeLadderState::Pushed && serverClockKnown && stepStartMs != _crashedForStep) {
            if (elapsed < static_cast<std::int64_t>(kPushContactMs)) {
                _crashWatched = true;
            }
            if (elapsed >= static_cast<std::int64_t>(kPushLandMs)) {
                _crashedForStep = stepStartMs;
                if (_crashWatched) {
                    const glm::vec3 landing = PointAlong(glm::radians(kLyingTiltDeg), Asset().height * 0.6F);
                    KCDC::Features::Audio::AudioService::PlayAt(kCrashSound, landing);
                    KCDC::Features::Siege::SiegeModule::Get().PlayEffect(kCrashEffect, landing, kCrashEffectMs);
                }
                _crashWatched = false;
            }
        }

        if (!_climbFitted) {
            const SDK::IEntity *body = KCDC::Common::LocalPlayer();
            if (body != nullptr && glm::length(glm::vec2(KCDC::Common::ToGlm(body->GetWorldPos()) - position)) <= kFitRange) {
                FitClimb(*entity);
            }
        }

        // `Ladder.lua` reads `Runtime.isUsable` for its prompt and its
        // `IsUsable`; written only when it changes. Nothing is climbed
        // before the climb is fitted to its wall.
        const bool climbable = IsClimbable() && _climbOffered && _climbFitted;
        if (!_climbApplied || climbable != _climbWasOffered) {
            SDK::IScriptTable *script = KCDC::Common::EntityScriptTable(entity);
            auto runtime              = script != nullptr ? script->GetTable("Runtime") : SDK::ScriptTablePtr {};
            if (runtime) {
                runtime->SetValue("isUsable", climbable);
                _climbWasOffered = climbable;
                _climbApplied    = true;
            }
        }
    }
} // namespace KCDC::Features::Siege
