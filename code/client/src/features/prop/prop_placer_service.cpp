#include <utils/safe_win32.h>

#include "prop_placer_service.h"

#include "core/application.h"
#include "features/camera/camera_view.h"
#include "features/world/world_query_service.h"

#include "common/module_support.h"
#include "common/vector_math.h"

#include "sdk/cryengine/entitysystem/i_entity.h"
#include "sdk/cryengine/input/i_input.h"
#include "sdk/cryengine/system/cry_name.h"
#include "sdk/cryengine/system/i_system.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <logging/logger.h>

namespace KCDC::Features::Prop {
    namespace {
        constexpr const char *kLogger = "PropPlacer";

        constexpr float kPi           = 3.14159265358979323846F;
        constexpr float kDegreesToRad = kPi / 180.0F;
        constexpr float kRadToDegrees = 180.0F / kPi;

        constexpr char kMapName[]    = "kcdc_prop_placer";
        constexpr char kFilterName[] = "kcdc_prop_placer_block";

        /** Any value does: nothing else holds a reason on this filter. */
        constexpr std::uint8_t kFilterReason = 0;

        /**
         * The map the mode owns while it runs. **Not exclusive** -- an
         * exclusive map takes `rotatepitch` and `rotateyaw` with everything
         * else, leaving a preview that cannot be aimed. What the game would
         * otherwise answer is taken away by the filter below.
         *
         * `keyboard` carries the mouse in CryEngine's input vocabulary.
         */
        constexpr char kMapXml[] = "<actionmap name=\"kcdc_prop_placer\">"
                                   "<action name=\"kcdc_place_confirm\" onPress=\"1\" keyboard=\"mouse1\" xboxpad=\"xi_a\" pspad=\"pad_cross\"/>"
                                   "<action name=\"kcdc_place_cancel\" onPress=\"1\" keyboard=\"mouse2\" xboxpad=\"xi_b\" pspad=\"pad_circle\"/>"
                                   "<action name=\"kcdc_place_rotate_left\" onPress=\"1\" onHold=\"1\" holdTriggerDelay=\"0.3\" holdRepeatDelay=\"0.05\">"
                                   "<keyboard><inputdata input=\"mwheel_down\"/><inputdata input=\"q\"/></keyboard>"
                                   "<xboxpad><inputdata input=\"xi_shoulderl\"/></xboxpad>"
                                   "</action>"
                                   "<action name=\"kcdc_place_rotate_right\" onPress=\"1\" onHold=\"1\" holdTriggerDelay=\"0.3\" holdRepeatDelay=\"0.05\">"
                                   "<keyboard><inputdata input=\"mwheel_up\"/><inputdata input=\"e\"/></keyboard>"
                                   "<xboxpad><inputdata input=\"xi_shoulderr\"/></xboxpad>"
                                   "</action>"
                                   "</actionmap>";

        /**
         * What the game must not answer while a spot is being chosen.
         *
         * `CActionMapManager::ActionFiltered` is consulted for every bind
         * before it fires, so these never reach the player's hands while look
         * and movement carry on. The names are the game's own.
         */
        constexpr char kFilterXml[] = "<actionfilter name=\"kcdc_prop_placer_block\" type=\"actionFail\">"
                                      // The strike, however it is reached.
                                      "<action name=\"attack_primary\"/>"
                                      "<action name=\"attack_primary_mouse\"/>"
                                      "<action name=\"prepare_to_attack\"/>"
                                      "<action name=\"attack_abort\"/>"
                                      "<action name=\"attack_abort_mouse\"/>"
                                      "<action name=\"attack_stop\"/>"
                                      "<action name=\"block\"/>"
                                      // Target and guard.
                                      "<action name=\"opponent_lock\"/>"
                                      "<action name=\"combat_unlock_mouse\"/>"
                                      "<action name=\"combat_zone_mouse_x\"/>"
                                      "<action name=\"combat_zone_mouse_y\"/>"
                                      "<action name=\"combat_switch_opp_prev\"/>"
                                      "<action name=\"combat_switch_opp_next\"/>"
                                      // The direction system: a strike is a direction picked
                                      // with the mouse and then committed, so the whole family
                                      // has to go or the commit finds a prepared swing.
                                      "<action name=\"sword_position\"/>"
                                      "<action name=\"sword_position_left\"/>"
                                      "<action name=\"sword_position_right\"/>"
                                      "<action name=\"sword_rotation\"/>"
                                      "<action name=\"sword_rotation_up\"/>"
                                      "<action name=\"sword_rotation_down\"/>"
                                      "<action name=\"sword_pressure_up\"/>"
                                      "<action name=\"sword_pressure_down\"/>"
                                      "<action name=\"sword_toggle_pressure\"/>"
                                      // Stealth is still an attack.
                                      "<action name=\"stealth_kill_attack\"/>"
                                      "<action name=\"stealth_kill_silent_attack\"/>"
                                      "<action name=\"combat_stealth_attack\"/>"
                                      "<action name=\"combat_stealth_silent_attack\"/>"
                                      "</actionfilter>";

