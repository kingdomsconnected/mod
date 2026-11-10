#include "features/camera/camera_control_service.h"

#include "core/application.h"
#include "core/obfuscation.h"

#include "features/camera/fly_keys.h"
#include "features/camera/noclip_scripting.h"
#include "features/world/world_query_service.h"

#include "common/module_support.h"

#include "sdk/cryengine/action/i_movement_controller.h"
#include "sdk/cryengine/entitysystem/i_entity.h"
#include "sdk/wh/entitymodule/c_human.h"
#include "sdk/wh/game/c_camera_manager.h"

#include "shared/features/world/world_query_rpc.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/network_peer.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <vector>

namespace KCDC::Features::Camera {
    namespace {
        /** Degrees of turn per pixel of mouse travel, as the editor flies. */
        constexpr float kLookSensitivity = 0.12F;

        /** An alt-tab or a level load must not launch the camera across the map. */
        constexpr float kMaxFrameSeconds = 0.1F;

        /** What a measured eye height is believed within; see HoldBody. */
        constexpr float kMaxEyeHeight = 3.0F;

        // How close to the pivot a wall may push the third-person camera, and
        // how far in front of that wall it stops so the near plane does not
        // clip it; and the most a script can ask for.
        constexpr float kMinThirdPersonDistance = 0.3F;
        constexpr float kWallStandoff           = 0.2F;
        constexpr float kMaxThirdPersonDistance = 20.0F;
        constexpr float kMaxThirdPersonHeight   = 5.0F;
        constexpr float kMaxThirdPersonShoulder = 3.0F;
        constexpr float kMinForwardSq           = 1e-6F;

        // How near the player's eyes a shot gliding back is when the game's
        // head hiding returns: the last stretch, so the view never ends up
        // inside a head it can see.
        constexpr float kHeadHideDistance = 0.6F;

        /** How long the orbit waits before asking for the look again after being refused it. */
        constexpr std::int64_t kOrbitRetryMicroseconds = 5000000;

        std::int64_t NowMicroseconds() {
            return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        }

        SDK::Vec3 Lerp(const SDK::Vec3 &from, const SDK::Vec3 &to, float amount) {
            return SDK::Vec3 {from.x + (to.x - from.x) * amount, from.y + (to.y - from.y) * amount, from.z + (to.z - from.z) * amount};
        }

        /** The unit vector along `value`, or `fallback` for one too short to have a direction. */
        SDK::Vec3 Direction(const SDK::Vec3 &value, const SDK::Vec3 &fallback) {
            const float length = std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
            return length > 1e-4F ? SDK::Vec3 {value.x / length, value.y / length, value.z / length} : fallback;
        }

        bool WindowIsFocused() {
            return Core::gGlobals.window != nullptr && GetForegroundWindow() == Core::gGlobals.window;
        }

        /**
         * The entity whose transform actually carries the player.
         *
         * A mounted player is attached to the horse, and moving the child of
         * an attachment only lasts until the parent's next transform update.
         */
        SDK::IEntity *TransformRoot(SDK::IEntity *entity) {
            for (SDK::IEntity *parent = entity->GetParent(); parent != nullptr; parent = entity->GetParent()) {
                entity = parent;
            }
            return entity;
        }
    } // namespace

    bool ParseBodyMode(const std::string &name, BodyMode &mode) {
        if (name == "body") {
            mode = BodyMode::Body;
            return true;
        }
        if (name == "camera") {
            mode = BodyMode::Camera;
            return true;
        }
        return false;
    }

    const char *BodyModeName(BodyMode mode) {
        return mode == BodyMode::Body ? "body" : "camera";
    }

    const char *EndReasonName(EndReason reason) {
        switch (reason) {
        case EndReason::Script: return "script";
        case EndReason::MapEditor: return "mapEditor";
        case EndReason::ViewLost: return "viewLost";
        case EndReason::SessionOver: return "sessionOver";
        }
        return "unknown";
    }

