#include "prop_editor_map_travel.h"
#include "core/application.h"
#include "common/module_support.h"
#include "common/vector_math.h"
#include "sdk/cryengine/3dengine/i_3d_engine.h"
#include "sdk/cryengine/action/i_game_framework.h"
#include "sdk/cryengine/entitysystem/i_entity.h"
#include "sdk/cryengine/physics/c_physical_world.h"
#include "sdk/wh/c_wh_global_environment.h"
#include "sdk/wh/guimodule/c_ui_map.h"
#include "sdk/wh/guimodule/c_ui_state_blockers.h"
#include <cstring>
#include <utility>
#include <utils/safe_win32.h>

namespace KCDC::Features::Prop {
    namespace {
        SDK::wh::framework::I_UIApse *Apse() {
            auto *environment = SDK::wh::C_GlobalEnvironment::GetInstance();
            return environment ? environment->GetUIApse() : nullptr;
        }
        bool ApseHeld() {
            auto *blockers = SDK::wh::guimodule::C_UIStateBlockers::GetInstance();
            return blockers && blockers->IsHeld(SDK::wh::guimodule::E_UIState::Apse);
        }
    } // namespace

    bool EditorMapTravel::Begin(KCDC::Features::Camera::AnchorCamera &camera, std::string &error) {
        auto *app    = Core::gApplication.get();
        auto *player = KCDC::Common::LocalPlayer();
        if (Active() || !app || !app->IsOfflineBuilderReady() || !camera.IsActive() || !player || player->GetParent() || app->IsNativeMenuOpen()) {
            error = "Open the offline editor with an unmounted player and no native menu before travelling";
            return false;
        }
        _savedCamera = camera.GetPose();
        _savedFov    = camera.GetRequestedFieldOfView();
        _player      = player->GetId();
        _selected = _ownsMap = _showRequested = _mouseReady = false;
        _groundFrames                                       = 0;
        _placement                                          = 0;
        ClearPointer();
        _started = std::chrono::steady_clock::now();
        ++_sequence;
        _message.clear();
        return true;
    }

    bool EditorMapTravel::Open(KCDC::Features::Camera::AnchorCamera &camera, std::string &error) {
        if (!Begin(camera, error)) {
            return false;
        }
        camera.Release();
        _phase  = Phase::Opening;
        _status = "opening";
        return true;
    }

    bool EditorMapTravel::Travel(KCDC::Features::Camera::AnchorCamera &camera, const SDK::Vec3 &position, std::string &error) {
        auto *engine = SDK::I3DEngine::GetInstance();
        if (!engine || !KCDC::Common::IsFinite(position) || position.x < 0 || position.y < 0 || position.x >= engine->GetTerrainSize() || position.y >= engine->GetTerrainSize()) {
            error = "Destination must be inside the loaded level's terrain";
            return false;
        }
        const float height = engine->GetTerrainElevation(position.x, position.y);
        if (!std::isfinite(height)) {
            error = "Terrain height is unavailable";
            return false;
        }
        if (_phase != Phase::Selecting && !Begin(camera, error)) {
            return false;
        }
        _destination = {position.x, position.y, height};
        _selected    = true;
        _phase       = Phase::Closing;
        _status      = "closing";
        _started     = std::chrono::steady_clock::now();
        return true;
    }

    void EditorMapTravel::Finish(const char *status, const char *message) {
        if (_phase == Phase::Returning) {
            return;
        }
        _status  = status;
        _message = message;
        _phase   = Phase::Returning;
        _started = std::chrono::steady_clock::now();
        ClearPointer();
    }

    bool EditorMapTravel::Cancel() {
        // Placement is drained before this controller's next update. Once
        // sent, it cannot be reported as a cancelled, unmoved player.
        if (_phase == Phase::Placing) {
            return false;
        }
        if (Active()) {
            Finish("cancelled", "Map travel cancelled");
        }
        return true;
    }

    void EditorMapTravel::Reset() {
        StopListening();
        _phase  = Phase::Idle;
        _status = "idle";
        _message.clear();
        _selected = _ownsMap = _showRequested = _mouseReady = false;
        _player                                             = 0;
        _placement                                          = 0;
        ClearPointer();
    }

