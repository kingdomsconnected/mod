#pragma once

#include "features/camera/anchor_camera.h"
#include "sdk/cryengine/scaleform/c_flash_ui_element.h"
#include "sdk/cryengine/scaleform/i_ui_element_event_listener.h"
#include <chrono>
#include <cmath>
#include <mutex>
#include <nlohmann/json.hpp>

namespace KCDC::Features::Prop {
    inline nlohmann::json MapTravelTools() {
        auto tools     = nlohmann::json::array();
        const auto add = [&](const char *name, const char *description, bool read, bool destination = false) {
            auto schema = nlohmann::json {{"type", "object"}, {"properties", nlohmann::json::object()}, {"additionalProperties", false}};
            if (destination) {
                schema["properties"]["position"] = {{"type", "array"}, {"minItems", 2}, {"maxItems", 2}, {"items", {{"type", "number"}, {"minimum", 0}, {"maximum", 32768}}}};
                schema["required"]               = {"position"};
            }
            tools.push_back({{"name", name}, {"description", description}, {"inputSchema", std::move(schema)}, {"outputSchema", {{"type", "object"}}}, {"annotations", {{"readOnlyHint", read}, {"destructiveHint", !read}, {"idempotentHint", read}, {"openWorldHint", false}}}});
        };
        add("wb_map_get", "Read map travel status, sequence, destination and error. Arrival is status=arrived and active=false.", true);
        add("wb_map_open", "Open the native map for offline click-to-travel. Operation completion acknowledges the request; poll wb_map_get for status=selecting.", false);
        add("wb_map_travel", "Travel to world X/Y in the loaded region, resolving terrain and streaming collision before moving the unmounted player. Operation completion is acceptance; poll wb_map_get for arrival or failure.", false, true);
        add("wb_map_cancel", "Cancel map selection or destination streaming, restoring the editor camera. An already queued placement cannot be cancelled.", false);
        return tools;
    }

    /** A click starts and ends over the map, without ever becoming a drag. */
    class MapTravelClick {
      public:
        void ActiveArea(bool onMap) {
            _onMap = onMap;
            if (!onMap) {
                _candidate = _pending = false;
            }
        }
        bool Pointer(bool down, int x, int y) {
            if (_pending && (down || x != _releaseX || y != _releaseY)) {
                _pending = false;
            }
            if (down && !_down) {
                _x         = x;
                _y         = y;
                _candidate = _onMap;
            }
            if (down && (!_onMap || std::abs(x - _x) > 5 || std::abs(y - _y) > 5)) {
                _candidate = false;
            }
            const bool selected = !down && _down && _candidate && _onMap && std::abs(x - _x) <= 5 && std::abs(y - _y) <= 5;
            _down               = down;
            if (!down) {
                _candidate = false;
            }
            if (selected) {
                _pending  = true;
                _releaseX = x;
                _releaseY = y;
            }
            return selected;
        }
        bool TakeClick() {
            const bool pending = _pending;
            _pending           = false;
            return pending;
        }
        void Reset() {
            _down = _candidate = _pending = false;
        }

      private:
        bool _onMap     = false;
        bool _down      = false;
        bool _candidate = false;
        bool _pending   = false;
        int _releaseX   = 0;
        int _releaseY   = 0;
        int _x          = 0;
        int _y          = 0;
    };

    /** Game-thread owner; render only reads State() from the editor snapshot. */
    class EditorMapTravel: public SDK::IUIElementEventListener {
      public:
        bool Open(KCDC::Features::Camera::AnchorCamera &camera, std::string &error);
        bool Travel(KCDC::Features::Camera::AnchorCamera &camera, const SDK::Vec3 &position, std::string &error);
        bool Cancel();
        void Update(KCDC::Features::Camera::AnchorCamera &camera);
        void Reset();
        /** Close our native screen while the level is still alive, then forget. */
        void Shutdown();
        /** Window-thread input; retain movement even between game ticks. */
        void Pointer(bool down, int x, int y);
        void ClearPointer();
        void OnUIEvent(SDK::IUIElement *sender, const SDK::SUIEventDesc &event, const SDK::SUIArguments &arguments, SDK::SFlashVarValue *result) override;
        void OnUnload(SDK::IUIElement *sender) override;
        bool Active() const {
            return _phase != Phase::Idle;
        }
        bool NativeInput() const {
            return _ownsMap || _phase == Phase::Opening;
        }
        nlohmann::json State() const;

      private:
        enum class Phase {
            Idle,
            Opening,
            Selecting,
            Closing,
            Streaming,
            Placing,
            Returning
        };
        bool Begin(KCDC::Features::Camera::AnchorCamera &camera, std::string &error);
        void Finish(const char *status, const char *message);
        void StopListening();
        SDK::CFlashUIElement *_mapElement = nullptr;
        Phase _phase                      = Phase::Idle;
        KCDC::Features::Camera::CameraPose _savedCamera {};
        float _savedFov       = 60;
        SDK::EntityId _player = 0;
        SDK::Vec3 _destination {};
        SDK::Vec3 _ground {};
        std::uint64_t _placement = 0;
        std::uint64_t _sequence  = 0;
        unsigned _groundFrames   = 0;
        bool _showRequested      = false;
        bool _ownsMap            = false;
        bool _selected           = false;
        bool _mouseReady         = false;
        MapTravelClick _click;
        std::mutex _pointerMutex;
        std::chrono::steady_clock::time_point _started;
        std::string _status = "idle";
        std::string _message;
    };
} // namespace KCDC::Features::Prop
