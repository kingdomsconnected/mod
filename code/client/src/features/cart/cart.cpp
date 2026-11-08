#include "cart.h"
#include "cart_service.h"

#include "common/actor_dressing.h"
#include "common/module_support.h"
#include "common/vector_math.h"

#include "shared/features/horse/horse_entity.h"

#include "sdk/cryengine/action/i_actor_system.h"
#include "sdk/cryengine/action/i_animated_character.h"
#include "sdk/cryengine/animation/cry_char_animation_params.h"
#include "sdk/cryengine/animation/i_animation_set.h"
#include "sdk/cryengine/animation/i_character_instance.h"
#include "sdk/cryengine/animation/i_skeleton_anim.h"
#include "sdk/cryengine/entitysystem/i_entity_class.h"
#include "sdk/cryengine/entitysystem/i_entity_proxy.h"
#include "sdk/cryengine/entitysystem/i_entity_system.h"
#include "sdk/cryengine/physics/c_physical_world.h"
#include "sdk/cryengine/physics/i_physical_entity.h"
#include "sdk/cryengine/scriptsystem/i_script_system.h"
#include "sdk/cryengine/scriptsystem/i_script_table.h"
#include "sdk/wh/entitymodule/c_actor.h"
#include "sdk/wh/entitymodule/c_cart_follower_model.h"
#include "sdk/wh/entitymodule/e_cart_position.h"

#include "core/application.h"

#include <core_modules.h>
#include <logging/logger.h>