    void EditorMapTravel::Pointer(bool down, int x, int y) {
        std::lock_guard lock(_pointerMutex);
        // Both event streams feed the same recognizer. Native active-area
        // transitions invalidate a gesture even if it re-enters before Update.
        _click.Pointer(down, x, y);
    }

    void EditorMapTravel::OnUIEvent(SDK::IUIElement *, const SDK::SUIEventDesc &event, const SDK::SUIArguments &arguments, SDK::SFlashVarValue *) {
        const auto *name = event.GetDisplayName();
        if (!name || std::strcmp(name, "OnCursorOnActiveAreaChanged") != 0) {
            return;
        }
        bool onMap = false;
        if (const auto *argument = arguments.GetArgument(0)) {
            argument->ReadBool(onMap);
        }
        std::lock_guard lock(_pointerMutex);
        _click.ActiveArea(onMap);
    }

    void EditorMapTravel::OnUnload(SDK::IUIElement *) {
        std::lock_guard lock(_pointerMutex);
        _click.ActiveArea(false);
        _click.Reset();
    }

    void EditorMapTravel::StopListening() {
        if (_mapElement) {
            _mapElement->RemoveEventListener(this);
            _mapElement->Release();
            _mapElement = nullptr;
        }
        OnUnload(nullptr);
    }

    void EditorMapTravel::ClearPointer() {
        std::lock_guard lock(_pointerMutex);
        _click.Reset();
    }

    void EditorMapTravel::Shutdown() {
        if (_ownsMap) {
            if (auto *apse = Apse()) {
                apse->Hide(true);
            }
        }
        Reset();
    }