    const char *EnableResultName(EnableResult result) {
        switch (result) {
        case EnableResult::Enabled: return "enabled";
        case EnableResult::AlreadyActive: return "alreadyActive";
        case EnableResult::NoCamera: return "noCamera";
        case EnableResult::CameraBusy: return "cameraBusy";
        }
        return "unknown";
    }

    EnableResult CameraControlService::Enable(const NoClipOptions &options) {
        KCDC_JUNK;
        if (_active || !KCDC_OPAQUE_TRUE) {
            return EnableResult::AlreadyActive;
        }

        // The map editor is the one at this keyboard, and it wins the one camera. So does
        // a script that put the player's view behind them, or that is filming it.
        if (_thirdPerson || _shot || (Core::gApplication != nullptr && Core::gApplication->GetMapEditor().IsOpen())) {
            return EnableResult::CameraBusy;
        }

        if (!_camera.Take()) {
            return EnableResult::NoCamera;
        }

        _options              = options;
        _active               = true;
        _looking              = false;
        _lastTickMicroseconds = 0;

        _camera.SetSpeed(options.speed);
        if (options.fieldOfView > 0.0F) {
            _camera.SetFieldOfView(options.fieldOfView);
        }

        if (_options.body == BodyMode::Body) {
            HoldBody();
        }
        HoldControls(true);

        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("NoClip on: {} body, {} input", BodyModeName(_options.body), _options.builtInInput ? "built-in" : "scripted");
        Scripting::Events::NoClipChanged(true, "script");
        return EnableResult::Enabled;
    }

    void CameraControlService::Disable(bool keepPosition, EndReason reason) {
        KCDC_JUNK;
        if (!_active || !KCDC_OPAQUE_TRUE) {
            return;
        }

        _active  = false;
        _looking = false;
        HoldControls(false);
        if (_bodyEntityId != 0) {
            ReleaseBody(keepPosition);
        }
        _camera.Release();

        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("NoClip off ({})", EndReasonName(reason));
        Scripting::Events::NoClipChanged(false, EndReasonName(reason));
    }

    void CameraControlService::SetBuiltInInputEnabled(bool enabled) {
        if (_options.builtInInput == enabled) {
            return;
        }
        _options.builtInInput = enabled;

        // The cursor drifted while the resource was driving; recentre before
        // counting any of that as travel.
        _looking = false;
    }

    bool CameraControlService::Move(const FlyInput &input, float deltaSeconds) {
        if (!_active || _options.builtInInput || !std::isfinite(deltaSeconds)) {
            return false;
        }

        _camera.Fly(input, std::clamp(deltaSeconds, 0.0F, kMaxFrameSeconds));
        if (_options.body == BodyMode::Body && _bodyEntityId != 0) {
            CarryBody();
        }
        return true;
    }

    void CameraControlService::Update() {
        if (!_active) {
            if (_shot) {
                // A shot films from where it was told to, and the player's
                // look is their own again while it does.
                _orbit.End(/*alignView=*/true);
                UpdateShot();
            }
            else if (_thirdPerson) {
                FollowBehind();
            }
            return;
        }

        // The editor takes the camera later in this same frame, so handing it
        // over now costs nothing.
        if (Core::gApplication != nullptr && Core::gApplication->GetMapEditor().IsOpen()) {
            Disable(/*keepPosition=*/true, EndReason::MapEditor);
            return;
        }

        // The anchor goes with the level. Asked of the entity system rather
        // than of the camera's own flag, which only a call reaching Apply
        // lowers -- and with a resource driving, no tick makes one.
        if (!_camera.IsActive() || KCDC::Common::ResolveEntity(_camera.GetAnchorEntityId()) == nullptr) {
            // Where the camera stopped: the takeoff point may be in a level
            // that no longer exists.
            Disable(/*keepPosition=*/true, EndReason::ViewLost);
            return;
        }

        // The engine hands the view back on its own -- every entity layer
        // that finishes streaming does -- and a resource holding the camera
        // still sends nothing that would ask for it again.
        _camera.Reclaim();

        const std::int64_t now = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        const float delta      = _lastTickMicroseconds == 0 ? 0.0F : std::clamp(static_cast<float>(now - _lastTickMicroseconds) * 1e-6F, 0.0F, kMaxFrameSeconds);
        _lastTickMicroseconds  = now;

        if (_options.builtInInput) {
            FlyInput fly {};
            if (WindowIsFocused()) {
                ReadFlyInput(fly);
                if (!ReadLookDelta(fly.yawDelta, fly.pitchDelta)) {
                    _looking = false;
                }
            }
            else {
                // Alt-tabbing never delivers a key release, and the cursor is
                // somewhere else entirely.
                _looking = false;
            }
            _camera.Fly(fly, delta);
        }

        if (_options.body == BodyMode::Body) {
            // A resource enabling during a level load has no local player
            // yet, so the grab is retried rather than being a one-shot.
            if (_bodyEntityId == 0) {
                HoldBody();
            }
            CarryBody();
        }
    }

