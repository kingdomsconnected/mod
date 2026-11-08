#include <utils/safe_win32.h>

#include "features/player/player_teleport.h"
#include "core/application.h"

#include "features/animation/animation_service.h"

#include "common/vector_math.h"

#include <logging/logger.h>

#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

#include "sdk/cryengine/action/i_game_framework.h"
#include "sdk/cryengine/animation/i_character_instance.h"
#include "sdk/cryengine/animation/i_skeleton_pose.h"
#include "sdk/cryengine/entitysystem/i_entity.h"
#include "sdk/cryengine/entitysystem/i_entity_class.h"
#include "sdk/cryengine/entitysystem/i_entity_proxy.h"
#include "sdk/cryengine/entitysystem/i_entity_system.h"
#include "sdk/cryengine/physics/c_physical_world.h"
#include "sdk/cryengine/physics/i_physical_entity.h"
#include "sdk/cryengine/scriptsystem/i_script_table.h"

#include <mafianet/get_time.h>

#include <algorithm>
#include <iterator>

namespace KCDC::Features::Player {
    namespace {
        // Both caps: frames alone stretch into seconds at the rate a level
        // load leaves, a clock alone can expire before the first probe runs.
        constexpr std::uint32_t kMaxGroundHoldFrames = 120;
        constexpr std::uint64_t kMaxGroundHoldMs     = 3000;

        constexpr float kGroundClearance = 0.05F;

        // Narrow, or a floor is mistaken for terrain three storeys below it.
        constexpr float kGroundProbeUp   = 2.0F;
        constexpr float kGroundProbeDown = 3.0F;

        float DistanceSquared(const SDK::Vec3 &left, const SDK::Vec3 &right) {
            const float x = left.x - right.x;
            const float y = left.y - right.y;
            const float z = left.z - right.z;
            return x * x + y * y + z * z;
        }

        SDK::Vec3 DoorApproachPosition(SDK::IEntity *door, const SDK::Vec3 &playerPosition) {
            constexpr float kDoorClearance = 1.5F;
            const SDK::Vec3 doorPosition   = door->GetWorldPos();
            SDK::Vec3 direction            = door->GetForwardDir();
            direction.z                    = 0.0F;

            float horizontalLength = std::sqrt(direction.x * direction.x + direction.y * direction.y);
            if (horizontalLength < 0.001F) {
                direction.x     = playerPosition.x - doorPosition.x;
                direction.y     = playerPosition.y - doorPosition.y;
                horizontalLength = std::sqrt(direction.x * direction.x + direction.y * direction.y);
            }
            if (horizontalLength < 0.001F) {
                direction.x      = 1.0F;
                direction.y      = 0.0F;
                horizontalLength = 1.0F;
            }

            direction.x /= horizontalLength;
            direction.y /= horizontalLength;
            const SDK::Vec3 front {doorPosition.x + direction.x * kDoorClearance, doorPosition.y + direction.y * kDoorClearance, doorPosition.z + 0.2F};
            const SDK::Vec3 back {doorPosition.x - direction.x * kDoorClearance, doorPosition.y - direction.y * kDoorClearance, doorPosition.z + 0.2F};
            return DistanceSquared(front, playerPosition) <= DistanceSquared(back, playerPosition) ? front : back;
        }