    void EditorMapTravel::Update(KCDC::Features::Camera::AnchorCamera &camera) {
        if (!Active()) {
            return;
        }
        auto *app    = Core::gApplication.get();
        auto *player = KCDC::Common::LocalPlayer();
        if (!app || !app->IsOfflineBuilderReady()) {
            Reset();
            return;
        }
        if (!player || player->GetId() != _player || player->GetParent()) {
            Finish("failed", "The offline player changed before travel completed");
        }
        const auto elapsed = std::chrono::steady_clock::now() - _started;
        if (_phase == Phase::Streaming && app->GetInput() && app->GetInput()->IsKeyPressed(VK_ESCAPE)) {
            Cancel();
        }
        auto *map = SDK::wh::guimodule::C_UIMap::GetInstance();
        if (_phase == Phase::Opening) {
            if (!_showRequested) {
                if (app->AreControlsLocked()) {
                    Finish("failed", "Another interface is holding game input");
                    return;
                }
                auto *apse     = Apse();
                auto params    = SDK::wh::framework::S_UIApseShowParams::Make();
                _showRequested = true;
                if (!apse || !apse->Show(SDK::wh::framework::E_UIApseView::Type::Map, &params)) {
                    Finish("failed", "The native map could not open");
                    return;
                }
                _ownsMap = true;
            }
            if (map && map->IsOpen()) {
                _mapElement = map->GetContent()->GetElement();
                if (!_mapElement) {
                    Finish("failed", "The native map input is unavailable");
                    return;
                }
                _mapElement->AddRef();
                _mapElement->AddEventListener(this, "KCDCMapTravel");
                {
                    std::lock_guard lock(_pointerMutex);
                    _click.ActiveArea(map->IsCursorOnMap());
                }
                ClearPointer();
                _phase  = Phase::Selecting;
                _status = "selecting";
            }
            else if (elapsed > std::chrono::seconds(5)) {
                Finish("failed", "The native map did not become ready");
            }
        }
        else if (_phase == Phase::Selecting) {
            if (!map || !map->IsOpen()) {
                Finish("cancelled", "Map closed without travelling");
                return;
            }
            auto input = app->GetInput();
            if (!input || GetForegroundWindow() != Core::gGlobals.window || app->AreControlsLocked()) {
                ClearPointer();
                _mouseReady = false;
                return;
            }
            const bool down = input->IsMouseButtonDown(0);
            if (!_mouseReady) {
                ClearPointer();
                _mouseReady = !down;
                return;
            }
            bool clicked;
            {
                std::lock_guard lock(_pointerMutex);
                clicked = _click.TakeClick();
            }
            if (clicked && map->IsCursorOnMap()) {
                SDK::Vec3 position {};
                std::string error;
                if (map->GetCursorWorldPosition(position)) {
                    if (!Travel(camera, position, error)) {
                        _message = error;
                    }
                }
                else {
                    _message = "Select a point inside the loaded region";
                }
            }
        }
        else if (_phase == Phase::Closing || _phase == Phase::Returning) {
            StopListening();
            if (_ownsMap) {
                if (auto *apse = Apse()) {
                    apse->Hide(true);
                }
                if (ApseHeld()) {
                    return;
                }
                _ownsMap = false;
            }
            if (!camera.IsActive() && !camera.Take()) {
                if (elapsed > std::chrono::seconds(5)) {
                    _status  = "failed";
                    _message = "The editor camera could not be restored";
                    _phase   = Phase::Idle;
                }
                return;
            }
            camera.SetFieldOfView(_savedFov);
            if (_phase == Phase::Returning) {
                const auto position = _status == "arrived" ? SDK::Vec3 {_destination.x, _destination.y, _destination.z + 2.0F} : _savedCamera.position;
                camera.SetPose(position, KCDC::Features::Camera::PoseForward(_savedCamera));
                _phase = Phase::Idle;
            }
            else {
                // The streaming camera goes first. Keep the player at their
                // original location until destination collision is present.
                camera.SetPose({_destination.x, _destination.y, _destination.z + 20.0F}, KCDC::Features::Camera::PoseForward(_savedCamera));
                _phase   = Phase::Streaming;
                _status  = "streaming";
                _started = std::chrono::steady_clock::now();
            }
        }
        else if (_phase == Phase::Streaming) {
            // Streamed entity layers can return the active view to the player.
            // Keep the destination as the streaming focus without flight input.
            camera.Reclaim();
            SDK::Vec3 ground {};
            auto *physics = SDK::CPhysicalWorld::GetInstance();
            if (physics && physics->ResolveGround(_destination, 64.0F, 128.0F, ground)) {
                _groundFrames = _groundFrames && std::abs(ground.z - _ground.z) < 0.05F ? _groundFrames + 1 : 1;
                _ground       = ground;
                if (_groundFrames >= 3 && elapsed >= std::chrono::milliseconds(750)) {
                    Features::Player::TeleportRequest request;
                    request.position            = {_ground.x, _ground.y, _ground.z + 0.2F};
                    request.label               = "World Builder map";
                    request.offlineEditorPlayer = _player;
                    _destination                = request.position;
                    _placement                  = app->GetPlayerTeleporter().QueueTeleport(request);
                    _phase                      = Phase::Placing;
                    _status                     = "placing";
                }
            }
            else {
                _groundFrames = 0;
            }
            if (_phase == Phase::Streaming && elapsed > std::chrono::seconds(8)) {
                Finish("failed", "Destination collision did not load; teleport cancelled");
            }
        }
        else if (_phase == Phase::Placing) {
            auto &teleporter = app->GetPlayerTeleporter();
            if (teleporter.GetLastPlacementId() >= _placement) {
                if (teleporter.GetLastPlacementId() == _placement && teleporter.GetLastResult() == Features::Player::TeleportResult::Succeeded) {
                    Finish("arrived", "Travelled to the map destination");
                }
                else {
                    Finish("failed", "Map travel was interrupted or the player could not be moved");
                }
            }
        }
    }

    nlohmann::json EditorMapTravel::State() const {
        return {{"sequence", std::to_string(_sequence)}, {"active", Active()}, {"status", _status}, {"message", _message}, {"destination", _selected ? nlohmann::json::array({_destination.x, _destination.y, _destination.z}) : nlohmann::json(nullptr)}};
    }
} // namespace KCDC::Features::Prop
