#pragma once

#include "core/feature_registry.h"
#include "features/camera/anchor_camera.h"

#include "sdk/types.h"

#include "shared/features/camera/camera_rpc.h"

#include <cstdint>
#include <string>

namespace KCDC::Features::Camera {
    /** What happens to the player's body while the camera is off flying. */
    enum class BodyMode : std::uint8_t {
        /** Carried with the camera, physics off. What "noclip" means. */
        Body,
        /** Left where it stood, untouched. A spectator rather than a noclip. */
        Camera,
    };

    bool ParseBodyMode(const std::string &name, BodyMode &mode);
    const char *BodyModeName(BodyMode mode);

    /** How the flight ended, carried to resources on the state event. */
    enum class EndReason : std::uint8_t {
        Script,      // a resource asked for it
        MapEditor,   // the map editor opened and wants the one camera
        ViewLost,    // the level went away underneath the anchor
        SessionOver, // the session ended
    };

    const char *EndReasonName(EndReason reason);

    /** Everything a caller can decide when the flight starts. */
    struct NoClipOptions {
        BodyMode body = BodyMode::Body;

        /**
         * Whether this machine's own keyboard and mouse fly the camera, as
         * opposed to the resource driving it through Move.
         */
        bool builtInInput = true;

        /** Metres a second at rest, before boost or crawl scales it. */
        float speed = 12.0F;

        /** Vertical field of view in degrees, or 0 to keep the game's. */
        float fieldOfView = 0.0F;
    };

    /** Where the third-person camera sits relative to the body it follows, in metres. */
    struct ThirdPersonOptions {
        /** Back along the look direction from the pivot. */
        float distance = 3.5F;

        /** Up from the body's origin to the pivot the camera orbits. */
        float height = 1.6F;

        /** Sideways from the pivot, positive to the right: an over-the-shoulder offset. */
        float shoulder = 0.0F;
    };

    /** A camera a script placed: where it stands, what it looks at, and how it gets there. */
    struct ShotOptions {
        SDK::Vec3 position {};
        SDK::Vec3 lookAt {};

        /** 0 is a cut; otherwise the glide from wherever the view is now. */
        std::uint32_t durationMs = 0;

        /** Vertical degrees, or 0 for the player's own lens. */
        float fieldOfView = 0.0F;
    };

    /** Why `Enable` refused, or that it did not. */
    enum class EnableResult : std::uint8_t {
        Enabled,
        AlreadyActive,
        NoCamera,   // no level, or the camera anchor could not be spawned
        CameraBusy, // the map editor is flying the one camera, or a shot or the third-person camera holds it
    };

    const char *EnableResultName(EnableResult result);

    /**
     * Flies the local player through the world without colliding with it.
     *
     * Two native halves, neither simulated by the mod. The view is a bodyless
     * anchor entity made the active camera through `CViewSystem::SetActive-
     * Camera`, so `CView::Update` reads its transform and nothing sweeps. The
     * body, in `Body` mode, is the player's own entity with
     * `IEntity::EnablePhysics(false)` on it and its transform stated each
     * frame.
     *
     * The game's own fly mode is deliberately not that path. It is all still
     * here -- `C_ScriptBindPlayer::SetFlyMode` (0x182AC26B4) dispatches player
     * state event 127, and `C_PlayerStateMovement::Fly` (0x182ABA410) enters
     * through `PhySetFly` (0x18285FCE4). Its per-frame handler falls back
     * to ground on the first prephysics tick, so the mode never holds.
     *
     * Game thread only, driven from the session feature sweep.
     */
    class CameraControlService final: public Core::IFeatureService {
      public:
        EnableResult Enable(const NoClipOptions &options);

        /**
         * Ends the flight. `keepPosition` leaves the body where the camera
         * stopped; false puts it back where it took off from.
         */
        void Disable(bool keepPosition, EndReason reason);

        bool IsActive() const {
            return _active;
        }

        BodyMode GetBodyMode() const {
            return _options.body;
        }

        bool IsBuiltInInputEnabled() const {
            return _options.builtInInput;
        }

        void SetBuiltInInputEnabled(bool enabled);

        // --- Third person ----------------------------------------------------

        /**
         * Puts the camera behind the local player, for a body whose own head
         * view is no use -- one hidden inside a disguise mesh -- or re-aims a
         * camera already there.
         *
         * The game has none a player can switch to. `C_CameraThirdPerson` is
         * built and intact, but the gameplay camera selector never picks it
         * and the actor's `ToggleThirdPerson` is an empty slot, so the
         * `g_tpview_*` cvars reach nothing. So it is this service's own
         * camera, the anchor a flight flies, placed every frame behind the
         * body along where the player is looking instead. Nothing about the
         * player's input is touched: they walk and look as they did. The
         * player's head, which the game hides for its own first person, is
         * drawn for as long as this camera is the one looking at them.
         *
         * A flight cannot start while it holds the view, and it waits while
         * the map editor has it, or while an Apse page -- the map, the
         * inventory -- has the game's UI camera up.
         */
        void EnableThirdPerson(const ThirdPersonOptions &options);

        /** Hands the view back to the game. Safe when not on. */
        void DisableThirdPerson();

        /** Whether a script asked for it; the view itself may be waiting for a body or for the map editor. */
        bool IsThirdPerson() const {
            return _thirdPerson;
        }

        // --- Shots ----------------------------------------------------------

