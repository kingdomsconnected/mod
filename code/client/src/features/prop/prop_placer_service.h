#pragma once

#include "core/feature_registry.h"
#include "prop_ghost.h"

#include "sdk/cryengine/action/i_action_filter.h"
#include "sdk/cryengine/action/i_action_map.h"
#include "sdk/cryengine/action/i_action_map_manager.h"
#include "sdk/cryengine/action/i_game_object.h"
#include "sdk/cryengine/system/i_system.h"
#include "sdk/types.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace KCDC::Features::Prop {
    /** Where a placement is aimed, and what it is aimed at. */
    struct PlacementPose {
        SDK::Vec3 position {0.0F, 0.0F, 0.0F};
        SDK::Quat rotation {0.0F, 0.0F, 0.0F, 1.0F};

        /** The surface normal under the aim, or straight up when nothing was hit. */
        SDK::Vec3 normal {0.0F, 0.0F, 1.0F};

        /** The game's own name for what is underfoot -- `mat_dirt`, `mat_stone`. */
        std::string surface;

        /** How far the aim point is from the camera, in metres. */
        float distance = 0.0F;

        /** False when the ray left the level and the fallback distance was used. */
        bool onSurface = false;
    };

    /**
     * What the placer refuses a spot for. Every rule is off at its default.
     *
     * None of it is authority: these colour a ghost, and what is actually
     * built is settled on the server from the pose the client confirmed.
     */
    struct PlacementRules {
        /** Degrees the ground may lean. 90 accepts anything. */
        float maxSlopeDegrees = 90.0F;

        /** Surface names that are allowed, or empty for all of them. */
        std::vector<std::string> surfaces;

        float minDistanceFromPlayer = 0.0F;

        /** 0 leaves it to the aim range. */
        float maxDistanceFromPlayer = 0.0F;

        /** Metres that must be clear of other entities. 0 turns the test off. */
        float clearance = 0.0F;

        /** Engine classes the clearance test looks for, or empty for all of them. */
        std::vector<std::string> clearanceClasses;

        /** Whether a spot the ray never reached is refused. */
        bool requireSurface = true;
    };

    /** Everything a placement session is. */
    struct PlacementSpec {
        std::vector<GhostPart> parts;

        GhostTint validTint   = PackGhostTint(60, 230, 90, 90);
        GhostTint invalidTint = PackGhostTint(240, 55, 45, 90);

        /** How far the aim ray reaches, in metres. */
        float range = 40.0F;

        /** Where the ghost sits when the ray reaches nothing. */
        float fallbackDistance = 6.0F;

        float scale = 1.0F;

        /** Lifts or sinks the ghost along the surface normal, in metres. */
        float verticalOffset = 0.0F;

        /** Stands the ghost on the surface it is aimed at rather than upright. */
        bool alignToSurface = false;

        /** Turns it to face away from the player, which is what a wall or a tower wants. */
        bool faceAwayFromPlayer = true;

        /** Added to whatever yaw the two above settle on; the rotate actions move it. */
        float yawDegrees = 0.0F;

        /** Whether a confirm leaves the session running so more can be placed. */
        bool keepOpen = true;

        PlacementRules rules;
    };

    /**
     * What a session reports, called from the service's tick rather than from
     * the input callback -- an action arrives inside the engine's input
     * dispatch, which is no place to run script.
     */
    struct IPropPlacerSink {
        virtual ~IPropPlacerSink() = default;

        /** The aim moved far enough to report, or validity flipped. */
        virtual void OnPlacementMove(const PlacementPose &pose, bool valid) = 0;

        virtual void OnPlacementConfirm(const PlacementPose &pose) = 0;

        /** A confirm the rules refused, so the caller can say why. */
        virtual void OnPlacementRejected(const PlacementPose &pose) = 0;

        virtual void OnPlacementCancel() = 0;

        /** The session is over, however it ended. Always the last call. */
        virtual void OnPlacementEnd() = 0;
    };

    /**
     * Placement mode: a ghost under the crosshair, confirmed with a click.
     *
     * The loop is native and runs on the game thread, so the preview tracks
     * the camera frame for frame. No script runs inside it: the spec's rules
     * are the ones the engine can answer from the ray it has already traced,
     * and anything beyond them is a veto the caller latches with `SetVeto`.
     *
     * Input is an action map of this service's own plus an action filter over
     * the game's attack and block actions. The map is deliberately *not*
     * exclusive: an exclusive one takes every action the game has, including
     * `rotatepitch` and `rotateyaw`, leaving a preview that cannot be aimed.
     * The filter takes away only what a click would otherwise reach.
     *
     * One session at a time.
     *
     * Game thread only, except `NativeContextChanged`.
     */
    class PropPlacerService final
        : public KCDC::Core::IFeatureService
        , public SDK::IActionListener {
      public:
        /** Metres of aim movement before a `move` is worth reporting. */
        static constexpr float kMoveReportDistance = 0.25F;

        /** And a floor on how often, whatever it moved. */
        static constexpr std::chrono::milliseconds kMoveReportInterval {80};

        void Update() override;
        void Reset() override;
        void NativeContextChanged() override;

        void OnAction(const SDK::CCryName &action, int activationMode, float value) override;
        void AfterAction() override {}

        /**
         * Starts a session. `sink` is borrowed and must outlive it; it is
         * told `OnPlacementEnd` whenever the session stops for any reason,
         * including one this service decided on.
         *
         * False when a session is already running, when the world is not up,
         * when another part of the mod holds the controls, or when the ghost
         * could not be built.
         */
        bool Begin(const PlacementSpec &spec, IPropPlacerSink *sink);

        /** Ends it, telling the sink. Safe to call when nothing is running. */
        void End();

        bool IsActive() const {
            return _active;
        }

        /**
         * A caller-side refusal, latched until cleared: where a rule the
         * engine cannot answer belongs, since it costs the loop nothing.
         */
        bool SetVeto(bool vetoed);

        /** Turns the ghost by `degrees`, which is what the rotate actions do. */
        bool Rotate(float degrees);

        /** Where it is aimed right now. False when no session is running. */
        bool ReadPose(PlacementPose &pose, bool &valid) const;

      private:
        /** Which of the map's actions arrived. */
        enum class Pending : std::uint8_t {
            None,
            Confirm,
            Cancel,
        };

        bool AcquireInput();
        void ReleaseInput();

        /** Re-traces the crosshair and settles `_pose`. False when there is no camera. */
        bool Aim();

        /** Runs the spec's rules against the settled pose. */
        bool Evaluate(const PlacementPose &pose) const;

        /** Whether the session may still run at all -- world, camera, controls. */
        bool StillAllowed() const;

        void ReportMoveIfDue(bool validityFlipped);

        PlacementSpec _spec;
        IPropPlacerSink *_sink = nullptr;

        bool _active         = false;
        bool _vetoed         = false;
        bool _valid          = false;
        bool _hasPose        = false;
        std::uint32_t _ghost = PropGhostService::kNoHandle;

        PlacementPose _pose;
        SDK::Vec3 _lastReported {0.0F, 0.0F, 0.0F};
        std::chrono::steady_clock::time_point _lastReport {};

        /** Latched rather than acted on where they arrive; see `OnAction`. */
        Pending _pending = Pending::None;
        float _pendingRotation = 0.0F;

        SDK::IActionMapManager *_inputManager = nullptr;
        SDK::IActionMap *_inputMap            = nullptr;
        bool _listening                       = false;

        /** Borrowed from the manager for the session's length. */
        SDK::IActionFilter *_filter = nullptr;

        /** Whether it was confirmed on, so a session that lost it says so once. */
        bool _filtering = false;

        std::atomic<std::uint64_t> _nativeContext {0};
        std::uint64_t _observedContext = 0;
    };
} // namespace KCDC::Features::Prop
