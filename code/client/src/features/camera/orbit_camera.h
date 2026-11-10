#pragma once

// First: the action headers below reach <windows.h>, which must not get in ahead of winsock2.
#include <utils/safe_win32.h>

#include "sdk/cryengine/action/i_action_filter.h"
#include "sdk/cryengine/action/i_action_map.h"
#include "sdk/cryengine/action/i_action_map_manager.h"
#include "sdk/cryengine/action/i_game_object.h"
#include "sdk/cryengine/system/cry_name.h"
#include "sdk/types.h"
#include "sdk/wh/entitymodule/c_human.h"

namespace KCDC::Features::Camera {
    /** What a script can decide about the orbit when it asks for one. */
    struct OrbitOptions {
        /** Multiplies the degrees a unit of mouse travel turns the camera by. */
        float sensitivity = 1.0F;

        /** Degrees a second the body turns toward where it is walking. */
        float turnRate = 540.0F;

        /** Whether the head follows the camera while the body holds its facing. */
        bool headLook = true;
    };

    /**
     * The third-person camera's free look: the mouse turns the camera around
     * the player and leaves the body alone, and the body turns only to walk.
     *
     * The game turns a player body with the view: `rotateyaw` feeds the actor
     * rotation's angles, and the body's base is rebuilt from them every frame
     * (C_ActorRotation). So the look actions are held back by an action
     * filter, as the emote wheel holds them, and the mouse's own delta is
     * read from an action map of this class's own bound to `maxis_x` and
     * `maxis_y`. With nothing feeding the angles the body keeps its facing.
     *
     * Walking is then framed by the camera rather than by the body. The move
     * keys' raw axes (C_PlayerInput::GetRawMovementInput) name a direction
     * relative to the camera; the body is turned toward it through its view
     * rotation, at a bounded rate, and the move vector the game consumes is
     * rewritten as "forward" for as long as a key is held.
     *
     * The head is turned to the camera through the movement controller's
     * look target, the way a remote copy's head is turned to its owner's
     * look, bounded to what a neck does.
     *
     * Game thread only. Owned by CameraControlService, which decides when the
     * orbit may steer and gives the look back when it may not.
     */
    class OrbitCamera final: public SDK::IActionListener {
      public:
        ~OrbitCamera() override = default;

        /**
         * Takes the look from the game, starting from `forward` so the view
         * does not jump. False when the input could not be taken, in which
         * case nothing is held.
         */
        bool Begin(const SDK::Vec3 &forward, const OrbitOptions &options);

        /**
         * Gives the look back. With `alignView` the body's view is turned to
         * where the camera was looking, so the game's own camera picks up
         * from the same direction.
         */
        void End(bool alignView);

        bool IsSteering() const {
            return _steering;
        }

        void SetOptions(const OrbitOptions &options);

        /** One frame: the mouse's travel, the body's turn, the head. */
        void Update(float deltaSeconds);

        /** Where the camera looks, as a unit vector. */
        SDK::Vec3 GetForward() const;

        /** Where the head looks: the camera's direction, bounded around the body's facing. */
        SDK::Vec3 GetHeadLook() const {
            return _headLook;
        }

        void OnAction(const SDK::CCryName &action, int activationMode, float value) override;
        void AfterAction() override {}

      private:
        bool AcquireInput();
        void ReleaseInput();

        /** Turns the body toward the camera-relative direction the move keys name. */
        void SteerBody(SDK::wh::entitymodule::C_Human *actor, float deltaSeconds);

        /** Points the head at the camera's direction, or lets it go. */
        void HoldHead(SDK::wh::entitymodule::C_Human *actor, bool hold);

        OrbitOptions _options {};
        bool _steering = false;

        // Radians. Zero yaw faces +Y and a rising yaw swings left, as the
        // actor rotation's own angles do; a positive pitch looks up.
        float _yaw   = 0.0F;
        float _pitch = 0.0F;

        SDK::Vec3 _headLook {0.0F, 1.0F, 0.0F};
        bool _headHeld          = false;
        SDK::EntityId _headEntityId = 0;

        // Whether the move vector currently holds this class's rewrite.
        bool _moveOverridden = false;

        // Accumulated inside the engine's input dispatch, settled in Update.
        float _pendingX = 0.0F;
        float _pendingY = 0.0F;

        // The pad's look stick reports a position, not a travel.
        float _stickX = 0.0F;
        float _stickY = 0.0F;

        SDK::IActionMapManager *_inputManager = nullptr;
        SDK::IActionMap *_inputMap            = nullptr;
        SDK::IActionFilter *_filter           = nullptr;
        bool _listening                       = false;
    };
} // namespace KCDC::Features::Camera