        /**
         * Films the local player's view from a fixed point -- what a cutscene
         * is made of -- cutting there or gliding from wherever the view is.
         *
         * The same anchor view the flight flies, so nothing of the game's own
         * camera is touched and nothing about the player's input changes. The
         * player's head, which the game keeps out of its first person, is drawn
         * for as long as the shot may be looking at them, through the camera
         * manager's own suppression (see `C_CameraManager::SetFirstPersonView-
         * Suppressed`), and hidden again only once the view is back inside
         * them or in the game's hands. A shot outranks the third-person
         * camera, which picks up again when it ends, and steps aside for the
         * map editor and the Apse pages as that camera does. Refused while a
         * flight holds the view, or for a camera standing on the point it
         * looks at.
         */
        bool SetShot(const ShotOptions &options);

        /** Gives the player their own view back, by a cut or a glide into their eyes. Safe when there is no shot. */
        void ClearShot(std::uint32_t durationMs);

        /** From `SetShot` until `ClearShot`: false already while gliding back. */
        bool IsShotActive() const {
            return _shot && !_shotReturning;
        }

        /** Registers the server's `CameraShot`. Once, at startup. */
        static void Install();

        /** A shot the server sent this player. */
        void Receive(const KCDC::Shared::RPC::CameraShot &shot);

        /**
         * The camera, whether or not it is flying. Handed out rather than
         * wrapped verb by verb: a second copy of each setter here would only
         * be a place for the two to disagree.
         */
        AnchorCamera &GetCamera() {
            return _camera;
        }

        const AnchorCamera &GetCamera() const {
            return _camera;
        }

        /**
         * Moves the camera by one frame of flight the caller resolved itself.
         * Refused while the built-in driver is on, so the two cannot both
         * integrate the same frame.
         */
        bool Move(const FlyInput &input, float deltaSeconds);

        void Update() override;
        void Reset() override;

      private:
        /** Reads this machine's keyboard into a frame of flight. */
        void ReadFlyInput(FlyInput &fly) const;

        /**
         * The mouse's travel since the last frame, as two angles, and puts the
         * cursor back in the middle of the window. Recentring is what makes
         * the look unbounded; the first frame only recentres.
         */
        bool ReadLookDelta(float &yawDelta, float &pitchDelta);

        /** Where the body's own origin goes for the camera to be at its eyes. */
        SDK::Vec3 BodyPositionFor(const CameraPose &pose) const;

        /** Takes the body out of the physical world and remembers where it was. */
        void HoldBody();

        /** States the body's pose at the camera for this frame. */
        void CarryBody();

        /** Puts the body back into the physical world, here or where it started. */
        void ReleaseBody(bool keepPosition);

        void HoldControls(bool hold);

        /** Places the camera behind the local player for this frame, taking the view when it has none. */
        void FollowBehind();

        /** Where that is and which way it looks, or false with no body to follow. */
        bool ThirdPersonPose(SDK::Vec3 &position, SDK::Vec3 &forward) const;

        /** Where the player's own eyes are and the way they look, as their body holds it. */
        bool OwnViewPose(SDK::Vec3 &eye, SDK::Vec3 &forward) const;

        /** The map editor or an Apse page has the one camera; see FollowBehind. */
        bool CameraMustYield() const;

        /** Places this frame of the shot: its held pose, or a step of its glide. */
        void UpdateShot();

        /** Ends the shot now, handing the view to the third-person camera or back to the game. */
        void FinishShot();

        /**
         * Keeps the game's own first-person head hiding off while one of this
         * service's cameras films the player from outside their eyes, and puts
         * it back as it was found. Idempotent.
         */
        void ShowHead(bool shown);

        /** Takes the view when there is none yet; false while the level has nothing to take it with. */
        bool HoldView();

        AnchorCamera _camera;
        NoClipOptions _options {};
        bool _active = false;

        /**
         * The entity actually carried, which is the outermost parent of the
         * player's own. Resolved once, so the body put back is the body taken.
         */
        SDK::EntityId _bodyEntityId = 0;

        /** How far the camera sits above the body's origin, measured at takeoff. */
        float _bodyEyeHeight = 0.0F;

        SDK::Vec3 _bodyOrigin {};
        SDK::Quat _bodyOriginRotation {0.0F, 0.0F, 0.0F, 1.0F};
        bool _bodyPhysicsWasEnabled = true;

        ThirdPersonOptions _thirdPersonOptions {};
        bool _thirdPerson = false;

        // A shot holds the view from SetShot until it has glided back. Its
        // glide runs from the pose the view had when it was asked for to the
        // target; on the way back the target is the player's eyes, read again
        // every frame because they keep walking.
        bool _shot          = false;
        bool _shotReturning = false;
        SDK::Vec3 _shotPosition {};
        SDK::Vec3 _shotForward {0.0F, 1.0F, 0.0F};

        /** The lens the shot asked for, or 0 to keep filming through the player's own. */
        float _shotFieldOfView = 0.0F;

        SDK::Vec3 _glideFromPosition {};
        SDK::Vec3 _glideFromForward {0.0F, 1.0F, 0.0F};
        float _glideFromFieldOfView          = 60.0F;
        std::int64_t _glideStartMicroseconds = 0;
        float _glideSeconds                  = 0.0F;

        /**
         * The lens to hand back with the view, read the first frame a shot
         * actually holds it and 0 until then: an unheld camera reports
         * whatever it was last asked for rather than what the game is drawing
         * through, and 60 by default.
         */
        float _ownFieldOfView = 0.0F;

        // The camera manager's suppression as it was found, put back as it was.
        bool _headShown         = false;
        bool _headWasSuppressed = false;

        bool _controlsHeld = false;
        bool _looking      = false;

        // Zero means "first tick", which travels no distance.
        std::int64_t _lastTickMicroseconds = 0;
    };
} // namespace KCDC::Features::Camera