        bool FindNearestUnlockedDoor(SDK::IEntity *player, SDK::Vec3 &destination, std::string &label, SDK::EntityId &doorEntityId, SDK::EntityGUID &doorGuid, float &distance) {
            auto *entitySystem = SDK::IEntitySystem::GetInstance();
            if (entitySystem == nullptr) {
                return false;
            }

            SDK::IEntityItPtr iterator(entitySystem->GetEntityIterator());
            if (!iterator) {
                return false;
            }

            const SDK::Vec3 playerPosition = player->GetWorldPos();
            float nearestDistanceSquared   = std::numeric_limits<float>::max();
            SDK::IEntity *nearestDoor      = nullptr;
            iterator->MoveFirst();
            while (!iterator->IsEnd()) {
                SDK::IEntity *entity       = iterator->Next();
                SDK::IEntityClass *entityClass = entity != nullptr ? entity->GetClass() : nullptr;
                const char *className      = entityClass != nullptr ? entityClass->GetName() : nullptr;
                if (className == nullptr || std::strcmp(className, SDK::EntityClassNames::AnimDoor) != 0) {
                    continue;
                }

                SDK::IEntityProxy *proxy  = entity->GetProxy(static_cast<int>(SDK::EEntityProxy::Script));
                SDK::IScriptTable *script = proxy != nullptr ? proxy->GetScriptTable() : nullptr;
                bool locked               = true;
                if (script == nullptr || !script->GetValue("bLocked", locked) || locked) {
                    continue;
                }

                const float candidateDistanceSquared = DistanceSquared(entity->GetWorldPos(), playerPosition);
                if (candidateDistanceSquared < nearestDistanceSquared) {
                    nearestDistanceSquared = candidateDistanceSquared;
                    nearestDoor            = entity;
                }
            }

            if (nearestDoor == nullptr) {
                return false;
            }

            destination  = DoorApproachPosition(nearestDoor, playerPosition);
            doorEntityId = nearestDoor->GetId();
            doorGuid     = nearestDoor->GetGuid();
            distance     = std::sqrt(nearestDistanceSquared);
            const char *doorName = nearestDoor->GetName();
            label = doorName != nullptr && doorName[0] != '\0' ? std::string("door ") + doorName : "nearest unlocked door";
            return KCDC::Common::IsFinite(destination);
        }

        /**
         * The entity whose transform actually carries the player.
         *
         * A mounted player is attached to the horse, and moving the child of
         * an attachment only lasts until the parent's next transform update.
         * The outermost parent takes everything attached to it along, and is
         * the player entity itself when nothing is.
         */
        SDK::IEntity *TransformRoot(SDK::IEntity *entity) {
            for (SDK::IEntity *parent = entity->GetParent(); parent != nullptr; parent = entity->GetParent()) {
                entity = parent;
            }
            return entity;
        }

        SDK::IPhysicalEntity *CharacterPhysics(SDK::ICharacterInstance *character) {
            SDK::ISkeletonPose *pose = character != nullptr ? character->GetISkeletonPose() : nullptr;
            return pose != nullptr ? pose->GetCharacterPhysics() : nullptr;
        }

        // An actor has two colliders -- the entity's and the animated
        // skeleton's -- and a mounted one a third on its carrier. Miss any
        // and the player is reported as its own ground.
        bool ProbeLocalGround(SDK::IEntity *player, const SDK::Vec3 &position, SDK::Vec3 &ground) {
            auto *physics = SDK::CPhysicalWorld::GetInstance();
            if (physics == nullptr) {
                return false;
            }

            SDK::IEntity *root = TransformRoot(player);
            SDK::IPhysicalEntity *candidates[] {player->GetPhysics(), CharacterPhysics(player->GetCharacter(0)), root->GetPhysics(), CharacterPhysics(root->GetCharacter(0))};

            SDK::IPhysicalEntity *skip[std::size(candidates)] {};
            int skipCount = 0;
            for (SDK::IPhysicalEntity *collider : candidates) {
                if (collider != nullptr && std::find(skip, skip + skipCount, collider) == skip + skipCount) {
                    skip[skipCount++] = collider;
                }
            }

            return physics->ResolveGround(position, kGroundProbeUp, kGroundProbeDown, ground, skip, skipCount);
        }
    } // namespace

    const char *TeleportResultToString(TeleportResult result) {
        switch (result) {
        case TeleportResult::None: return "no request yet";
        case TeleportResult::Succeeded: return "teleported";
        case TeleportResult::NoLocalPlayer: return "no local player (not in a level)";
        case TeleportResult::NoUnlockedDoor: return "no loaded unlocked door was found";
        case TeleportResult::InvalidPosition: return "the destination is not a finite position";
        case TeleportResult::GroundUnavailable: return "ground did not become available";
        case TeleportResult::ContextChanged: return "offline editor player changed";
        }
        return "unknown";
    }

