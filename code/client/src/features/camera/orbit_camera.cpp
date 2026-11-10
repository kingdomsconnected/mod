#include "features/camera/orbit_camera.h"

#include "core/application.h"

#include "features/player/native_player_module.h"

#include "sdk/cryengine/action/c_animated_character.h"
#include "sdk/cryengine/action/i_movement_controller.h"
#include "sdk/cryengine/action/i_procedural_context.h"
#include "sdk/cryengine/system/i_system.h"
#include "sdk/wh/entitymodule/c_actor_rotation.h"
#include "sdk/wh/entitymodule/c_player.h"
#include "sdk/wh/entitymodule/c_player_input.h"

#include "common/module_support.h"

#include <logging/logger.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace KCDC::Features::Camera {
    namespace {
        constexpr const char *kLogger = "Camera";

        constexpr char kMapName[]    = "kcdc_orbit_camera";
        constexpr char kFilterName[] = "kcdc_orbit_camera_look";

        /** Any value does: nothing else holds a reason on this filter. */
        constexpr std::uint8_t kFilterReason = 0;

        /**
         * Not exclusive: the player keeps walking, jumping and using things.
         * `keyboard` carries the mouse in CryEngine's vocabulary, and
         * `maxis_x` / `maxis_y` are its raw deltas, as the emote wheel reads
         * them. The pad's look stick reports where it stands instead.
         */
        constexpr char kMapXml[] = "<actionmap name=\"kcdc_orbit_camera\" priority=\"gameplay\" exclusivity=\"0\">"
                                   "<action name=\"kcdc_orbit_x\" onPress=\"0\" onRelease=\"0\" onHold=\"0\" keyboard=\"maxis_x\"/>"
                                   "<action name=\"kcdc_orbit_y\" onPress=\"0\" onRelease=\"0\" onHold=\"0\" keyboard=\"maxis_y\"/>"
                                   "<action name=\"kcdc_orbit_stick_x\" onPress=\"0\" onRelease=\"0\" onHold=\"0\" xboxpad=\"xi_thumbrx\"/>"
                                   "<action name=\"kcdc_orbit_stick_y\" onPress=\"0\" onRelease=\"0\" onHold=\"0\" xboxpad=\"xi_thumbry\"/>"
                                   "</actionmap>";

        /** What turns the view, and with it the body, on the mouse and on the pad. */
        constexpr char kFilterXml[] = "<actionfilter name=\"kcdc_orbit_camera_look\" type=\"actionFail\">"
                                      "<action name=\"rotateyaw\"/>"
                                      "<action name=\"rotatepitch\"/>"
                                      "<action name=\"mouse_looking_x\"/>"
                                      "<action name=\"mouse_looking_y\"/>"
                                      "<action name=\"xi_rotateyaw\"/>"
                                      "<action name=\"xi_rotatepitch\"/>"
                                      "</actionfilter>";

        constexpr float kPi        = 3.14159265358979323846F;
        constexpr float kDegToRad  = kPi / 180.0F;

        /** Degrees of turn per unit of mouse travel at sensitivity 1, as the free camera flies. */
        constexpr float kMouseDegrees = 0.12F;

        /** Degrees a second a look stick held all the way over turns the camera. */
        constexpr float kStickDegreesPerSecond = 140.0F;
        constexpr float kStickDeadZone         = 0.18F;

        // Up is bounded short of where the camera would go under the ground
        // the body stands on; down short of straight overhead.
        constexpr float kMaxPitchUp   = 55.0F * kDegToRad;
        constexpr float kMaxPitchDown = 75.0F * kDegToRad;

        constexpr float kMinSensitivity = 0.05F;
        constexpr float kMaxSensitivity = 10.0F;
        constexpr float kMinTurnRate    = 60.0F;
        constexpr float kMaxTurnRate    = 3600.0F;

        /** A move axis below this is a key not held. */
        constexpr float kMoveDeadZone = 0.1F;

        // What a neck does. Past the far bound the camera is in front of the
        // body looking back at it, and the head eases to straight ahead
        // rather than flipping from one shoulder to the other.
        constexpr float kHeadYawLimit   = 70.0F * kDegToRad;
        constexpr float kHeadYawFarEdge = 110.0F * kDegToRad;
        constexpr float kHeadPitchLimit = 35.0F * kDegToRad;
        constexpr float kHeadLookDistance = 10.0F;
        constexpr float kHeadHeight       = 1.6F;

        float WrapAngle(float radians) {
            radians = std::fmod(radians + kPi, 2.0F * kPi);
            if (radians < 0.0F) {
                radians += 2.0F * kPi;
            }
            return radians - kPi;
        }

        SDK::Vec3 ForwardOf(float yaw, float pitch) {
            const float level = std::cos(pitch);
            return SDK::Vec3 {-std::sin(yaw) * level, std::cos(yaw) * level, std::sin(pitch)};
        }

        /** The rotation that faces `yaw` and looks up by `pitch`: about +Z, then about the body's own X. */
        SDK::Quat ViewRotation(float yaw, float pitch) {
            const float sinYaw   = std::sin(yaw * 0.5F);
            const float cosYaw   = std::cos(yaw * 0.5F);
            const float sinPitch = std::sin(pitch * 0.5F);
            const float cosPitch = std::cos(pitch * 0.5F);
            return SDK::Quat {cosYaw * sinPitch, sinYaw * sinPitch, sinYaw * cosPitch, cosYaw * cosPitch};
        }

        /** The body's facing, as its rotation keeps it; `fallback` for a body with no rotation state. */
        float BodyYaw(const SDK::wh::entitymodule::C_Human *actor, float fallback) {
            const SDK::wh::entitymodule::C_ActorRotation *rotation = actor->GetRotation();
            const float yaw                                        = rotation != nullptr ? rotation->GetAngles().z : fallback;
            return std::isfinite(yaw) ? yaw : fallback;
        }

        SDK::wh::entitymodule::C_PlayerInput *LocalInput() {
            auto *player = SDK::wh::entitymodule::C_Player::GetInstance();
            return player != nullptr ? player->GetPlayerInput() : nullptr;
        }
    } // namespace

    bool OrbitCamera::Begin(const SDK::Vec3 &forward, const OrbitOptions &options) {
        if (_steering) {
            SetOptions(options);
            return true;
        }
        if (!AcquireInput()) {
            return false;
        }

        SetOptions(options);
        const float level = std::sqrt(forward.x * forward.x + forward.y * forward.y);
        _yaw              = level > 1e-4F ? std::atan2(-forward.x, forward.y) : 0.0F;
        _pitch            = std::clamp(std::atan2(forward.z, std::max(level, 1e-4F)), -kMaxPitchDown, kMaxPitchUp);
        _headLook         = ForwardOf(_yaw, 0.0F);
        _pendingX         = 0.0F;
        _pendingY         = 0.0F;
        _stickX           = 0.0F;
        _stickY           = 0.0F;
        _moveOverridden   = false;
        _steering         = true;

        // The view's pitch aims the upper body, and nothing will move it
        // while the look actions are held back: left where the player was
        // looking, the body would stand bent toward the ground or the sky.
        if (SDK::wh::entitymodule::C_Human *actor = KCDC::Common::LocalHumanActor(); actor != nullptr && actor->TakesYawDelta()) {
            actor->SetViewRotation(ViewRotation(BodyYaw(actor, _yaw), 0.0F));
        }
        Framework::Logging::GetLogger(kLogger)->debug("Orbit look on");
        return true;
    }

    void OrbitCamera::End(bool alignView) {
        if (!_steering) {
            return;
        }
        _steering = false;
        ReleaseInput();

        SDK::wh::entitymodule::C_Human *actor = KCDC::Common::LocalHumanActor();

        // The keys go back to meaning what they meant: the move vector is
        // put back as the handlers would have left it.
        if (_moveOverridden) {
            if (SDK::wh::entitymodule::C_PlayerInput *input = LocalInput()) {
                const SDK::Vec2 &raw = input->GetRawMovementInput();
                input->OverrideMoveVector(SDK::Vec3 {raw.x, raw.y, 0.0F});
            }
            _moveOverridden = false;
        }

        if (actor != nullptr) {
            HoldHead(actor, false);
            if (alignView && actor->TakesYawDelta()) {
                actor->SetViewRotation(ViewRotation(_yaw, _pitch));
            }
        }
        _headHeld     = false;
        _headEntityId = 0;
        Framework::Logging::GetLogger(kLogger)->debug("Orbit look off");
    }

    void OrbitCamera::SetOptions(const OrbitOptions &options) {
        _options.sensitivity = std::isfinite(options.sensitivity) ? std::clamp(options.sensitivity, kMinSensitivity, kMaxSensitivity) : 1.0F;
        _options.turnRate    = std::isfinite(options.turnRate) ? std::clamp(options.turnRate, kMinTurnRate, kMaxTurnRate) : 540.0F;
        _options.headLook    = options.headLook;
    }

    SDK::Vec3 OrbitCamera::GetForward() const {
        return ForwardOf(_yaw, _pitch);
    }

    void OrbitCamera::Update(float deltaSeconds) {
        if (!_steering) {
            return;
        }

        // A level change replaces the action map manager and frees the old
        // one with everything this class registered on it.
        if (SDK::IActionMapManager::GetInstance() != _inputManager) {
            _inputManager = nullptr;
            _inputMap     = nullptr;
            _filter       = nullptr;
            _listening    = false;
            if (!AcquireInput()) {
                return;
            }
        }

        const float mouseRadians = kMouseDegrees * kDegToRad * _options.sensitivity;
        float yawDelta           = -_pendingX * mouseRadians;
        float pitchDelta         = -_pendingY * mouseRadians;
        _pendingX                = 0.0F;
        _pendingY                = 0.0F;

        const float stickRadians = kStickDegreesPerSecond * kDegToRad * _options.sensitivity * deltaSeconds;
        if (std::fabs(_stickX) > kStickDeadZone) {
            yawDelta -= _stickX * stickRadians;
        }
        if (std::fabs(_stickY) > kStickDeadZone) {
            pitchDelta += _stickY * stickRadians;
        }

        if (std::isfinite(yawDelta) && std::isfinite(pitchDelta)) {
            _yaw   = WrapAngle(_yaw + yawDelta);
            _pitch = std::clamp(_pitch + pitchDelta, -kMaxPitchDown, kMaxPitchUp);
        }

        SDK::wh::entitymodule::C_Human *actor = KCDC::Common::LocalHumanActor();
        if (actor == nullptr) {
            return;
        }

        SteerBody(actor, deltaSeconds);

        // Bounded around where the body faces now, after this frame's turn.
        const float offset = WrapAngle(_yaw - BodyYaw(actor, _yaw));
        const float spread = std::fabs(offset);
        float headYaw      = std::clamp(offset, -kHeadYawLimit, kHeadYawLimit);
        if (spread > kHeadYawFarEdge) {
            headYaw *= std::clamp((kPi - spread) / (kPi - kHeadYawFarEdge), 0.0F, 1.0F);
        }
        _headLook = ForwardOf(BodyYaw(actor, _yaw) + headYaw, std::clamp(_pitch, -kHeadPitchLimit, kHeadPitchLimit));
        HoldHead(actor, _options.headLook);
    }

    void OrbitCamera::SteerBody(SDK::wh::entitymodule::C_Human *actor, float deltaSeconds) {
        SDK::wh::entitymodule::C_PlayerInput *input = LocalInput();
        if (input == nullptr) {
            return;
        }

        const SDK::Vec2 raw   = input->GetRawMovementInput();
        const float magnitude = std::sqrt(raw.x * raw.x + raw.y * raw.y);
        if (!std::isfinite(magnitude) || magnitude < kMoveDeadZone) {
            // The handlers clear one axis per released key, and the axis this
            // class wrote is not one of theirs: without this the body would
            // keep walking forward after the last key came up.
            if (_moveOverridden) {
                input->OverrideMoveVector(SDK::Vec3 {0.0F, 0.0F, 0.0F});
                _moveOverridden = false;
            }
            return;
        }

        // The keys name a direction around the camera; the body turns to it
        // and walks forward. Right is a falling yaw.
        const float wanted  = WrapAngle(_yaw + std::atan2(-raw.x, raw.y));
        const float current = BodyYaw(actor, wanted);
        const float step    = _options.turnRate * kDegToRad * std::clamp(deltaSeconds, 0.0F, 0.1F);
        const float turned  = WrapAngle(current + std::clamp(WrapAngle(wanted - current), -step, step));

        // Under a camera limit or a rotation lock the body's facing is
        // somebody else's for now -- a seat, a lean, a scripted action -- and
        // the keys keep their own meaning.
        if (!actor->TakesYawDelta()) {
            if (_moveOverridden) {
                input->OverrideMoveVector(SDK::Vec3 {raw.x, raw.y, 0.0F});
                _moveOverridden = false;
            }
            return;
        }

        // Level: the view's pitch aims the upper body, and the camera's is
        // the head's business here.
        actor->SetViewRotation(ViewRotation(turned, 0.0F));
        input->OverrideMoveVector(SDK::Vec3 {0.0F, std::min(magnitude, 1.0F), 0.0F});
        _moveOverridden = true;
    }

    void OrbitCamera::HoldHead(SDK::wh::entitymodule::C_Human *actor, bool hold) {
        SDK::IMovementController *movement = actor->GetMovementController();
        const SDK::EntityId entityId       = actor->GetEntityId();
        if (movement == nullptr || (!hold && !_headHeld)) {
            return;
        }

        SDK::CMovementRequest request {};
        if (hold) {
            SDK::SMovementState state {};
            movement->GetMovementState(state, false);
            const SDK::Vec3 from = std::isfinite(state.eyePosition.x) && std::isfinite(state.eyePosition.y) && std::isfinite(state.eyePosition.z) ? state.eyePosition : SDK::Vec3 {state.pos.x, state.pos.y, state.pos.z + kHeadHeight};
            request.SetLookTarget(SDK::Vec3 {from.x + _headLook.x * kHeadLookDistance, from.y + _headLook.y * kHeadLookDistance, from.z + _headLook.z * kHeadLookDistance});
            // Never the lower body: turning that is the move keys' job.
            request.SetLookStyle(SDK::CMovementRequest::LOOKSTYLE_HARD_NOLOWER);
        }
        else {
            request.ClearLookTarget();
        }
        movement->RequestMovement(request);

        // The look context turns the head only while a looking clip asks for
        // it too, and standing has none: it is told one does as it updates,
        // and the request is put down once when the head is let go.
        KCDC::Features::Player::NativePlayerModule::SetLookHeld(entityId, hold);
        if (!hold) {
            SDK::CAnimatedCharacter *animated  = actor->GetAnimatedCharacterRuntime();
            SDK::IActionController *controller = animated != nullptr ? animated->GetActionController() : nullptr;
            if (auto *look = controller != nullptr ? static_cast<SDK::CProceduralContextLook *>(controller->FindProceduralContext("ProceduralContextLook")) : nullptr) {
                look->SetClipRequestsLooking(false);
            }
        }
        _headHeld     = hold;
        _headEntityId = hold ? entityId : 0;
    }

    bool OrbitCamera::AcquireInput() {
        SDK::ISystem *system            = SDK::ISystem::GetInstance();
        SDK::IActionMapManager *manager = SDK::IActionMapManager::GetInstance();
        if (system == nullptr || manager == nullptr || manager->GetActionMap(kMapName) != nullptr) {
            return false;
        }

        SDK::XmlNodeRef node = system->LoadXmlFromBuffer(kMapXml, sizeof(kMapXml) - 1, false, false);
        SDK::IActionMap *map = node.IsValid() ? manager->CreateActionMap(kMapName) : nullptr;
        if (map == nullptr) {
            return false;
        }
        if (!map->LoadFromXML(node) || !manager->AddExtraActionListener(this, kMapName)) {
            manager->RemoveActionMap(kMapName);
            return false;
        }

        _inputManager = manager;
        _inputMap     = map;
        _listening    = true;
        manager->EnableActionMap(kMapName, true);

        _filter = manager->FindOrCreateActionFilter(kFilterName, kFilterXml, sizeof(kFilterXml) - 1);
        if (_filter != nullptr) {
            _filter->Enable(kFilterReason);
        }
        if (_filter == nullptr || !manager->IsFilterEnabled(kFilterName)) {
            // Without it the mouse turns the body and the camera both, which
            // is worse than no orbit at all.
            Framework::Logging::GetLogger(kLogger)->warn("The orbit look could not hold the game's look actions back; staying with the plain third-person camera");
            ReleaseInput();
            return false;
        }
        return true;
    }

    void OrbitCamera::ReleaseInput() {
        SDK::IActionMapManager *manager = SDK::IActionMapManager::GetInstance();

        // Only while it is still the manager this class took: a level change
        // replaces it, and the old one is freed.
        if (manager != nullptr && manager == _inputManager) {
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
        _filter       = nullptr;
        _listening    = false;
        _pendingX     = 0.0F;
        _pendingY     = 0.0F;
        _stickX       = 0.0F;
        _stickY       = 0.0F;
    }

    /**
     * Runs inside the engine's input dispatch, so it only accumulates. Where
     * the camera has got to is settled on the service's own tick.
     */
    void OrbitCamera::OnAction(const SDK::CCryName &action, int, float value) {
        if (!_steering || !std::isfinite(value)) {
            return;
        }
        const char *name = action.c_str();
        if (std::strcmp(name, "kcdc_orbit_x") == 0) {
            _pendingX += value;
        }
        else if (std::strcmp(name, "kcdc_orbit_y") == 0) {
            _pendingY += value;
        }
        else if (std::strcmp(name, "kcdc_orbit_stick_x") == 0) {
            _stickX = std::clamp(value, -1.0F, 1.0F);
        }
        else if (std::strcmp(name, "kcdc_orbit_stick_y") == 0) {
            _stickY = std::clamp(value, -1.0F, 1.0F);
        }
    }
} // namespace KCDC::Features::Camera