        // Deliberately absent: `rotatepitch`, `rotateyaw`, `mouse_looking_x`
        // and the `move*` family are how the player aims and walks.

        /** What `eAAM_OnPress` is, which is the only activation this mode acts on. */
        constexpr int kOnPress = 1;

        /** Degrees the wheel or a shoulder button turns the ghost per step. */
        constexpr float kRotateStep = 15.0F;

        float Length(const SDK::Vec3 &value) {
            return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
        }

        SDK::Vec3 Normalized(const SDK::Vec3 &value, const SDK::Vec3 &fallback) {
            const float length = Length(value);
            return length > 1e-6F ? SDK::Vec3 {value.x / length, value.y / length, value.z / length} : fallback;
        }

        /**
         * A yaw about Z, in the engine's own convention: zero faces +Y, which
         * is where an identity rotation leaves a brush.
         */
        float YawTowards(float dx, float dy) {
            return (dx * dx + dy * dy) > 1e-8F ? std::atan2(-dx, dy) : 0.0F;
        }

        /**
         * A rotation from a yaw and, optionally, a surface to stand on.
         *
         * The aligned case builds the basis directly: CryEngine's object
         * matrix reads its columns as right, forward, up, so a matrix of
         * those three unit vectors is already the rotation.
         */
        SDK::Quat RotationFor(float yawRadians, const SDK::Vec3 &normal, bool align) {
            const glm::vec3 flat {-std::sin(yawRadians), std::cos(yawRadians), 0.0F};

            if (!align) {
                return KCDC::Common::ToNative(glm::angleAxis(yawRadians, glm::vec3(0.0F, 0.0F, 1.0F)));
            }

            glm::vec3 up = glm::vec3(normal.x, normal.y, normal.z);
            if (glm::length(up) < 1e-6F) {
                up = glm::vec3(0.0F, 0.0F, 1.0F);
            }
            up = glm::normalize(up);

            glm::vec3 right = glm::cross(flat, up);
            if (glm::length(right) < 1e-4F) {
                // Aiming along the normal leaves no facing to keep.
                right = glm::cross(glm::vec3(0.0F, 0.0F, 1.0F), up);
                if (glm::length(right) < 1e-4F) {
                    right = glm::vec3(1.0F, 0.0F, 0.0F);
                }
            }
            right = glm::normalize(right);

            const glm::vec3 forward = glm::normalize(glm::cross(up, right));
            return KCDC::Common::ToNative(glm::normalize(glm::quat_cast(glm::mat3(right, forward, up))));
        }

        KCDC::Features::World::WorldQueryService *Queries() {
            return KCDC::Core::gApplication ? &KCDC::Core::gApplication->GetWorldQueryService() : nullptr;
        }

        PropGhostService *Ghosts() {
            return KCDC::Core::gApplication ? &KCDC::Core::gApplication->GetPropGhostService() : nullptr;
        }
    } // namespace