    std::uint64_t PlayerTeleporter::QueueTeleport(const TeleportRequest &request) {
        std::lock_guard lock(_mutex);
        const std::uint64_t placementId = _nextPlacementId++;
        _pending.push_back(QueuedRequest {request, placementId});
        return placementId;
    }

    void PlayerTeleporter::Reset() {
        std::lock_guard lock(_mutex);
        _pending.clear();
        _holding = false;
        _hold    = {};
    }

    void PlayerTeleporter::SettlePlacement(std::uint64_t placementId, bool success) {
        if (auto *app = KCDC::Core::gApplication.get()) {
            app->OnSessionPlacementSettled(placementId, success);
        }
    }

    void PlayerTeleporter::Update() {
        std::vector<QueuedRequest> pending;
        {
            std::lock_guard lock(_mutex);
            pending = std::move(_pending);
            _pending.clear();
        }

        for (const QueuedRequest &queued : pending) {
            // The body is only in one place, so a later ask replaces the hold.
            // The one it replaces is done: the body is in the level, now on
            // the server's newer word.
            if (_holding) {
                SettlePlacement(_hold.placementId, true);
            }
            _holding = false;
            if (!Execute(queued.request, queued.placementId)) {
                SettlePlacement(queued.placementId, GetLastResult() == TeleportResult::Succeeded);
            }
        }

        if (_holding && ServiceGroundHold()) {
            _holding = false;
            // Ground that never streamed in leaves the body where it was
            // asserted, as before; only losing the player is a failure.
            SettlePlacement(_hold.placementId, GetLastResult() != TeleportResult::NoLocalPlayer);
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Spawn hold released after {} frames at ({:.1f}, {:.1f}, {:.1f})", _hold.frames, _hold.position.x, _hold.position.y, _hold.position.z);
        }
    }

    bool PlayerTeleporter::ApplyPlacement(const SDK::Vec3 &position, const SDK::Quat &rotation, bool hasRotation) {
        auto *framework = SDK::IGameFramework::GetInstance();
        auto *player    = framework != nullptr ? framework->GetClientEntity() : nullptr;
        if (player == nullptr) {
            return false;
        }

        SDK::IEntity *root = TransformRoot(player);

        // force, so the move is not dropped against a transform the engine
        // believes it already owns, and recalcPhysicsBounds so the living
        // entity's physics follows rather than snapping the actor back.
        root->SetPos(position, SDK::ENTITY_XFORM_POS, true, true);
        // Not SetPosRotScale, which carries neither of those two flags.
        if (hasRotation) {
            root->SetRotation(rotation, SDK::ENTITY_XFORM_ROT);
        }
        return true;
    }