    void CameraControlService::Reset() {
        Disable(/*keepPosition=*/false, EndReason::SessionOver);

        // Nothing to align: the body the look belonged to goes with the session.
        _orbit.End(/*alignView=*/false);
        _orbitTickMicroseconds  = 0;
        _orbitRetryMicroseconds = 0;

        // The requests belong to the session that made them, like the view.
        // The shot first, so the third-person camera does not inherit it.
        if (_shot) {
            _thirdPerson = false;
            FinishShot();
        }
        DisableThirdPerson();
        _thirdPersonOptions = {};
    }

    // --- Third person --------------------------------------------------------

    void CameraControlService::EnableThirdPerson(const ThirdPersonOptions &options) {
        _thirdPersonOptions.distance = std::clamp(options.distance, kMinThirdPersonDistance, kMaxThirdPersonDistance);
        _thirdPersonOptions.height   = std::clamp(options.height, -kMaxThirdPersonHeight, kMaxThirdPersonHeight);
        _thirdPersonOptions.shoulder = std::clamp(options.shoulder, -kMaxThirdPersonShoulder, kMaxThirdPersonShoulder);
        _thirdPersonOptions.orbit        = options.orbit;
        _thirdPersonOptions.orbitOptions = options.orbitOptions;
        _thirdPerson                     = true;

        // Asked for again, so a refusal is worth another try now; an orbit
        // already steering only takes the new settings.
        _orbitRetryMicroseconds = 0;
        if (_orbit.IsSteering()) {
            _orbit.SetOptions(options.orbitOptions);
        }
    }

    void CameraControlService::DisableThirdPerson() {
        if (!_thirdPerson) {
            return;
        }
        _thirdPerson = false;

        // The game's own camera takes over looking where this one looked.
        _orbit.End(/*alignView=*/true);
        _orbitTickMicroseconds = 0;

        // A flight owns the camera while it runs, and hands it back itself;
        // so does a shot, and with it the player's head.
        if (_shot) {
            return;
        }
        ShowHead(false);
        if (!_active && _camera.IsActive()) {
            _camera.Release();
        }
    }

    bool CameraControlService::CameraMustYield() const {
        // The map editor wins the one camera, as it does over a flight; the
        // view comes back when it closes.
        if (Core::gApplication != nullptr && Core::gApplication->GetMapEditor().IsOpen()) {
            return true;
        }

        // So does an Apse page -- the map, the inventory, the journal. Each is
        // a 3D scene the game films through its UI camera, which reaches the
        // screen only through the player's own view; with the anchor's view
        // active instead, the page is never drawn and never finishes opening.
        // The game's camera selection runs from the player's update whichever
        // view is drawn, so it is the signal to stand down on, and to come back.
        const auto *cameraManager = SDK::wh::game::C_CameraManager::GetInstance();
        return cameraManager != nullptr && cameraManager->IsUICameraActive();
    }

    bool CameraControlService::HoldView() {
        // A level change takes the anchor with it; a view on an entity that
        // is gone is let go and taken again in the new level.
        if (_camera.IsActive() && KCDC::Common::ResolveEntity(_camera.GetAnchorEntityId()) == nullptr) {
            _camera.Release();
        }
        return _camera.IsActive() || _camera.Take();
    }