    bool PropPlacerService::AcquireInput() {
        SDK::ISystem *system            = SDK::ISystem::GetInstance();
        SDK::IActionMapManager *manager = SDK::IActionMapManager::GetInstance();
        SDK::IInput *input              = SDK::IInput::GetInstance();
        if (system == nullptr || manager == nullptr || input == nullptr || !input->IsEventPostingEnabled()) {
            return false;
        }

        // A leftover map would be one a previous session failed to release.
        if (manager->GetActionMap(kMapName) != nullptr) {
            Framework::Logging::GetLogger(kLogger)->warn("The placement action map is already owned; refusing to start");
            return false;
        }

        SDK::XmlNodeRef node = system->LoadXmlFromBuffer(kMapXml, sizeof(kMapXml) - 1, false, false);
        if (!node.IsValid()) {
            Framework::Logging::GetLogger(kLogger)->warn("The placement action map would not parse");
            return false;
        }

        SDK::IActionMap *map = manager->CreateActionMap(kMapName);
        if (map == nullptr) {
            return false;
        }

        if (!map->LoadFromXML(node)) {
            manager->RemoveActionMap(kMapName);
            Framework::Logging::GetLogger(kLogger)->warn("The placement action map would not load its actions");
            return false;
        }

        if (!manager->AddExtraActionListener(this, kMapName)) {
            manager->RemoveActionMap(kMapName);
            return false;
        }

        _inputManager = manager;
        _inputMap     = map;
        _listening    = true;
        manager->EnableActionMap(kMapName, true);

        // The map adds this mode's actions; the filter is what takes the
        // game's own attack and block away while it runs. Without it a confirm
        // click would also swing; with an exclusive map instead of it, the
        // player could not turn the camera at all.
        // Through the filter itself: KCD2 has no manager-level enable, and
        // the slot stock CryEngine keeps one in is a query here. `Enable`
        // also releases whatever the filtered actions were holding.
        _filter = manager->FindOrCreateActionFilter(kFilterName, kFilterXml, sizeof(kFilterXml) - 1);
        if (_filter != nullptr) {
            _filter->Enable(kFilterReason);
        }

        // Asked rather than assumed: without it the mode still runs, but
        // every click also reaches the player's hands.
        _filtering = _filter != nullptr && manager->IsFilterEnabled(kFilterName);
        if (!_filtering) {
            Framework::Logging::GetLogger(kLogger)->warn("Placing without the combat action filter: clicks will also reach the player's hands");
        }
        return true;
    }

    void PropPlacerService::ReleaseInput() {
        SDK::IActionMapManager *manager = SDK::IActionMapManager::GetInstance();

        // Only while it is still the manager this service took: a level
        // change replaces it, and the old one is freed.
        if (manager != nullptr && manager == _inputManager) {
            // Every reason at once: nothing else holds this filter, and
            // clearing cannot leave it stuck on.
            if (_filter != nullptr && manager->GetActionFilter(kFilterName) == _filter) {
                _filter->Disable(kFilterReason, true);
            }
            if (_inputMap != nullptr && manager->GetActionMap(kMapName) == _inputMap) {
                manager->EnableActionMap(kMapName, false);
                if (_listening) {
                    manager->RemoveExtraActionListener(this, kMapName);
                }
                manager->RemoveActionMap(kMapName);
            }
        }

        _inputManager = nullptr;
        _inputMap     = nullptr;
        _listening    = false;
        _filtering    = false;
        _filter       = nullptr;
    }

    bool PropPlacerService::Begin(const PlacementSpec &spec, IPropPlacerSink *sink) {
        if (_active || sink == nullptr || spec.parts.empty()) {
            return false;
        }

        PropGhostService *ghosts = Ghosts();
        if (ghosts == nullptr || !KCDC::Core::gApplication || KCDC::Core::gApplication->AreControlsLocked()) {
            return false;
        }

        KCDC::Features::Camera::ViewPose view;
        if (!KCDC::Features::Camera::ReadViewPose(view)) {
            return false;
        }

        _spec   = spec;
        _sink   = sink;
        _vetoed = false;
        _valid  = false;
        _hasPose = false;
        _pending = Pending::None;
        _pendingRotation = 0.0F;
        _lastReport      = {};

        _spec.range            = std::clamp(_spec.range, 1.0F, 512.0F);
        _spec.fallbackDistance = std::clamp(_spec.fallbackDistance, 0.5F, _spec.range);
        _spec.scale            = std::isfinite(_spec.scale) ? std::clamp(_spec.scale, 0.01F, 100.0F) : 1.0F;

        // Parked at the camera until the first `Aim` settles it.
        _ghost = ghosts->Create(_spec.parts, _spec.invalidTint, view.position, SDK::Quat {0.0F, 0.0F, 0.0F, 1.0F}, _spec.scale);
        if (_ghost == PropGhostService::kNoHandle) {
            _sink = nullptr;
            return false;
        }

        if (!AcquireInput()) {
            ghosts->Destroy(_ghost);
            _ghost = PropGhostService::kNoHandle;
            _sink  = nullptr;
            return false;
        }

        _active = true;

        // Settled before the first frame, so it never jumps to the crosshair.
        if (Aim()) {
            _valid = Evaluate(_pose);
            ghosts->SetPose(_ghost, _pose.position, _pose.rotation, _spec.scale);
            ghosts->SetTint(_ghost, _valid ? _spec.validTint : _spec.invalidTint);
        }

        Framework::Logging::GetLogger(kLogger)->info("Placement started with {} part(s)", _spec.parts.size());
        return true;
    }