#include <mafianet/get_time.h>

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace KCDC::Features::Cart {
    namespace {
        constexpr const char *kLogger = "Cart";

        /** How long a build that failed for a reason time can fix is left before the next try. */
        constexpr MafiaNet::Time kBuildRetryMs = 1000;

        /**
         * Tags every GUID this mod gives a cart's entities, so none is mistaken
         * for a level's: a part's slot under its cart's network id. A part
         * spawned with no GUID is keyed under GUID 0 by the smart objects
         * manager, which then hands a passenger's `cart` stance the entry of
         * whatever other GUID-less body holds that key, and takes it away from
         * every passenger when that body goes -- an NPC despawning nearby --
         * leaving no fragment for sitting or climbing down.
         */
        constexpr SDK::EntityGUID kGuidTag  = 0xF5A8000000000000ULL;
        constexpr SDK::EntityGUID kGuidMask = 0x0000FFFFFFFFFFFFULL;

        /** The seats' slots, past every prefab's parts. */
        constexpr std::size_t kSeatGuidSlot = 0x80;

        /**
         * Ground is looked for this far above and below the ground last found
         * under a point; and this far either way the first time, when all
         * there is to go by is the height the cart was spawned at.
         */
        constexpr float kGroundProbeUp   = 1.5F;
        constexpr float kGroundProbeDown = 4.0F;
        constexpr float kGroundSeek      = 100.0F;

        /**
         * How far above the ground under a wheel a level surface may stand and
         * still be driven onto: a kerb, a bridge's deck. Anything higher is a
         * wall, and anything static lower than the terrain is under the map.
         */
        constexpr float kGroundStep = 1.25F;

        /** How upright a surface's normal is for it to be level enough to drive on. */
        constexpr float kLevelNormalZ = 0.7F;

        /** How far down a face may look and still be a wall rather than something overhead. */
        constexpr float kOverheadNormalZ = 0.3F;

        /**
         * A static surface is ground only where it is broad -- a deck, a yard
         * -- and not a fence's rail or a post's top: found again within this
         * seam of its height on at least three of four sides this far out.
         */
        constexpr float kSurfaceSpan = 0.35F;
        constexpr float kSurfaceSeam = 0.15F;

        /**
         * How the ground is eased: towards a new height over this many
         * seconds -- cobbles and seams read differently frame by frame and
         * would shake the bed -- and, under a horse, taken at once past a jump
         * of this many metres.
         */
        constexpr float kGroundEase = 0.08F;
        constexpr float kGroundJump = 0.75F;

        /**
         * An axle's wheels on ground that changes by more than this step at
         * once: down onto lower ground they fall, from rest, at this
         * acceleration; up onto a deck they are eased over this many seconds.
         * Less than a step, on a slope or a cobble, they follow it eased.
         */
        constexpr float kWheelGravity  = 9.81F;
        constexpr float kWheelStep     = 0.15F;
        constexpr float kWheelStepEase = 0.3F;

        /** Eased this close to the ground under it, a part has settled onto it. */
        constexpr float kGroundSettled = 0.001F;

        /**
         * The obstacle probe: at knee height, so a log pile or a wall stops the
         * team and a kerb does not; starting this far ahead of the lead point,
         * past the horses' hind legs, and reaching on past their heads by a
         * little more the faster the team goes; or reaching this far behind
         * the bed backing up.
         */
        constexpr float kObstacleHeight        = 0.8F;

        constexpr float kObstacleAhead         = 1.9F;
        constexpr float kObstacleReach         = 2.4F;
        constexpr float kObstacleReachPerSpeed = 0.3F;

        /** How high a horse's body is, for the obstacle probe down each horse's line. */
        constexpr float kHorseBodyHeight       = 1.4F;
        constexpr float kObstacleReachBack     = 0.9F;

        /**
         * The team's gait clock (`Cart::StepTeamGait`): the layer the horses'
         * clips play on; how long a change between two moving gaits has to
         * hold before the clip changes, so a pace wobbling on the stream does
         * not restart it; how often the clip is started again, which puts it
         * back where the horse's own graph took the layer (the engine refuses
         * the start while the clip already runs, so it costs nothing
         * otherwise); the blend into a new clip and into a restarted one; how
         * far apart the horses idle, so two heads do not bob as one; and the
         * margin a gait is kept by at its edges, tighter for the driver, whose
         * pace is steady, than for a copy drawn from the stream.
         */
        constexpr std::int32_t kHorseLayer         = 0;
        constexpr MafiaNet::Time kHorseGaitHoldMs  = 350;
        constexpr MafiaNet::Time kHorseGaitCheckMs = 100;
        constexpr float kHorseGaitBlend            = 0.2F;
        constexpr float kHorseGaitRestartBlend     = 0.15F;
        constexpr float kHorseIdleStagger          = 0.37F;
        constexpr float kHorseGaitDriverMargin     = 0.2F;
        constexpr float kHorseGaitCopyMargin       = 0.5F;

        /** The harness goes on again this long after it first did: the first equip does not always show. */
        constexpr MafiaNet::Time kHorseHarnessAgainMs = 1000;

        SDK::IScriptTable *ScriptTableOf(SDK::IEntity *entity) {
            auto *proxy = static_cast<SDK::IEntityProxy *>(entity->GetProxy(static_cast<int>(SDK::EEntityProxy::Script)));
            return proxy != nullptr ? proxy->GetScriptTable() : nullptr;
        }

        /**
         * Keeps a part out of the rigid-body simulation while leaving it solid.
         *
         * KCD2's `GeomEntity.lua` declares `Physics.bRigidBody = true`, which
         * `EntityCommon.PhysicalizeRigid` builds a dynamic body from: a wagon
         * made of those settles, tips and is shoved, while the cart movement
         * states the chassis every frame. `PE_STATIC` is what the level's own
         * carts are made of.
         */
        void MakePhysicsStatic(SDK::IEntity *entity) {
            static constexpr Shared::Game::PrefabProperty kStatic[] = {{"Physics.bRigidBody", "0"}};
            KCDC::Common::LoadPrefabProperties(entity, kStatic);
        }

        /** Calls one of an entity's own `self`-only script methods. */
        void CallEntityScript(SDK::IEntity *entity, const char *method) {
            SDK::IScriptTable *scriptTable = ScriptTableOf(entity);
            SDK::IScriptSystem *scripts    = scriptTable != nullptr ? scriptTable->GetScriptSystem() : nullptr;
            if (scripts == nullptr || !scripts->BeginCallTableMethod(scriptTable, method)) {
                return;
            }
            scripts->PushFuncParamAny(SDK::ScriptAnyValue(scriptTable));
            scripts->EndCall();
        }

        glm::vec3 PartPosition(const Shared::Game::CartPart &part) {
            return glm::vec3(part.posX, part.posY, part.posZ);
        }

        glm::quat PartRotation(const Shared::Game::CartPart &part) {
            return glm::normalize(glm::quat(part.rotW, part.rotX, part.rotY, part.rotZ));
        }

        /** Whether static ground found at `top` is broad enough to stand on; see `kSurfaceSpan`. */
        bool IsBroadSurface(SDK::CPhysicalWorld *physicalWorld, const glm::vec3 &top, const std::vector<SDK::IPhysicalEntity *> &skip) {
            constexpr float kSides[4][2] = {{kSurfaceSpan, 0.0F}, {-kSurfaceSpan, 0.0F}, {0.0F, kSurfaceSpan}, {0.0F, -kSurfaceSpan}};
            int found                    = 0;
            for (const auto &side : kSides) {
                const SDK::Vec3 origin {top.x + side[0], top.y + side[1], top.z + kSurfaceSeam};
                const SDK::Vec3 down {0.0F, 0.0F, -2.0F * kSurfaceSeam};
                SDK::SRayHit hit {};
                if (physicalWorld->RayWorldIntersection(origin, down, SDK::ent_static, SDK::CPhysicalWorld::kGroundRayFlags, &hit, 1, const_cast<SDK::IPhysicalEntity **>(skip.data()), static_cast<int>(skip.size())) > 0) {
                    ++found;
                }
            }
            return found >= 3;
        }

        /**
         * Whether what the obstacle probe hit has a broad, level top within a
         * step of the ground under the team -- a bridge's deck, not a fence --
         * for the wheels to take rather than the team to stop at. Looked for
         * from a step's height down to the probe, just past the face it hit:
         * from inside a wall, nothing is found.
         */
        bool IsStepUp(SDK::CPhysicalWorld *physicalWorld, const SDK::SRayHit &face, float ground, const std::vector<SDK::IPhysicalEntity *> &skip) {
            const glm::vec2 past = glm::vec2(face.point.x, face.point.y) - glm::vec2(face.normal.x, face.normal.y) * 0.1F;
            const SDK::Vec3 origin {past.x, past.y, ground + kGroundStep};
            const SDK::Vec3 down {0.0F, 0.0F, -(kGroundStep - kObstacleHeight + 0.05F)};
            SDK::SRayHit top {};
            return physicalWorld->RayWorldIntersection(origin, down, SDK::ent_static | SDK::ent_rigid | SDK::ent_sleeping_rigid, SDK::CPhysicalWorld::kGroundRayFlags, &top, 1, const_cast<SDK::IPhysicalEntity **>(skip.data()), static_cast<int>(skip.size())) > 0
                   && top.normal.z > kLevelNormalZ && IsBroadSurface(physicalWorld, glm::vec3(top.point.x, top.point.y, top.point.z), skip);
        }

        /** The game's own clip for each of a horse's gaits. */
        const char *HorseGaitClip(Shared::Game::CartHorseGait gait) {
            switch (gait) {
            case Shared::Game::CartHorseGait::Walk: return "relaxed_walk";
            case Shared::Game::CartHorseGait::Trot: return "relaxed_trot";
            case Shared::Game::CartHorseGait::Canter: return "relaxed_canter";
            case Shared::Game::CartHorseGait::Gallop: return "relaxed_gallop";
            case Shared::Game::CartHorseGait::WalkBack: return "relaxed_walk_bwd";
            case Shared::Game::CartHorseGait::Idle: break;
            }
            return "relaxed_idle";
        }

    } // namespace

    // --- Replica3 hooks ---------------------------------------------------

    void Cart::OnConstructed() {
        _drive = GetDrive();
        Framework::Logging::GetLogger(kLogger)->info("Cart {} constructed: blueprint '{}'", GetNetworkID(), blueprint);
    }

    /**
     * The server put the cart somewhere, or reset it: the driver, whose client
     * steps the drive, takes it from there; every other copy hears it on the
     * stream like any other pose.
     */
    void Cart::OnStateForced() {
        _drive = GetDrive();
        _snapshots.Clear();
    }

    void Cart::OnDeserialized(bool transformUpdated) {
        Shared::Entities::CartEntity::OnDeserialized(transformUpdated);
        if (!transformUpdated || IsOwner()) {
            return;
        }
        const Shared::Game::CartDrive drive = GetDrive();
        if (Shared::Game::IsSaneCartDrive(drive)) {
            _snapshots.Push(CartSnapshot {drive}, GetLastTransformTime());
        }
    }

    void Cart::DeallocReplica(MafiaNet::Connection_RM3 *) {
        Despawn();
        delete this;
    }

    // --- The native cart --------------------------------------------------

    void Cart::RefreshNativeBinding() {
        // Entity ids are salted, so the `WHCart`'s stops resolving once the
        // entity is gone: only then is the extension looked up again.
        if (_cartEntityId == 0 || KCDC::Common::ResolveEntity(_cartEntityId) == nullptr) {
            _cart = nullptr;
        }
        else if (_cart == nullptr) {
            _cart = SDK::wh::entitymodule::C_Cart::FindByEntityId(_cartEntityId);
        }
    }

    // --- Per frame --------------------------------------------------------

    void Cart::Frame(const Shared::Game::CartControls &controls, bool localDriverSeated, float frameTime) {
        RefreshNativeBinding();
        if (_cart == nullptr) {
            if (!_parts.empty()) {
                // The tree went with a level change; the next frame builds it again.
                Despawn();
            }
            TryBuild();
            return;
        }

        RequestHorses();
        DressHorses();

        _driving = IsOwner() && localDriverSeated;
        if (_driving) {
            Drive(controls, frameTime);
        }
        else {
            Follow();
        }
        Present(_drive, frameTime);
        StepTeamGait(_drive.speed, frameTime);
    }

    // --- Building ---------------------------------------------------------

    void Cart::TryBuild() {
        if (_buildRefused) {
            return;
        }
        const MafiaNet::Time now = MafiaNet::GetTime();
        if (now < _nextBuildAttempt) {
            return;
        }
        _nextBuildAttempt = now + kBuildRetryMs;

        const Shared::Game::CartBlueprint *chosen = Shared::Game::FindCartBlueprint(blueprint);
        _rig                                      = chosen != nullptr ? Shared::Game::BuildCartRig(*chosen) : std::nullopt;
        if (chosen == nullptr || !_rig) {
            _buildRefused = true;
            Framework::Logging::GetLogger(kLogger)->error("Cannot build cart {}: no drivable blueprint named '{}'", GetNetworkID(), blueprint);
            return;
        }
        if (SDK::IEntitySystem::GetInstance() == nullptr || !KCDC::Common::IsFinite(position)) {
            return;
        }

        if (!BuildTree(*chosen)) {
            Despawn();
            return;
        }

        RefreshNativeBinding();
        if (_cart == nullptr) {
            Despawn();
            _buildRefused = true;
            Framework::Logging::GetLogger(kLogger)->error("Cart {}: its WHCart registered no cart extension", GetNetworkID());
            return;
        }

        // The engine's own pass, now that the tree it reads is up. At
        // InitEntity the WHCart had no parent, and the pass returns before its
        // latch when it finds none, so it is asked for here.
        _cart->RunSetupPass();
        if (_cart->GetFollowerModel() == nullptr || _cart->GetMoveContext() == nullptr) {
            Despawn();
            _buildRefused = true;
            Framework::Logging::GetLogger(kLogger)->error("Cart {}: the setup pass found no GeomEntity chassis to build the follower model from", GetNetworkID());
            return;
        }

        OpenSeats();
        if (!SpawnSeatEntities()) {
            Despawn();
            _buildRefused = true;
            Framework::Logging::GetLogger(kLogger)->error("Cart {}: its seats could not be built", GetNetworkID());
            return;
        }

        _harnessSlots  = static_cast<int>(std::min<std::size_t>(_rig->horseCount, static_cast<std::size_t>(Shared::Game::CartHarnessSlots(*chosen))));
        _buildDeferred = false;
        Framework::Logging::GetLogger(kLogger)->info("Built cart {} ('{}') as {} entities, harnessed for {} horse(s)", GetNetworkID(), blueprint, _parts.size(), _harnessSlots);
    }

    /**
     * The blueprint's tree, the way the level builds it: the chassis at the
     * root, spawned at this cart's pose, and every other part hung off its
     * parent at its prefab-local transform. A part the prefab leaves at the
     * root other than the chassis is hung off the chassis instead, at where
     * the prefab has it relative to the chassis -- the setup pass follows the
     * chassis, and a loose root would be left standing where the cart set off.
     *
     * Attached without `ATTACHMENT_KEEP_TRANSFORMATION`, which is what makes
     * the engine read a spawned part's transform as local to its parent.
     */
    bool Cart::BuildTree(const Shared::Game::CartBlueprint &chosen) {
        auto *entitySystem         = SDK::IEntitySystem::GetInstance();
        const int chassisIndex     = Shared::Game::FindCartChassis(chosen);
        const auto &chassisPart    = chosen.parts[chassisIndex];
        const glm::quat chassisRot = PartRotation(chassisPart);
        const glm::vec3 chassisPos = PartPosition(chassisPart);

        std::vector<SDK::EntityId> spawned(chosen.partCount, 0);
        _chassisEntityId = SpawnPart(chassisPart, static_cast<std::size_t>(chassisIndex), nullptr, KCDC::Common::ToNative(position), KCDC::Common::ToNative(glm::angleAxis(_drive.leadYaw, glm::vec3(0.0F, 0.0F, 1.0F))));
        if (_chassisEntityId == 0) {
            return false;
        }
        spawned[chassisIndex] = _chassisEntityId;

        for (std::size_t index = 0; index < chosen.partCount; ++index) {
            if (static_cast<int>(index) == chassisIndex) {
                continue;
            }

            const Shared::Game::CartPart &part = chosen.parts[index];
            SDK::EntityId parentId             = _chassisEntityId;
            glm::vec3 localPos                 = PartPosition(part);
            glm::quat localRot                 = PartRotation(part);
            if (part.parent >= 0) {
                parentId = spawned[static_cast<std::size_t>(part.parent)];
            }
            else {
                const glm::quat inverse = glm::inverse(chassisRot);
                localPos                = inverse * (localPos - chassisPos);
                localRot                = inverse * localRot;
            }

            SDK::IEntity *parent       = parentId != 0 ? entitySystem->GetEntity(parentId) : nullptr;
            const SDK::EntityId partId = parent != nullptr ? SpawnPart(part, index, parent, KCDC::Common::ToNative(localPos), KCDC::Common::ToNative(localRot)) : 0;
            if (partId == 0) {
                if (!_buildDeferred) {
                    Framework::Logging::GetLogger(kLogger)->warn("Cart {} lost part '{}' of blueprint '{}'", GetNetworkID(), part.name, chosen.name);
                }
                return false;
            }
            spawned[index] = partId;

            if (part.entityClass == SDK::EntityClassNames::WHCart) {
                _cartEntityId = partId;
            }
            if (_rig && static_cast<int>(index) == _rig->bodyIndex) {
                _bodyEntityId = partId;
            }
        }

        return _cartEntityId != 0;
    }

    SDK::EntityId Cart::SpawnPart(const Shared::Game::CartPart &part, std::size_t slot, SDK::IEntity *parent, const SDK::Vec3 &partPosition, const SDK::Quat &partRotation) {
        auto *entitySystem = SDK::IEntitySystem::GetInstance();
        auto *registry     = entitySystem->GetClassRegistry();

        const std::string entityClassName(part.entityClass);
        SDK::IEntityClass *entityClass = registry != nullptr ? registry->FindClass(entityClassName.c_str()) : nullptr;
        if (entityClass == nullptr) {
            // The classes come up with the game framework: a missing one before
            // a level is loaded is a wait, reported once, not a failure.
            if (!_buildDeferred) {
                _buildDeferred = true;
                Framework::Logging::GetLogger(kLogger)->info("Holding the build of cart {}: entity class '{}' is not registered yet", GetNetworkID(), entityClassName);
            }
            return 0;
        }

        // The game's own naming contract, not decoration: the follower model
        // is built by matching the chassis's children against "wheel_fl",
        // "wheel_fr" and "backPart" after everything from the first '[' is
        // cut off, which is what the level's own `wheel_fl[<prefab>]` is for.
        char partName[128];
        std::snprintf(partName, sizeof(partName), "%.*s[%sCart_%llu]", static_cast<int>(part.name.size()), part.name.data(), KCDC::Common::Spawn::kSpawnedActorNamePrefix, static_cast<unsigned long long>(GetNetworkID()));

        SDK::SEntitySpawnParams params {};
        params.entityClass = entityClass;
        params.guid        = kGuidTag | (((static_cast<SDK::EntityGUID>(GetNetworkID()) << 8) | slot) & kGuidMask);
        params.name        = partName;
        params.position    = partPosition;
        params.rotation    = partRotation;
        params.scale       = SDK::Vec3 {part.scaleX, part.scaleY, part.scaleZ};
        if (!part.geometry.empty()) {
            // The level loader sets it on its own carts from the spec; a
            // runtime spawn has it only when asked, and a wagon casting no
            // shadow floats.
            params.flags = SDK::ENTITY_FLAG_CASTSHADOW;
        }

        SDK::IEntity *entity = entitySystem->SpawnEntity(params, false);
        if (entity == nullptr) {
            return 0;
        }
        const SDK::EntityId partId = entity->GetId();

        KCDC::Common::LoadPrefabProperties(entity, {part.properties, part.propertyCount});
        const bool isMountPoint = entityClassName == SDK::EntityClassNames::WHCartMountPoint;
        if (!isMountPoint && !part.geometry.empty()) {
            MakePhysicsStatic(entity);
        }

        if (!entitySystem->InitEntity(entity, params)) {
            // The native initializer owns failure cleanup; the entity is not reused.
            return 0;
        }
        _parts.push_back(partId);

        entity = entitySystem->GetEntity(partId);
        if (entity == nullptr) {
            return 0;
        }

        // KCD2's GeomEntity has no model property -- a level entity gets its
        // mesh from the loader's `Geometry` attribute -- so the mesh goes in
        // by hand, and the entity's own physicalization is run again now that
        // there is something to physicalize.
        if (!part.geometry.empty()) {
            const std::string geometry(part.geometry);
            entity->LoadGeometry(0, geometry.c_str());
            CallEntityScript(entity, "PhysicalizeThis");
        }

        // A mount point sets its interactive collision class from
        // OnEnablePhysics and the editor's reset, neither of which a spawn goes
        // through; without it the interactor never offers the prompt.
        if (isMountPoint) {
            CallEntityScript(entity, "SetInteractiveCollisionType");
        }

        if (parent != nullptr) {
            parent->AttachChild(entity);
        }
        return partId;
    }

    /**
     * The prefab's own `WHCart` seats nobody -- every seat has a `WHCart` of
     * its own -- but its mount points raise the game's prompt, and the
     * prompt's check (`CanPlayerMount`, 0x1828F1B50) asks it for a free player
     * seat and for the player-seat enable the game gives a cart from a script
     * node, which a runtime cart has never had.
     */
    void Cart::OpenSeats() {
        _cart->SetPositionEnabled(SDK::wh::entitymodule::E_CartPosition::Codriver, true);
        _cart->SetPositionEnabled(SDK::wh::entitymodule::E_CartPosition::RightBack, true);
        _cart->AddPlayerSeatEnable(true);
    }

    /**
     * One `WHCart` per seat, under the body, and the seat's own position on it.
     *
     * Where the game seats a body is the `WHCart`'s own smart-object helper
     * for its position, taken through the cart entity's world transform
     * (`GetSeatHelperTransform`, 0x1828F4484) -- not the cart's movement or
     * its wheels. Each position has its own approach and climbs too, so a
     * body is climbed into its seat by the game's own action, the way the
     * game's passengers are: the local player through the mount, everybody
     * else's copy through the same cart action. None of them ever moves on
     * its own, which is all the action asks of a cart
     * (`C_ActorActionCart::Enter`, 0x1828D7614, refuses only a moving one)
     * besides the seat being free.
     *
     * A two-wheeler's frame is tipped up to its harness, and its seats are
     * laid out on the frame before that: they are hung off it tipped back.
     */
    bool Cart::SpawnSeatEntities() {
        static constexpr std::string_view kSeatNames[]                        = {"seat0", "seat1", "seat2", "seat3", "seat4", "seat5"};
        static constexpr Shared::Game::PrefabProperty kSeatEntityProperties[] = {{"guidSmartObjectType", "cbbbbbdb-df9e-41b5-8fed-783b4372e15c"}};
        static_assert(sizeof(kSeatNames) / sizeof(kSeatNames[0]) == Shared::Game::kCartMaxSeats);

        SDK::IEntity *parent = KCDC::Common::ResolveEntity(_rig->articulated ? _bodyEntityId : _chassisEntityId);
        if (parent == nullptr) {
            return false;
        }

        // The frame the seats are laid out in, as seen from the part they hang off.
        const glm::quat untip = glm::angleAxis(-_rig->lift, glm::vec3(1.0F, 0.0F, 0.0F));
        const glm::vec3 shift = _rig->articulated ? glm::vec3(0.0F) : _rig->axle - untip * _rig->axle;

        const Shared::Game::CartSeatLayout layout = Shared::Game::CartSeatsFor(*_rig);
        for (std::size_t index = 0; index < layout.count; ++index) {
            const Shared::Game::CartSeat &seat = layout.seats[index];

            Shared::Game::CartPart part;
            part.name          = kSeatNames[index];
            part.entityClass   = SDK::EntityClassNames::WHCart;
            part.properties    = kSeatEntityProperties;
            part.propertyCount = sizeof(kSeatEntityProperties) / sizeof(kSeatEntityProperties[0]);

            const SDK::EntityId seatEntityId      = SpawnPart(part, kSeatGuidSlot + index, parent, KCDC::Common::ToNative(shift + untip * seat.frame), KCDC::Common::ToNative(untip));
            SDK::wh::entitymodule::C_Cart *native = seatEntityId != 0 ? SDK::wh::entitymodule::C_Cart::FindByEntityId(seatEntityId) : nullptr;
            if (native == nullptr) {
                return false;
            }
            native->SetPositionEnabled(SeatPosition(seat), true);
            native->AddPlayerSeatEnable(true);
            _seatEntityIds[index] = seatEntityId;
            _seatCarts[index]     = native;
        }
        return true;
    }

    void Cart::CollectNativeCarts(std::vector<const SDK::wh::entitymodule::C_Cart *> &out) const {
        if (_cart == nullptr) {
            return;
        }
        out.push_back(_cart);
        for (const SDK::wh::entitymodule::C_Cart *seat : _seatCarts) {
            if (seat != nullptr) {
                out.push_back(seat);
            }
        }
    }

    SDK::wh::entitymodule::E_CartPosition::Type Cart::SeatPosition(const Shared::Game::CartSeat &seat) {
        if (seat.front) {
            return SDK::wh::entitymodule::E_CartPosition::Codriver;
        }
        if (seat.name == "leftBack") {
            return SDK::wh::entitymodule::E_CartPosition::LeftBack;
        }
        if (seat.name == "rightFront") {
            return SDK::wh::entitymodule::E_CartPosition::RightFront;
        }
        if (seat.name == "leftFront") {
            return SDK::wh::entitymodule::E_CartPosition::LeftFront;
        }
        return SDK::wh::entitymodule::E_CartPosition::RightBack;
    }

    std::optional<glm::vec3> Cart::SeatWorldPoint(std::uint8_t seat) const {
        SDK::IEntity *entity = KCDC::Common::ResolveEntity(GetSeatEntityId(seat));
        if (entity == nullptr) {
            return std::nullopt;
        }
        // The seat's `WHCart` stands at its frame, so the seat is the point off it.
        const Shared::Game::CartSeat &layout = *Seats().At(seat);
        return KCDC::Common::ToGlm(entity->GetWorldPos()) + KCDC::Common::ToGlm(entity->GetWorldRotation()) * (layout.point - layout.frame);
    }

    std::optional<std::uint8_t> Cart::SeatOfEntity(SDK::EntityId entityId) const {
        for (std::size_t seat = 0; seat < _seatEntityIds.size(); ++seat) {
            if (entityId != 0 && _seatEntityIds[seat] == entityId) {
                return static_cast<std::uint8_t>(seat);
            }
        }
        return std::nullopt;
    }

    std::vector<SDK::EntityId> Cart::GetCartEntityIds() const {
        std::vector<SDK::EntityId> ids;
        if (_cartEntityId != 0) {
            ids.push_back(_cartEntityId);
        }
        for (SDK::EntityId seatEntityId : _seatEntityIds) {
            if (seatEntityId != 0) {
                ids.push_back(seatEntityId);
            }
        }
        return ids;
    }

    /**
     * The team, the way it is driven here: not the game's harnessed horses,
     * which only ever walk an NPC caravan's road forward, but a horse with no
     * brain (`bIsDummy`, the engine's own NO_XAI flag) put at its place on
     * the pole every frame and playing the gait the cart's pace calls for.
     */
    void Cart::RequestHorses() {
        for (int slot = 0; slot < _harnessSlots; ++slot) {
            const std::size_t index = static_cast<std::size_t>(slot);
            if (_horseTickets[index] != nullptr) {
                continue;
            }
            if (_horses[index] != 0 && KCDC::Common::ResolveEntity(_horses[index]) != nullptr) {
                continue;
            }
            _horses[index]       = 0;
            _horseDressed[index] = false;
            _horseGround[index].reset();

            const float yaw    = HorseYaw(_drive);
            const glm::vec3 at = HorsePoint(_drive, index);

            KCDC::Common::Spawn::SpawnRequest request;
            request.actorClass = SDK::ActorClassNames::Horse;
            request.soulGuid   = Shared::Entities::HorseEntity::kDefaultSoulGuid;
            request.world      = KCDC::Common::Spawn::WorldPlacement {KCDC::Common::ToNative(at), KCDC::Common::ToNative(glm::angleAxis(yaw, glm::vec3(0.0F, 0.0F, 1.0F)))};
            // A horse in the shafts is the cart's: no brain to walk it off,
            // nobody gets on it and rides it out of them, and its saddle
            // screen has nothing to show.
            request.properties = {{"bIsDummy", true}, {"bNotPlayerMountable", true}, {"bNotPlayerInspectable", true}};

            _horseTickets[index] = KCDC::Core::gApplication->GetActorSpawner().Request(request, [this, index](KCDC::Common::Spawn::SpawnTicket &ticket, bool spawned) {
                OnHorseSpawned(index, ticket, spawned);
            });
        }
    }

    void Cart::OnHorseSpawned(std::size_t slot, KCDC::Common::Spawn::SpawnTicket &ticket, bool spawned) {
        _horseTickets[slot] = nullptr;
        if (!spawned) {
            Framework::Logging::GetLogger(kLogger)->info("Cart {}: horse {} did not spawn ({})", GetNetworkID(), slot, KCDC::Common::Spawn::SpawnResultToString(ticket.GetResult()));
            return;
        }
        _horses[slot]       = ticket.GetEntityId();
        _horseDressed[slot] = false;
        _gaitStarted        = false;
        _poseSettled        = false;

        // Every actor but the local player starts put away: no update slot,
        // the bind pose. Woken through the manager's own request, as a
        // replicated horse is.
        auto *actorSystem = SDK::IActorSystem::GetInstance();
        if (auto *horse = actorSystem != nullptr ? static_cast<SDK::wh::entitymodule::C_Actor *>(actorSystem->GetActor(_horses[slot])) : nullptr) {
            horse->RequestPutAway(false);
        }
    }

    /**
     * Each horse once it is up: grounded only -- a living body put somewhere
     * every frame shakes against whoever stands there, and the cart's own
     * parts, but its physics is what finds the ground its hooves sound on
     * (the footstep effect is looked up by the living body's ground surface;
     * a disabled collider has none) -- and the harness put on, the
     * game's own preset for the cart, and once more a second later, the
     * first equip not always showing.
     */
    void Cart::DressHorses() {
        auto *actorSystem        = SDK::IActorSystem::GetInstance();
        const MafiaNet::Time now = MafiaNet::GetTime();
        if (actorSystem == nullptr || !_rig) {
            return;
        }
        const bool again = _harnessAgainAt != 0 && now >= _harnessAgainAt;
        if (again) {
            _harnessAgainAt = 0;
        }

        for (int slot = 0; slot < _harnessSlots; ++slot) {
            const std::size_t index = static_cast<std::size_t>(slot);
            if (_horses[index] == 0 || (_horseDressed[index] && !again)) {
                continue;
            }
            auto *horse = static_cast<SDK::wh::entitymodule::C_Actor *>(actorSystem->GetActor(_horses[index]));
            if (horse == nullptr || !horse->IsSettled() || horse->IsPutAway()) {
                continue;
            }
            if (!KCDC::Common::EquipClothingPreset(horse, _rig->harness[index])) {
                continue;
            }
            if (!_horseDressed[index]) {
                if (SDK::IAnimatedCharacter *animated = horse->GetAnimatedCharacter()) {
                    animated->RequestPhysicalColliderMode(SDK::eColliderMode_GroundedOnly, SDK::eColliderModeLayer_Game, "");
                }
                _horseDressed[index] = true;
                _harnessAgainAt      = now + kHorseHarnessAgainMs;
                Framework::Logging::GetLogger(kLogger)->info("Cart {}: horse {} is in its harness", GetNetworkID(), slot);
            }
        }
    }

    float Cart::HorseYaw(const Shared::Game::CartDrive &drive) const {
        return _rig->articulated ? drive.leadYaw : drive.bodyYaw;
    }

    /**
     * Where a horse stands, on the lead point's ground: at its place in the
     * frame the cart turns its team with -- a wagon's front axle, turned to
     * the lead's yaw, or a two-wheeler's frame, turned to the body's.
     */
    glm::vec3 Cart::HorsePoint(const Shared::Game::CartDrive &drive, std::size_t slot) const {
        const float yaw        = HorseYaw(drive);
        const glm::vec2 offset = _rig->horses[slot] - glm::vec2(_rig->lead);
        const glm::vec2 ahead  = Shared::Game::CartHeading(yaw);
        const glm::vec2 right  = glm::vec2(ahead.y, -ahead.x);
        return glm::vec3(glm::vec2(drive.lead) + right * offset.x + ahead * offset.y, drive.lead.z);
    }

    // --- Driving ----------------------------------------------------------

    /**
     * The local player's reins. The drive is stepped from the keys, and what
     * it comes to is written into the replicated pose, which the stream
     * carries to everyone else because this client owns the cart while its
     * player sits at the reins.
     */
    void Cart::Drive(const Shared::Game::CartControls &controls, float frameTime) {
        const Shared::Game::CartGaits gaits = Gaits();
        // Checked the way the cart is going, or from a standstill the way the
        // driver asks it to: a cart standing against a wall would otherwise
        // creep into it and be stopped dead every other frame.
        const float heading                = std::fabs(_drive.speed) > Shared::Game::kCartStandSpeed ? _drive.speed : static_cast<float>(controls.throttle);
        const bool blocked                 = heading != 0.0F && IsBlocked(_drive, heading > 0.0F);
        const Shared::Game::CartDrive next = Shared::Game::StepCart(_drive, controls, gaits, _rig->wheelbase, _rig->articulated, blocked, frameTime);
        if (!Shared::Game::IsSaneCartDrive(next)) {
            return;
        }

        Shared::Entities::CartPace wanted = Shared::Entities::CartPace::Stand;
        if (next.speed > Shared::Game::kCartStandSpeed) {
            wanted = next.speed > gaits.walk + 0.1F ? Shared::Entities::CartPace::Trot : Shared::Entities::CartPace::Walk;
        }
        else if (next.speed < -Shared::Game::kCartStandSpeed) {
            wanted = Shared::Entities::CartPace::Reverse;
        }
        pace   = static_cast<std::uint8_t>(wanted);
        _drive = next;
        SetDrive(_drive);
    }

    /** Somebody else's reins, or nobody's: the drive drawn a little behind the stream, as every replicated pose is. */
    void Cart::Follow() {
        CartSnapshot sampled;
        if (_snapshots.Sample(KCDC::Common::Replication::PoseRenderTime(_snapshots), sampled)) {
            _drive = sampled.drive;
            return;
        }
        // Nothing on the stream yet, or never again: where the cart was put.
        _drive = GetDrive();
    }

    /**
     * Something solid in the team's way. A ray at knee height -- a log pile
     * or a wall stops the team, a kerb does not, the wheels take that --
     * from just past the horses' heads going forward, or from behind the bed
     * backing up. Only what the level is built of counts, and other entities:
     * the cart's own parts and horses are passed over, and so is anything of
     * the game's own kinds (foreign ids past `PHYS_FOREIGN_ID_USER`), which is
     * what a horse's own gear is.
     */
    bool Cart::IsBlocked(const Shared::Game::CartDrive &drive, bool forward) {
        auto *physicalWorld = SDK::CPhysicalWorld::GetInstance();
        if (physicalWorld == nullptr) {
            return false;
        }

        const glm::vec2 along = forward ? Shared::Game::CartHeading(drive.leadYaw) : -Shared::Game::CartHeading(drive.bodyYaw);
        const glm::vec2 from  = forward ? glm::vec2(drive.lead) + along * kObstacleAhead : glm::vec2(drive.Axle(_rig->wheelbase)) + along * _rig->tail;
        const float reach     = forward ? kObstacleReach + std::fabs(drive.speed) * kObstacleReachPerSpeed : kObstacleReachBack;
        const glm::vec3 origin(from, (forward ? _leadGround : _axleGround).value_or(drive.lead.z) + kObstacleHeight);

        // Down the middle at knee height, and going forward down each horse's
        // line at its body's height as well: the team walks side by side,
        // wider than one ray, and what a horse walks into beside the track is a
        // wall, not a rock or a railing it passes over.
        struct Lane {
            float side;
            float height;
            bool levelOnly; // the team's own heads and gear stand in a horse's line
        };
        const glm::vec2 right = glm::vec2(along.y, -along.x);
        std::vector<Lane> lanes {{0.0F, kObstacleHeight, false}};
        for (std::size_t slot = 0; forward && slot < _rig->horseCount; ++slot) {
            lanes.push_back({_rig->horses[slot].x - _rig->lead.x, kHorseBodyHeight, true});
        }

        const std::vector<SDK::IPhysicalEntity *> skip = OwnBodies();
        for (const Lane &lane : lanes) {
            const glm::vec3 laneOrigin = origin + glm::vec3(right * lane.side, lane.height - kObstacleHeight);
            SDK::SRayHit hit {};
            float travelled = 0.0F;
            for (int attempt = 0; attempt < 4 && travelled < reach; ++attempt) {
                const glm::vec3 start = laneOrigin + glm::vec3(along * travelled, 0.0F);
                const glm::vec3 ray   = glm::vec3(along * (reach - travelled), 0.0F);
                if (physicalWorld->RayWorldIntersection(KCDC::Common::ToNative(start), KCDC::Common::ToNative(ray), SDK::ent_static | SDK::ent_rigid | SDK::ent_sleeping_rigid, SDK::CPhysicalWorld::kGroundRayFlags, &hit, 1,
                        const_cast<SDK::IPhysicalEntity **>(skip.data()), static_cast<int>(skip.size()))
                    <= 0) {
                    break;
                }
                auto *collider    = static_cast<SDK::IPhysicalEntity *>(hit.collider);
                const int foreign = collider != nullptr ? collider->GetiForeignData() : SDK::PHYS_FOREIGN_ID_STATIC;
                // Only an upright face is in the way: the ray grazes the undersides
                // of a bridge's beams, bevelled or flat, and runs up the top of a ramp.
                const bool upright = hit.normal.z > -kOverheadNormalZ && hit.normal.z < kLevelNormalZ;
                const bool solid   = foreign == SDK::PHYS_FOREIGN_ID_STATIC || (!lane.levelOnly && (foreign == SDK::PHYS_FOREIGN_ID_ENTITY || foreign == SDK::PHYS_FOREIGN_ID_FOLIAGE));
                if (solid && upright && !IsStepUp(physicalWorld, hit, start.z - lane.height, skip)) {
                    return true;
                }
                travelled += hit.distance + 0.05F;
            }
        }
        return false;
    }

    // --- Posing -----------------------------------------------------------

    /**
     * The whole cart from a drive: the chassis's world transform written, the
     * wheels rolled by how far they went, each horse put at its place on the
     * pole and stepped in its gait.
     *
     * A wagon's chassis is its front axle, turned to the lead's yaw about the
     * lead point, the front axle's centre; the body hangs off it and is turned
     * on that same centre to the body's yaw, as a wagon's bed turns on its
     * kingpin. A two-wheeler is one frame on its axle, turned to the body's
     * yaw and tipped up to its horse's harness. Both are pitched by the
     * ground under the lead point against the ground under the trailing axle.
     */
    void Cart::Present(const Shared::Game::CartDrive &drive, float frameTime) {
        SDK::IEntity *chassis = KCDC::Common::ResolveEntity(_chassisEntityId);
        if (chassis == nullptr) {
            return;
        }
        // A cart standing where it was last posed, on ground it has settled
        // onto, is left as it is: nothing about it would change.
        const bool unchanged = _posed && drive.lead == _posedDrive.lead && drive.leadYaw == _posedDrive.leadYaw && drive.bodyYaw == _posedDrive.bodyYaw;
        if (unchanged && _poseSettled) {
            return;
        }

        // The drive's height changes only when the cart is put somewhere --
        // spawned or teleported: the ground there is looked for afresh.
        if (_posed && drive.lead.z != _posedDrive.lead.z) {
            _leadGround.reset();
            _axleGround.reset();
            _horseGround = {};
            _leadFall    = 0.0F;
            _axleFall    = 0.0F;
        }

        const std::vector<SDK::IPhysicalEntity *> skip = OwnBodies();
        const glm::vec3 axle                           = drive.Axle(_rig->wheelbase);
        const auto settle                              = [frameTime](std::optional<float> &eased, float &fall, float raw) {
            if (!eased) {
                eased = raw;
                fall  = 0.0F;
            }
            else if (raw < *eased - kWheelStep || (fall > 0.0F && raw < *eased)) {
                fall += kWheelGravity * frameTime;
                eased = std::max(raw, *eased - fall * frameTime);
            }
            else {
                fall  = 0.0F;
                eased = *eased + (raw - *eased) * std::min(1.0F, frameTime / (raw - *eased > kWheelStep ? kWheelStepEase : kGroundEase));
            }
            if (*eased == raw) {
                fall = 0.0F;
            }
            return *eased;
        };
        const auto probe = [&](const glm::vec3 &at, const std::optional<float> &eased) {
            return GroundAt(glm::vec3(glm::vec2(at), eased.value_or(at.z)), skip, eased ? kGroundProbeUp : kGroundSeek);
        };
        const float leadRaw    = probe(drive.lead, _leadGround);
        const float axleRaw    = probe(axle, _axleGround);
        const float leadGround = settle(_leadGround, _leadFall, leadRaw);
        const float axleGround = settle(_axleGround, _axleFall, axleRaw);
        const float pitch      = std::atan2(leadGround - axleGround, _rig->wheelbase);

        // A two-wheeler is tipped up about its axle, the hub staying where it is.
        const float yaw       = HorseYaw(drive);
        const glm::quat frame = glm::angleAxis(yaw, glm::vec3(0.0F, 0.0F, 1.0F)) * glm::angleAxis(pitch + _rig->lift, glm::vec3(1.0F, 0.0F, 0.0F));
        const glm::quat flat  = glm::angleAxis(yaw, glm::vec3(0.0F, 0.0F, 1.0F)) * glm::angleAxis(pitch, glm::vec3(1.0F, 0.0F, 0.0F));
        const glm::vec3 pivot = _rig->articulated ? glm::vec3(glm::vec2(drive.lead), leadGround) : glm::vec3(glm::vec2(axle), axleGround);
        const glm::vec3 local = _rig->articulated ? _rig->lead : _rig->axle;
        const glm::vec3 hub   = pivot + flat * glm::vec3(0.0F, 0.0F, local.z) + glm::vec3(0.0F, 0.0F, _rig->height);
        const glm::vec3 at    = hub - frame * local;

        const SDK::Vec3 chassisPosition = KCDC::Common::ToNative(at);
        const SDK::Quat chassisRotation = KCDC::Common::ToNative(frame);
        chassis->SetWorldTM(SDK::Matrix34FromTransform(chassisPosition, chassisRotation, SDK::Vec3 {1.0F, 1.0F, 1.0F}));

        if (_rig->articulated) {
            const glm::quat turn  = glm::angleAxis(Shared::Game::CartWrapAngle(drive.bodyYaw - drive.leadYaw), glm::vec3(0.0F, 0.0F, 1.0F));
            const glm::vec3 hinge = glm::vec3(_rig->lead.x, _rig->lead.y, 0.0F);
            const glm::vec3 where = hinge + turn * (_rig->bodyPosition - hinge);
            if (SDK::IEntity *body = KCDC::Common::ResolveEntity(_bodyEntityId)) {
                body->SetLocalTM(SDK::Matrix34FromTransform(KCDC::Common::ToNative(where), KCDC::Common::ToNative(turn * _rig->bodyRotation), SDK::Vec3 {1.0F, 1.0F, 1.0F}));
            }
        }

        _cart->GetFollowerModel()->RollWheels(KCDC::Common::ToNative(flat * glm::vec3(0.0F, 1.0F, 0.0F)));

        const bool teamSettled = PlaceTeam(drive, leadGround, skip, frameTime);
        _poseSettled           = unchanged && teamSettled && std::fabs(leadGround - leadRaw) < kGroundSettled && std::fabs(axleGround - axleRaw) < kGroundSettled;
        _posedDrive            = drive;
        _posed                 = true;
    }

    bool Cart::StartClip(SDK::IEntity *entity, std::string_view clip, std::int32_t layer, float blend, float rate, bool loop) {
        SDK::ICharacterInstance *character = entity != nullptr ? entity->GetCharacter(0) : nullptr;
        SDK::ISkeletonAnim *skeleton       = character != nullptr ? character->GetISkeletonAnim() : nullptr;
        SDK::IAnimationSet *animations     = character != nullptr ? character->GetIAnimationSet() : nullptr;
        if (skeleton == nullptr || animations == nullptr) {
            return false;
        }
        const std::string name(clip);
        const int animationId = animations->GetAnimIDByName(name.c_str());
        if (animationId < 0) {
            return false;
        }
        SDK::CryCharAnimationParams params(layer, loop ? SDK::CA_LOOP_ANIMATION : SDK::CA_REPEAT_LAST_KEY);
        params.m_fTransTime = blend;
        const bool started  = skeleton->StartAnimationById(animationId, params);
        skeleton->SetLayerPlaybackScale(layer, rate);
        return started;
    }

    float Cart::ClipLength(SDK::IEntity *entity, std::string_view clip) {
        SDK::ICharacterInstance *character = entity != nullptr ? entity->GetCharacter(0) : nullptr;
        SDK::IAnimationSet *animations     = character != nullptr ? character->GetIAnimationSet() : nullptr;
        if (animations == nullptr) {
            return 0.0F;
        }
        const std::string name(clip);
        const int animationId = animations->GetAnimIDByName(name.c_str());
        return animationId >= 0 ? animations->GetDuration_sec(animationId) : 0.0F;
    }

    /**
     * Each horse put at its place on the pole, on the ground under it, facing
     * the way the pole does. Whether every horse is up and has settled onto
     * its ground.
     */
    bool Cart::PlaceTeam(const Shared::Game::CartDrive &drive, float leadGround, const std::vector<SDK::IPhysicalEntity *> &skip, float frameTime) {
        const float yaw          = HorseYaw(drive);
        const SDK::Quat rotation = KCDC::Common::ToNative(glm::angleAxis(yaw, glm::vec3(0.0F, 0.0F, 1.0F)));
        bool settled             = true;
        for (int slot = 0; slot < _harnessSlots; ++slot) {
            const std::size_t index = static_cast<std::size_t>(slot);
            SDK::IEntity *horse     = _horses[index] != 0 ? KCDC::Common::ResolveEntity(_horses[index]) : nullptr;
            if (horse == nullptr) {
                settled = false;
                continue;
            }
            glm::vec3 at                = HorsePoint(drive, index);
            const float raw             = GroundAt(glm::vec3(glm::vec2(at), leadGround), skip, kGroundProbeUp);
            std::optional<float> &eased = _horseGround[index];
            eased                       = !eased || std::fabs(raw - *eased) > kGroundJump ? raw : *eased + (raw - *eased) * std::min(1.0F, frameTime / kGroundEase);
            at.z                        = *eased;
            horse->SetWorldTM(SDK::Matrix34FromTransform(KCDC::Common::ToNative(at), rotation, SDK::Vec3 {1.0F, 1.0F, 1.0F}));
            settled = settled && std::fabs(*eased - raw) < kGroundSettled;
        }
        return settled;
    }

    /**
     * The team's gait, on one clock for the cart so both horses step as one.
     *
     * Each horse is an actor whose own graph changes its idle every few
     * seconds and takes the gait layer with it: one horse stood while the
     * other trotted. So the clock -- the gait's phase, advanced at the rate
     * the pace plays the clip at -- sets both horses' time every frame, and
     * the gait's clip is started again every tenth of a second, which puts it
     * back wherever the graph took the layer. A change between two moving
     * gaits waits until it has held for a moment and keeps the stride's
     * phase; from or to the idle it is taken at once.
     */
    void Cart::StepTeamGait(float speed, float frameTime) {
        SDK::IEntity *first = _horses[0] != 0 ? KCDC::Common::ResolveEntity(_horses[0]) : nullptr;
        if (first == nullptr) {
            return;
        }
        const MafiaNet::Time now = MafiaNet::GetTime();

        const Shared::Game::CartHorseGait wanted = Shared::Game::ChooseCartHorseGait(speed, _gaitStarted ? _gait : Shared::Game::CartHorseGait::Idle, _driving ? kHorseGaitDriverMargin : kHorseGaitCopyMargin);
        Shared::Game::CartHorseGait gait         = wanted;
        const bool moving                        = wanted != Shared::Game::CartHorseGait::Idle && _gait != Shared::Game::CartHorseGait::Idle;
        if (_gaitStarted && wanted != _gait && moving) {
            if (!_gaitNext || *_gaitNext != wanted) {
                _gaitNext      = wanted;
                _gaitNextSince = now;
            }
            if (now - _gaitNextSince < kHorseGaitHoldMs) {
                gait = _gait;
            }
        }
        else {
            _gaitNext.reset();
        }
        const float rate = gait == Shared::Game::CartHorseGait::Idle ? 1.0F : Shared::Game::CartHorseGaitRate(gait, speed);

        if (!_gaitStarted || gait != _gait) {
            const bool keepPhase = _gaitStarted && _gait != Shared::Game::CartHorseGait::Idle && gait != Shared::Game::CartHorseGait::Idle;
            _gait                = gait;
            _gaitStarted         = true;
            _gaitPhase           = keepPhase ? _gaitPhase : 0.0F;
            _gaitRate            = rate;
            _gaitCheckAt         = now + kHorseGaitCheckMs;
            _gaitNext.reset();

            const float length = ClipLength(first, HorseGaitClip(gait));
            _gaitLength        = length > 0.05F ? length : 0.0F;
            for (SDK::EntityId horseId : _horses) {
                if (SDK::IEntity *horse = horseId != 0 ? KCDC::Common::ResolveEntity(horseId) : nullptr) {
                    StartClip(horse, HorseGaitClip(gait), kHorseLayer, kHorseGaitBlend, rate, true);
                }
            }
        }
        if (_gaitLength > 0.0F) {
            _gaitPhase = std::fmod(_gaitPhase + frameTime * rate / _gaitLength, 1.0F);
        }

        const bool check = now >= _gaitCheckAt;
        if (check) {
            _gaitCheckAt = now + kHorseGaitCheckMs;
        }
        const bool respeed = std::fabs(rate - _gaitRate) > 0.05F;
        if (respeed) {
            _gaitRate = rate;
        }

        for (int slot = 0; slot < _harnessSlots; ++slot) {
            const std::size_t index            = static_cast<std::size_t>(slot);
            SDK::IEntity *horse                = _horses[index] != 0 ? KCDC::Common::ResolveEntity(_horses[index]) : nullptr;
            SDK::ICharacterInstance *character = horse != nullptr ? horse->GetCharacter(0) : nullptr;
            SDK::ISkeletonAnim *skeleton       = character != nullptr ? character->GetISkeletonAnim() : nullptr;
            if (skeleton == nullptr) {
                continue;
            }
            if (check) {
                StartClip(horse, HorseGaitClip(gait), kHorseLayer, kHorseGaitRestartBlend, rate, true);
            }
            if (respeed) {
                skeleton->SetLayerPlaybackScale(kHorseLayer, rate);
            }
            if (_gaitLength > 0.0F) {
                const float phase = gait == Shared::Game::CartHorseGait::Idle ? std::fmod(_gaitPhase + static_cast<float>(slot) * kHorseIdleStagger, 1.0F) : _gaitPhase;
                skeleton->SetLayerNormalizedTime(kHorseLayer, phase);
            }
        }
    }

    /**
     * Read fresh every frame rather than kept: CryPhysics writes into each
     * body it is told to skip through `MarkSkipEnts`, so a pointer kept
     * past its body's re-physicalization would be a write into freed memory.
     */
    std::vector<SDK::IPhysicalEntity *> Cart::OwnBodies() const {
        std::vector<SDK::IPhysicalEntity *> bodies;
        bodies.reserve(_parts.size() + _horses.size());
        for (SDK::EntityId entityId : _parts) {
            SDK::IEntity *entity = KCDC::Common::ResolveEntity(entityId);
            if (SDK::IPhysicalEntity *body = entity != nullptr ? entity->GetPhysics() : nullptr) {
                bodies.push_back(body);
            }
        }
        for (SDK::EntityId entityId : _horses) {
            SDK::IEntity *entity = entityId != 0 ? KCDC::Common::ResolveEntity(entityId) : nullptr;
            if (SDK::IPhysicalEntity *body = entity != nullptr ? entity->GetPhysics() : nullptr) {
                bodies.push_back(body);
            }
        }
        return bodies;
    }

    float Cart::GroundAt(const glm::vec3 &point, const std::vector<SDK::IPhysicalEntity *> &skip, float reach) const {
        auto *physicalWorld = SDK::CPhysicalWorld::GetInstance();
        if (physicalWorld == nullptr) {
            return point.z;
        }

        // The terrain, and static geometry -- a bridge, a paved yard built of
        // brushes -- where it stands above the terrain and within a step of
        // the point: the points probed lie under the team and the shafts,
        // where anything static higher than that is in the way rather than
        // underfoot, and rocks under the terrain are under the map.
        const SDK::Vec3 origin {point.x, point.y, point.z + reach};
        const SDK::Vec3 down {0.0F, 0.0F, -(reach + std::max(reach, kGroundProbeDown))};
        SDK::SRayHit hit {};
        std::optional<float> terrain;
        if (physicalWorld->RayWorldIntersection(origin, down, SDK::ent_terrain, SDK::CPhysicalWorld::kGroundRayFlags, &hit, 1) > 0) {
            terrain = hit.point.z;
        }
        if (physicalWorld->RayWorldIntersection(origin, down, SDK::ent_static, SDK::CPhysicalWorld::kGroundRayFlags, &hit, 1, const_cast<SDK::IPhysicalEntity **>(skip.data()), static_cast<int>(skip.size())) > 0 && hit.point.z <= point.z + kGroundStep
            && (!terrain || hit.point.z > *terrain) && IsBroadSurface(physicalWorld, glm::vec3(hit.point.x, hit.point.y, hit.point.z), skip)) {
            return hit.point.z;
        }
        return terrain.value_or(point.z);
    }

    // --- Lifetime ---------------------------------------------------------

    void Cart::Despawn() {
        auto &spawner = KCDC::Core::gApplication->GetActorSpawner();
        for (std::size_t slot = 0; slot < _horses.size(); ++slot) {
            spawner.Return(_horseTickets[slot]);
            _horseTickets[slot] = nullptr;
        }

        // Children first, so a part that failed to attach is not left behind;
        // the horses before the cart they stand at.
        std::vector<SDK::EntityId> entities;
        for (SDK::EntityId horse : _horses) {
            if (horse != 0) {
                entities.push_back(horse);
            }
        }
        entities.insert(entities.end(), _parts.rbegin(), _parts.rend());
        if (!entities.empty()) {
            CartService::Get().Retire(GetCartEntityIds(), std::move(entities));
        }

        if (_chassisEntityId != 0) {
            Framework::Logging::GetLogger(kLogger)->info("Despawned cart {}", GetNetworkID());
        }

        _parts.clear();
        _horses          = {};
        _horseDressed    = {};
        _horseGround     = {};
        _chassisEntityId = 0;
        _bodyEntityId    = 0;
        _cartEntityId    = 0;
        _seatEntityIds   = {};
        _seatCarts       = {};
        _cart            = nullptr;
        _leadGround.reset();
        _axleGround.reset();
        _leadFall       = 0.0F;
        _axleFall       = 0.0F;
        _gaitStarted    = false;
        _harnessAgainAt = 0;
        _posed          = false;
        _poseSettled    = false;
    }
} // namespace KCDC::Features::Cart