    bool CameraControlService::OrbitMaySteer() const {
        auto *app                             = Core::gApplication.get();
        SDK::wh::entitymodule::C_Human *actor = KCDC::Common::LocalHumanActor();
        if (app == nullptr || actor == nullptr) {
            return false;
        }

        // In the saddle the game's own camera films the rider; with a weapon
        // or fists up the fight turns the body from the view, and aiming
        // needs the two to agree; under a camera limit or a rotation lock --
        // a seat, a lean, a scripted action -- the facing is not the keys'.
        return app->GetHorseService().SeatOf(actor->GetEntityId()) == 0 && !app->GetPlayerService().Combat().fistsUp && actor->TakesYawDelta();
    }

    void CameraControlService::SettleOrbit() {
        if (!_thirdPersonOptions.orbit || !OrbitMaySteer()) {
            _orbit.End(/*alignView=*/true);
            _orbitTickMicroseconds = 0;
            return;
        }

        const std::int64_t now = NowMicroseconds();
        if (!_orbit.IsSteering()) {
            SDK::Vec3 eye {};
            SDK::Vec3 forward {};
            if (now < _orbitRetryMicroseconds || !OwnViewPose(eye, forward)) {
                return;
            }
            if (!_orbit.Begin(forward, _thirdPersonOptions.orbitOptions)) {
                _orbitRetryMicroseconds = now + kOrbitRetryMicroseconds;
                return;
            }
            _orbitTickMicroseconds = 0;
        }

        const float delta      = _orbitTickMicroseconds == 0 ? 0.0F : std::clamp(static_cast<float>(now - _orbitTickMicroseconds) * 1e-6F, 0.0F, kMaxFrameSeconds);
        _orbitTickMicroseconds = now;
        _orbit.Update(delta);
    }

    void CameraControlService::FollowBehind() {
        if (CameraMustYield()) {
            if (_camera.IsActive()) {
                _camera.Release();
            }
            ShowHead(false);
            // The page is filmed through the player's own view, which looks
            // where the body does: the look goes back with it.
            _orbit.End(/*alignView=*/true);
            _orbitTickMicroseconds = 0;
            return;
        }

        SettleOrbit();

        SDK::Vec3 position {};
        SDK::Vec3 forward {};
        if (!ThirdPersonPose(position, forward)) {
            return;
        }

        if (!HoldView()) {
            return;
        }

        // Filming the player from behind them, so the head the game hides for
        // its own first person has to be drawn.
        ShowHead(true);
        _camera.SetPose(position, forward);
    }

    bool CameraControlService::OwnViewPose(SDK::Vec3 &eye, SDK::Vec3 &forward) const {
        auto *actor    = KCDC::Common::LocalHumanActor();
        auto *movement = actor != nullptr ? actor->GetMovementController() : nullptr;
        if (movement == nullptr) {
            return false;
        }

        // Where the player is looking, as their own body holds it: the mouse
        // still turns the body, whichever view is drawn. The body keeps the
        // two halves apart, the way the game's own first-person camera reads
        // them: its view rotation is yaw only -- which is all eyeDirection is
        // built from, so it comes out level -- and the pitch is a separate
        // angle beside it.
        SDK::SMovementState state {};
        movement->GetMovementState(state, true);
        const float level = std::sqrt(state.eyeDirection.x * state.eyeDirection.x + state.eyeDirection.y * state.eyeDirection.y);
        const float pitch = actor->GetViewPitch();
        if (!std::isfinite(level) || level * level < kMinForwardSq || !std::isfinite(pitch)) {
            return false;
        }
        if (!std::isfinite(state.eyePosition.x) || !std::isfinite(state.eyePosition.y) || !std::isfinite(state.eyePosition.z)) {
            return false;
        }
        const float up = std::cos(pitch);
        forward        = SDK::Vec3 {state.eyeDirection.x / level * up, state.eyeDirection.y / level * up, std::sin(pitch)};
        eye            = state.eyePosition;
        return true;
    }