    void PropPlacerService::End() {
        if (!_active) {
            return;
        }

        _active = false;
        ReleaseInput();

        if (PropGhostService *ghosts = Ghosts(); ghosts != nullptr && _ghost != PropGhostService::kNoHandle) {
            ghosts->Destroy(_ghost);
        }
        _ghost = PropGhostService::kNoHandle;

        IPropPlacerSink *sink = _sink;
        _sink                 = nullptr;
        _pending              = Pending::None;
        _pendingRotation      = 0.0F;
        _hasPose              = false;

        if (sink != nullptr) {
            sink->OnPlacementEnd();
        }
    }

    bool PropPlacerService::SetVeto(bool vetoed) {
        if (!_active) {
            return false;
        }
        _vetoed = vetoed;
        return true;
    }

    bool PropPlacerService::Rotate(float degrees) {
        if (!_active || !std::isfinite(degrees)) {
            return false;
        }
        _spec.yawDegrees = std::fmod(_spec.yawDegrees + degrees, 360.0F);
        return true;
    }

    bool PropPlacerService::ReadPose(PlacementPose &pose, bool &valid) const {
        if (!_active || !_hasPose) {
            return false;
        }
        pose  = _pose;
        valid = _valid;
        return true;
    }

    void PropPlacerService::OnAction(const SDK::CCryName &action, int activationMode, float) {
        if (!_active || !_listening || activationMode != kOnPress) {
            return;
        }

        const char *name = action.c_str();

        // Latched, not acted on: this runs inside the engine's input
        // dispatch, and confirming here would raise script from within it.
        if (std::strcmp(name, "kcdc_place_confirm") == 0) {
            _pending = Pending::Confirm;
        }
        else if (std::strcmp(name, "kcdc_place_cancel") == 0) {
            _pending = Pending::Cancel;
        }
        else if (std::strcmp(name, "kcdc_place_rotate_left") == 0) {
            _pendingRotation -= kRotateStep;
        }
        else if (std::strcmp(name, "kcdc_place_rotate_right") == 0) {
            _pendingRotation += kRotateStep;
        }
    }