    bool PlayerTeleporter::Execute(const TeleportRequest &request, std::uint64_t placementId) {
        {
            std::lock_guard lock(_mutex);
            _lastPlacementId = placementId;
        }
        const auto fail = [&](TeleportResult result, const std::string &label) {
            std::lock_guard lock(_mutex);
            _lastResult = result;
            _lastLabel  = label;
        };

        auto *framework = SDK::IGameFramework::GetInstance();
        auto *player    = framework != nullptr ? framework->GetClientEntity() : nullptr;
        if (player == nullptr) {
            fail(TeleportResult::NoLocalPlayer, request.label);
            return false;
        }

        SDK::Vec3 destination      = request.position;
        if (request.offlineEditorPlayer != 0 &&
            (player->GetId() != request.offlineEditorPlayer || player->GetParent() || !Core::gApplication || !Core::gApplication->IsOfflineBuilderReady())) {
            fail(TeleportResult::ContextChanged, request.label);
            return false;
        }
        std::string label          = request.label;
        SDK::EntityId doorEntityId = 0;
        SDK::EntityGUID doorGuid   = 0;
        float doorDistance         = 0.0F;
        if (request.target == TeleportTarget::NearestUnlockedDoor && !FindNearestUnlockedDoor(player, destination, label, doorEntityId, doorGuid, doorDistance)) {
            fail(TeleportResult::NoUnlockedDoor, request.label);
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Door debug teleport found no loaded unlocked AnimDoor entity");
            return false;
        }
        if (!KCDC::Common::IsFinite(destination)) {
            fail(TeleportResult::InvalidPosition, label);
            return false;
        }

        // A destination the probe cannot reach keeps the height it was given.
        SDK::Vec3 ground {};
        if (request.waitForGround && ProbeLocalGround(player, destination, ground)) {
            destination.z = ground.z + kGroundClearance;
        }

        // Whatever the body plays is cut first. `CAnimatedCharacter` adopts the
        // new pose, but a scripted full-body clip still running -- and the
        // Animation MCM slot it holds -- kept the body where it was until the
        // clip ended; a loop starts again where the body lands.
        KCDC::Features::Animation::AnimationService::Get().InterruptLocal();

        if (!ApplyPlacement(destination, request.rotation, request.hasRotation)) {
            fail(TeleportResult::NoLocalPlayer, label);
            return false;
        }

        {
            std::lock_guard lock(_mutex);
            _lastResult   = TeleportResult::Succeeded;
            _lastLabel    = label;
            _lastPosition = destination;
        }

        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Teleported the player to {} ({:.1f}, {:.1f}, {:.1f})", label, destination.x, destination.y, destination.z);
        if (request.target == TeleportTarget::NearestUnlockedDoor) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Door debug teleport selected entity {} GUID {:016X} at {:.1f} m", doorEntityId, doorGuid, doorDistance);
        }

        if (!request.waitForGround) {
            return false;
        }

        _hold             = {};
        _hold.position    = destination;
        _hold.rotation    = request.rotation;
        _hold.hasRotation = request.hasRotation;
        _hold.placementId = placementId;
        _hold.deadlineMs  = MafiaNet::GetTime() + kMaxGroundHoldMs;
        _holding          = true;
        return true;
    }

    bool PlayerTeleporter::ServiceGroundHold() {
        auto *framework = SDK::IGameFramework::GetInstance();
        auto *player    = framework != nullptr ? framework->GetClientEntity() : nullptr;
        if (player == nullptr) {
            std::lock_guard lock(_mutex);
            _lastResult = TeleportResult::NoLocalPlayer;
            return true;
        }

        // Re-assert before testing: a frame in which the body has already
        // begun to fall must not be the one that measures its height.
        ApplyPlacement(_hold.position, _hold.rotation, _hold.hasRotation);
        ++_hold.frames;

        SDK::Vec3 ground {};
        if (ProbeLocalGround(player, _hold.position, ground)) {
            return true;
        }
        if (_hold.frames >= kMaxGroundHoldFrames || MafiaNet::GetTime() >= _hold.deadlineMs) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("Spawn hold gave up waiting for ground at ({:.1f}, {:.1f}, {:.1f}) after {} frames", _hold.position.x, _hold.position.y, _hold.position.z, _hold.frames);
            std::lock_guard lock(_mutex);
            _lastResult = TeleportResult::GroundUnavailable;
            return true;
        }
        return false;
    }

    TeleportResult PlayerTeleporter::GetLastResult() const {
        std::lock_guard lock(_mutex);
        return _lastResult;
    }

    std::string PlayerTeleporter::GetLastLabel() const {
        std::lock_guard lock(_mutex);
        return _lastLabel;
    }

    SDK::Vec3 PlayerTeleporter::GetLastPosition() const {
        std::lock_guard lock(_mutex);
        return _lastPosition;
    }

    std::uint64_t PlayerTeleporter::GetLastPlacementId() const {
        std::lock_guard lock(_mutex);
        return _lastPlacementId;
    }
} // namespace KCDC::Features::Player