    bool CameraControlService::ThirdPersonPose(SDK::Vec3 &position, SDK::Vec3 &forward) const {
        SDK::IEntity *body = KCDC::Common::LocalPlayer();
        SDK::Vec3 eye {};
        if (body == nullptr || !OwnViewPose(eye, forward)) {
            return false;
        }

        // Orbiting, the camera looks where the mouse put it; the body's own
        // view only says where it is facing.
        if (_orbit.IsSteering()) {
            forward = _orbit.GetForward();
        }

        // Right is level with the horizon, as the flight's basis is.
        const float flat      = std::sqrt(forward.x * forward.x + forward.y * forward.y);
        const SDK::Vec3 right = flat > 1e-4F ? SDK::Vec3 {forward.y / flat, -forward.x / flat, 0.0F} : SDK::Vec3 {1.0F, 0.0F, 0.0F};

        const SDK::Vec3 origin = body->GetWorldPos();
        const SDK::Vec3 pivot {origin.x + right.x * _thirdPersonOptions.shoulder, origin.y + right.y * _thirdPersonOptions.shoulder, origin.z + _thirdPersonOptions.height};
        const SDK::Vec3 wanted {pivot.x - forward.x * _thirdPersonOptions.distance, pivot.y - forward.y * _thirdPersonOptions.distance, pivot.z - forward.z * _thirdPersonOptions.distance};

        // Pulled in along the same line when the world is in the way. The
        // cover trace passes bodies, the player's own included.
        float distance = _thirdPersonOptions.distance;
        if (auto *app = Core::gApplication.get()) {
            KCDC::Features::World::WorldQueryService::RayRequest request;
            request.origin            = pivot;
            request.target            = wanted;
            request.mode              = KCDC::Shared::RPC::WorldRayMode::Cover;
            request.ignoreLocalPlayer = true;
            std::vector<KCDC::Features::World::WorldQueryService::RayHit> hits;
            if (app->GetWorldQueryService().Raycast(request, hits) && !hits.empty()) {
                distance = std::max(hits.front().distance - kWallStandoff, kMinThirdPersonDistance);
            }
        }

        position = SDK::Vec3 {pivot.x - forward.x * distance, pivot.y - forward.y * distance, pivot.z - forward.z * distance};
        return true;
    }

    // --- Shots ---------------------------------------------------------------

    void CameraControlService::Install() {
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr) {
            return;
        }