    bool PropPlacerService::Aim() {
        KCDC::Features::Camera::ViewPose view;
        KCDC::Features::World::WorldQueryService *queries = Queries();
        if (queries == nullptr || !KCDC::Features::Camera::ReadViewPose(view)) {
            return false;
        }

        const SDK::Vec3 heading = KCDC::Features::Camera::RayThroughNdc(view, 0.0F, 0.0F);

        KCDC::Features::World::WorldQueryService::RayRequest request;
        request.origin = view.position;
        request.target = SDK::Vec3 {view.position.x + heading.x * _spec.range, view.position.y + heading.y * _spec.range, view.position.z + heading.z * _spec.range};

        // `Ground` rather than `Anything`: a preview stands on what a body
        // could stand on, not on a passing NPC.
        request.mode              = KCDC::Shared::RPC::WorldRayMode::Ground;
        request.maxHits           = 1;
        request.ignoreLocalPlayer = true;

        std::vector<KCDC::Features::World::WorldQueryService::RayHit> hits;
        PlacementPose pose;

        if (queries->Raycast(request, hits) && !hits.empty()) {
            pose.position  = hits.front().point;
            pose.normal    = Normalized(hits.front().normal, SDK::Vec3 {0.0F, 0.0F, 1.0F});
            pose.surface   = hits.front().surface;
            pose.distance  = hits.front().distance;
            pose.onSurface = true;
        }
        else {
            pose.position  = SDK::Vec3 {view.position.x + heading.x * _spec.fallbackDistance, view.position.y + heading.y * _spec.fallbackDistance, view.position.z + heading.z * _spec.fallbackDistance};
            pose.normal    = SDK::Vec3 {0.0F, 0.0F, 1.0F};
            pose.distance  = _spec.fallbackDistance;
            pose.onSurface = false;
        }

        float yaw = _spec.yawDegrees * kDegreesToRad;
        if (_spec.faceAwayFromPlayer) {
            if (SDK::IEntity *player = KCDC::Common::LocalPlayer()) {
                const SDK::Vec3 stood = player->GetWorldPos();
                yaw += YawTowards(pose.position.x - stood.x, pose.position.y - stood.y);
            }
            else {
                yaw += YawTowards(heading.x, heading.y);
            }
        }

        pose.rotation = RotationFor(yaw, pose.normal, _spec.alignToSurface);

        if (_spec.verticalOffset != 0.0F) {
            // Along the normal, so a lift on a slope moves off it, not into it.
            const SDK::Vec3 lift = _spec.alignToSurface ? pose.normal : SDK::Vec3 {0.0F, 0.0F, 1.0F};
            pose.position        = SDK::Vec3 {pose.position.x + lift.x * _spec.verticalOffset, pose.position.y + lift.y * _spec.verticalOffset, pose.position.z + lift.z * _spec.verticalOffset};
        }

        if (!KCDC::Common::IsFinite(pose.position, pose.rotation)) {
            return false;
        }

        _pose    = pose;
        _hasPose = true;
        return true;
    }

    bool PropPlacerService::Evaluate(const PlacementPose &pose) const {
        if (_vetoed) {
            return false;
        }

        const PlacementRules &rules = _spec.rules;

        if (rules.requireSurface && !pose.onSurface) {
            return false;
        }

        if (rules.maxSlopeDegrees < 90.0F && pose.onSurface) {
            // The normal is a unit vector, so its z is the cosine of the lean.
            const float lean = std::acos(std::clamp(pose.normal.z, -1.0F, 1.0F)) * kRadToDegrees;
            if (lean > rules.maxSlopeDegrees) {
                return false;
            }
        }

        if (!rules.surfaces.empty()) {
            if (std::find(rules.surfaces.begin(), rules.surfaces.end(), pose.surface) == rules.surfaces.end()) {
                return false;
            }
        }

        if (rules.minDistanceFromPlayer > 0.0F || rules.maxDistanceFromPlayer > 0.0F) {
            SDK::IEntity *player = KCDC::Common::LocalPlayer();
            if (player == nullptr) {
                return false;
            }
            const SDK::Vec3 stood = player->GetWorldPos();
            const float span      = Length(SDK::Vec3 {pose.position.x - stood.x, pose.position.y - stood.y, pose.position.z - stood.z});
            if (rules.minDistanceFromPlayer > 0.0F && span < rules.minDistanceFromPlayer) {
                return false;
            }
            if (rules.maxDistanceFromPlayer > 0.0F && span > rules.maxDistanceFromPlayer) {
                return false;
            }
        }

        if (rules.clearance > 0.0F) {
            KCDC::Features::World::WorldQueryService *queries = Queries();
            if (queries == nullptr) {
                return false;
            }

            // The entity grid does not see static geometry, so this refuses a
            // spot beside another entity and not one inside a wall.
            KCDC::Features::World::WorldQueryService::ProximityRequest request;
            request.centre        = pose.position;
            request.radius        = rules.clearance;
            request.maxEntities   = 8;
            request.physicalOnly  = false;

            const SDK::EntityId self = KCDC::Common::LocalActorEntityId();

            // One pass per class, or one with no filter for an empty list.
            // The player's own body is skipped.
            const std::vector<std::string> classes = rules.clearanceClasses.empty() ? std::vector<std::string> {std::string {}} : rules.clearanceClasses;
            for (const std::string &name : classes) {
                request.entityClass = name;
                std::vector<KCDC::Features::World::WorldQueryService::NearbyEntity> found;
                queries->EntitiesInRadius(request, found);
                for (const auto &entity : found) {
                    if (entity.entityId != self) {
                        return false;
                    }
                }
            }
        }

        return true;
    }

    bool PropPlacerService::StillAllowed() const {
        if (!KCDC::Core::gApplication || KCDC::Core::gApplication->AreControlsLocked()) {
            return false;
        }

        // A level change builds a new manager, taking the old map with it.
        SDK::IActionMapManager *manager = SDK::IActionMapManager::GetInstance();
        if (manager == nullptr || manager != _inputManager || manager->GetActionMap(kMapName) != _inputMap) {
            return false;
        }

        SDK::IInput *input = SDK::IInput::GetInstance();
        return input != nullptr && input->IsEventPostingEnabled();
    }

    void PropPlacerService::ReportMoveIfDue(bool validityFlipped) {
        if (_sink == nullptr) {
            return;
        }

        const auto now  = std::chrono::steady_clock::now();
        const float span = Length(SDK::Vec3 {_pose.position.x - _lastReported.x, _pose.position.y - _lastReported.y, _pose.position.z - _lastReported.z});

        // Throttled: this is the one callback that could fire every frame,
        // and each one enters V8 from inside the game tick.
        const bool due = validityFlipped || (span >= kMoveReportDistance && (now - _lastReport) >= kMoveReportInterval);
        if (!due) {
            return;
        }

        _lastReported = _pose.position;
        _lastReport   = now;
        _sink->OnPlacementMove(_pose, _valid);
    }

    void PropPlacerService::Update() {
        const std::uint64_t context = _nativeContext.load();
        if (context != _observedContext) {
            _observedContext = context;

            // All of these named a world that has gone; dropped rather than
            // released, and the caller is told the session ended.
            _inputManager = nullptr;
            _inputMap     = nullptr;
            _listening    = false;
            _ghost        = PropGhostService::kNoHandle;
            if (_active) {
                _active               = false;
                IPropPlacerSink *sink = _sink;
                _sink                 = nullptr;
                _hasPose              = false;
                if (sink != nullptr) {
                    sink->OnPlacementEnd();
                }
            }
            return;
        }

        if (!_active) {
            return;
        }

        if (!StillAllowed()) {
            Framework::Logging::GetLogger(kLogger)->info("Placement ended: the mode no longer holds the game's input");
            End();
            return;
        }

        if (_pendingRotation != 0.0F) {
            Rotate(_pendingRotation);
            _pendingRotation = 0.0F;
        }

        PropGhostService *ghosts = Ghosts();
        if (ghosts == nullptr || !ghosts->IsAlive(_ghost)) {
            End();
            return;
        }

        const bool wasValid = _valid;
        if (Aim()) {
            _valid = Evaluate(_pose);
            ghosts->SetPose(_ghost, _pose.position, _pose.rotation, _spec.scale);
            ghosts->SetTint(_ghost, _valid ? _spec.validTint : _spec.invalidTint);
        }

        // Drained after the aim, so a confirm answers the pose the player was
        // looking at on the frame they clicked.
        const Pending pending = _pending;
        _pending              = Pending::None;

        if (pending == Pending::Cancel) {
            IPropPlacerSink *sink = _sink;
            if (sink != nullptr) {
                sink->OnPlacementCancel();
            }
            End();
            return;
        }

        if (pending == Pending::Confirm && _hasPose) {
            IPropPlacerSink *sink = _sink;
            if (sink != nullptr) {
                if (_valid) {
                    sink->OnPlacementConfirm(_pose);
                }
                else {
                    sink->OnPlacementRejected(_pose);
                }
            }

            // A rejected confirm never closes the session.
            if (_valid && !_spec.keepOpen) {
                End();
                return;
            }
        }

        ReportMoveIfDue(wasValid != _valid);
    }

    void PropPlacerService::NativeContextChanged() {
        ++_nativeContext;
    }

    void PropPlacerService::Reset() {
        End();
    }
} // namespace KCDC::Features::Prop