        net->RegisterRPC<KCDC::Shared::RPC::CameraShot>([](const KCDC::Shared::RPC::CameraShot &shot, MafiaNet::Packet *) {
            if (KCDC::Core::gApplication != nullptr) {
                KCDC::Core::gApplication->GetCameraControl().Receive(shot);
            }
        });
    }

    void CameraControlService::Receive(const KCDC::Shared::RPC::CameraShot &shot) {
        if (!shot.IsSane()) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("Dropped a malformed camera shot");
            return;
        }
        if (!shot.active) {
            ClearShot(shot.durationMs);
            return;
        }

        ShotOptions options;
        options.position    = KCDC::Common::ToNative(shot.position);
        options.lookAt      = KCDC::Common::ToNative(shot.lookAt);
        options.durationMs  = shot.durationMs;
        options.fieldOfView = shot.fieldOfView;
        if (!SetShot(options)) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("A camera shot from the server was refused: the camera is flying");
        }
    }

    bool CameraControlService::SetShot(const ShotOptions &options) {
        const SDK::Vec3 &to = options.position;
        if (_active || !std::isfinite(to.x) || !std::isfinite(to.y) || !std::isfinite(to.z) || !std::isfinite(options.fieldOfView)) {
            return false;
        }
        const SDK::Vec3 along {options.lookAt.x - to.x, options.lookAt.y - to.y, options.lookAt.z - to.z};
        if (!std::isfinite(along.x) || !std::isfinite(along.y) || !std::isfinite(along.z) || along.x * along.x + along.y * along.y + along.z * along.z < 1e-4F) {
            return false;
        }

        // The glide starts where the view is now: the shot or the third-person
        // camera already holding it, or the game's own, which Take seeds the
        // anchor from. Without a level to take it in, the shot waits and cuts.
        const bool held = !CameraMustYield() && HoldView();

        _shot                   = true;
        _shotReturning          = false;
        _shotPosition           = to;
        _shotForward            = Direction(along, SDK::Vec3 {0.0F, 1.0F, 0.0F});
        _shotFieldOfView        = options.fieldOfView > 0.0F ? std::clamp(options.fieldOfView, KCDC::Shared::RPC::CameraShot::kMinFieldOfView, KCDC::Shared::RPC::CameraShot::kMaxFieldOfView) : 0.0F;
        _glideSeconds           = held ? static_cast<float>(std::min(options.durationMs, KCDC::Shared::RPC::CameraShot::kMaxDurationMs)) * 1e-3F : 0.0F;
        _glideStartMicroseconds = NowMicroseconds();
        if (held) {
            _glideFromPosition    = _camera.GetPose().position;
            _glideFromForward     = PoseForward(_camera.GetPose());
            _glideFromFieldOfView = _camera.GetRequestedFieldOfView();
        }

        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Camera shot at {:.1f} {:.1f} {:.1f}, {} ms", to.x, to.y, to.z, options.durationMs);
        UpdateShot();
        return true;
    }

    void CameraControlService::ClearShot(std::uint32_t durationMs) {
        if (!_shot || _shotReturning) {
            if (_shot && durationMs == 0) {
                FinishShot();
            }
            return;
        }

        if (durationMs == 0 || !_camera.IsActive() || CameraMustYield()) {
            FinishShot();
            return;
        }

        _shotReturning          = true;
        _glideFromPosition      = _camera.GetPose().position;
        _glideFromForward       = PoseForward(_camera.GetPose());
        _glideFromFieldOfView   = _camera.GetRequestedFieldOfView();
        _glideSeconds           = static_cast<float>(std::min(durationMs, KCDC::Shared::RPC::CameraShot::kMaxDurationMs)) * 1e-3F;
        _glideStartMicroseconds = NowMicroseconds();
        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Camera shot gliding back over {} ms", durationMs);
    }

    void CameraControlService::FinishShot() {
        _shot          = false;
        _shotReturning = false;

        // The third-person camera picks the view up from here on its next
        // tick, and goes on filming the player: it wants the lens back, and
        // the head kept. With nothing else asking for the view the game has it
        // back, and its own head hiding with it.
        if (_thirdPerson) {
            if (_ownFieldOfView > 0.0F) {
                _camera.SetFieldOfView(_ownFieldOfView);
            }
            ShowHead(true);
        }
        else {
            ShowHead(false);
            if (_camera.IsActive()) {
                _camera.Release();
            }
        }

        // Read again for the next shot: by then the player may have changed it.
        _ownFieldOfView = 0.0F;
        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Camera shot over");
    }

    void CameraControlService::UpdateShot() {
        if (CameraMustYield()) {
            if (_camera.IsActive()) {
                _camera.Release();
            }
            // The game is drawing through its own first-person camera again, so
            // its head hiding has to come back with it.
            ShowHead(false);
            // Nothing to glide back from or into while the view is elsewhere.
            if (_shotReturning) {
                FinishShot();
            }
            return;
        }
        if (!HoldView()) {
            // Nothing of the shot is on screen yet -- no level to take the view
            // in -- and the player is still looking out of their own eyes.
            ShowHead(false);
            return;
        }

        // The lens to give back, read the first frame the view is ours: before
        // Take has seeded it off the game's camera there is nothing to read.
        if (_ownFieldOfView <= 0.0F) {
            _ownFieldOfView = _camera.GetRequestedFieldOfView();
        }

        // Where the glide is going. On the way back that is wherever the player
        // is looking from this frame -- or behind them, for a third-person
        // camera that will take over.
        SDK::Vec3 toPosition = _shotPosition;
        SDK::Vec3 toForward  = _shotForward;
        float toFieldOfView  = _shotFieldOfView > 0.0F ? _shotFieldOfView : _ownFieldOfView;
        if (_shotReturning) {
            const bool found = _thirdPerson ? ThirdPersonPose(toPosition, toForward) : OwnViewPose(toPosition, toForward);
            if (!found) {
                FinishShot();
                return;
            }
            toFieldOfView = _ownFieldOfView;
        }

        const float elapsed = static_cast<float>(NowMicroseconds() - _glideStartMicroseconds) * 1e-6F;
        const float linear  = _glideSeconds > 0.0F ? std::clamp(elapsed / _glideSeconds, 0.0F, 1.0F) : 1.0F;
        if (linear >= 1.0F && _shotReturning) {
            FinishShot();
            return;
        }

        // Eased in and out, so neither end of the glide is a jolt.
        const float eased        = linear * linear * (3.0F - 2.0F * linear);
        const SDK::Vec3 position = linear >= 1.0F ? toPosition : Lerp(_glideFromPosition, toPosition, eased);
        const SDK::Vec3 forward  = linear >= 1.0F ? toForward : Direction(Lerp(_glideFromForward, toForward, eased), toForward);
        const float fieldOfView  = linear >= 1.0F ? toFieldOfView : _glideFromFieldOfView + (toFieldOfView - _glideFromFieldOfView) * eased;

        // Drawn while the camera films the player from outside their eyes. On
        // the way into them the game's own hiding comes back for the last
        // stretch, before the view is close enough to be inside the head --
        // and once hidden it stays hidden, so a player walking off again does
        // not flicker it. A glide back to third person never goes inside them.
        if (!_shotReturning || _thirdPerson) {
            ShowHead(true);
        }
        else {
            const SDK::Vec3 gap {position.x - toPosition.x, position.y - toPosition.y, position.z - toPosition.z};
            if (gap.x * gap.x + gap.y * gap.y + gap.z * gap.z < kHeadHideDistance * kHeadHideDistance) {
                ShowHead(false);
            }
        }

        if (std::abs(fieldOfView - _camera.GetRequestedFieldOfView()) > 1e-3F) {
            _camera.SetFieldOfView(fieldOfView);
        }
        _camera.SetPose(position, forward);
    }

    void CameraControlService::ShowHead(bool shown) {
        if (_headShown == shown) {
            return;
        }

        auto *cameraManager = SDK::wh::game::C_CameraManager::GetInstance();
        if (cameraManager == nullptr) {
            // No game to show it in. What stays recorded is "not shown", so
            // the next shot asks again.
            _headShown = false;
            return;
        }

        // Whatever the game had set is what it gets back; the manager's own
        // update reapplies the appearance on its next frame either way.
        if (shown) {
            _headWasSuppressed = cameraManager->IsFirstPersonViewSuppressed();
        }
        const std::int32_t suppressed = shown || _headWasSuppressed ? 1 : 0;
        cameraManager->SetFirstPersonViewSuppressed(&suppressed);
        _headShown = shown;
    }

    void CameraControlService::ReadFlyInput(FlyInput &fly) const {
        const auto *application = Core::gApplication.get();
        const auto input        = application != nullptr ? application->GetInput() : nullptr;
        if (input == nullptr) {
            return;
        }

        // The game's own photo-mode keys, so a rebind in the controls menu
        // and a non-QWERTY layout both carry over.
        FlyKeyBindings::Get().Read(*input, fly);
    }

    bool CameraControlService::ReadLookDelta(float &yawDelta, float &pitchDelta) {
        yawDelta   = 0.0F;
        pitchDelta = 0.0F;

        const HWND window = Core::gGlobals.window;
        if (window == nullptr) {
            return false;
        }

        RECT client {};
        if (GetClientRect(window, &client) == FALSE) {
            return false;
        }

        POINT centre {(client.right - client.left) / 2, (client.bottom - client.top) / 2};
        if (ClientToScreen(window, &centre) == FALSE) {
            return false;
        }

        POINT cursor {};
        if (GetCursorPos(&cursor) == FALSE) {
            return false;
        }

        // Both axes are negated: yaw turns about +Z by the right-hand rule,
        // so a rising yaw swings left, and the cursor's Y counts down the
        // screen while pitch counts up.
        if (_looking) {
            yawDelta   = -static_cast<float>(cursor.x - centre.x) * kLookSensitivity;
            pitchDelta = -static_cast<float>(cursor.y - centre.y) * kLookSensitivity;
        }

        SetCursorPos(centre.x, centre.y);
        _looking = true;
        return true;
    }

    SDK::Vec3 CameraControlService::BodyPositionFor(const CameraPose &pose) const {
        return SDK::Vec3 {pose.position.x, pose.position.y, pose.position.z - _bodyEyeHeight};
    }

    void CameraControlService::HoldBody() {
        SDK::IEntity *player = KCDC::Common::LocalPlayer();
        if (player == nullptr) {
            return;
        }

        SDK::IEntity *body     = TransformRoot(player);
        _bodyEntityId          = body->GetId();
        _bodyOrigin            = body->GetWorldPos();
        _bodyOriginRotation    = body->GetRotation();
        _bodyPhysicsWasEnabled = body->IsPhysicsEnabled();

        // Exact at takeoff, where the camera is still on the player's eyes. A
        // body grabbed later is measured against a camera that has since flown
        // off, so the answer is clamped rather than trusted.
        _bodyEyeHeight = std::clamp(_camera.GetPose().position.z - _bodyOrigin.z, 0.0F, kMaxEyeHeight);

        // pef_disabled rather than a collider filter: nothing is stepped, so
        // nothing sweeps, nothing falls, and nothing pushes the body back out
        // of the wall it is inside.
        body->EnablePhysics(false);
    }

    void CameraControlService::CarryBody() {
        SDK::IEntity *body = _bodyEntityId != 0 ? KCDC::Common::ResolveEntity(_bodyEntityId) : nullptr;
        if (body == nullptr) {
            // Gone with the level; Update's own check ends the flight next tick.
            _bodyEntityId = 0;
            return;
        }

        const CameraPose &pose = _camera.GetPose();

        // Forced, so the move is not dropped against a transform the engine
        // believes it already owns. No physics bounds: there is no physics.
        body->SetPos(BodyPositionFor(pose), SDK::ENTITY_XFORM_POS, /*recalcPhysicsBounds=*/false, /*force=*/true);

        // Yaw alone: a body pitched with the camera lies on its face when the
        // flier looks down.
        CameraPose flat   = pose;
        flat.pitchDegrees = 0.0F;
        body->SetRotation(PoseRotation(flat), SDK::ENTITY_XFORM_ROT);
    }

    void CameraControlService::ReleaseBody(bool keepPosition) {
        SDK::IEntity *body = KCDC::Common::ResolveEntity(_bodyEntityId);
        _bodyEntityId      = 0;
        if (body == nullptr) {
            return;
        }

        const SDK::Vec3 landing = keepPosition ? BodyPositionFor(_camera.GetPose()) : _bodyOrigin;

        if (_bodyPhysicsWasEnabled) {
            body->EnablePhysics(true);
        }

        // recalcPhysicsBounds, so the living entity follows the transform it
        // was not simulating rather than snapping back to its stale proxy.
        body->SetPos(landing, SDK::ENTITY_XFORM_POS, /*recalcPhysicsBounds=*/true, /*force=*/true);
        if (!keepPosition) {
            body->SetRotation(_bodyOriginRotation, SDK::ENTITY_XFORM_ROT);
        }
    }

    void CameraControlService::HoldControls(bool hold) {
        if (_controlsHeld == hold) {
            return;
        }
        _controlsHeld = hold;

        if (auto *application = Core::gApplication.get()) {
            // No cursor: the flight warps it to the middle of the window every
            // frame, so a pointer drawn over that is a dot pinned to the centre.
            application->LockControls(hold, /*withCursor=*/false);
        }
    }
} // namespace KCDC::Features::Camera
