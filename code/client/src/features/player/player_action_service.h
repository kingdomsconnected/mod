#pragma once

#include "core/feature_registry.h"

#include "sdk/cryengine/action/i_action_controller.h"
#include "sdk/cryengine/action/i_mannequin_action.h"
#include "sdk/cryengine/animation/i_skeleton_anim.h"
#include "sdk/types.h"

#include "shared/features/player/player_action.h"
#include "shared/features/player/station_tools.h"

#include <atomic>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

namespace KCDC::Features::Player {
    /**
     * Client owner of what the local player's body does that other players
     * have to see: sitting down and standing up, the game's interaction
     * animations, and praising the dog.
     *
     * None of these is an event the mod is told about. Each is only visible
     * where the engine queues it, so the hooks report here:
     *
     * - `CActionController::Queue` for every Mannequin action. One the local
     *   player's controller takes whose fragment is in the body action catalog
     *   becomes a `Fragment` action. If it loops, a reference is held, and when
     *   it ends with nothing newer after it a `Stop` follows, so receivers do
     *   not loop on forever.
     * - `C_ActionActor::QueueAction` for the dog's paired praise, which the
     *   animal starts (`C_Dog::Bond`) and the player only takes part in. Only
     *   a dog this client owns counts: a praise replayed here on somebody
     *   else's dog comes back through the same hook and is not ours to report.
     * - `CActionController::Queue` again for the lockpicking, alchemy and
     *   smithing minigames' actions (`I_LockPickingAction`, and callback
     *   actions on an `Alchemy*` or `Blacksmithing*` fragment). Each is held
     *   until it ends, and the ones still running travel as
     *   `MinigameActions`.
     *
     * The owner replica takes what was caught once per tick. The hooks run from
     * whichever thread drives the actor, so the controller they compare against
     * is refreshed on the main thread and read atomically, and the capture sits
     * behind a mutex.
     */
    class PlayerActionService final: public KCDC::Core::IFeatureService {
      public:
        static PlayerActionService &Get();

        void Update() override;
        void Reset() override;

        /** Any thread, from the Queue hook: one action about to be queued on `controller`. */
        void NotifyQueued(SDK::IActionController *controller, SDK::IAction &action);

        // Human retains the action and unregisters it before releasing it.
        void TrackPickingMotion(SDK::IActionController *controller, SDK::IAction &action, SDK::ISkeletonAnim &skeleton, const std::array<float, 3> &values);
        void ForgetPickingMotion(SDK::IAction *action);
        void ApplyPickingMotion(SDK::IActionController *controller);

        /** Any thread, from the action-queue hook: a praise was queued on the dog with that entity. */
        void NotifyPraise(SDK::EntityId dogEntityId, std::uint32_t variant);

        /**
         * Main thread: the body action captured since the last call, with
         * `revision` left for the caller to assign. Empty when there was none.
         */
        std::optional<Shared::Game::BodyAction> TakeLocalAction();
        std::uint32_t DiceThrow();

        /** Main thread: the minigame actions still queued or playing, oldest first, and a lockpicking turn's progress. */
        Shared::Game::MinigameActions CaptureMinigameActions();

        /**
         * Any thread, from the `AnimEvent` clip hook: a tool event a fragment
         * raised for the body with that entity, in `action`. Held until the
         * next Update hands it to the remote body that replays the action.
         */
        void NotifyStationToolEvent(SDK::EntityId bodyEntityId, const SDK::IAction *action, Shared::Game::StationToolEvent event);

      private:
        /** Drops the action whose end is replicated, if any. Under the mutex. */
        void ReleaseHeldAction();
        void ReleaseCapturedAction();

        struct PickingPlayback {
            SDK::IAction *action;
            SDK::ISkeletonAnim *skeleton;
            std::array<float, 3> values;
        };

        // The local player's action controller, as the last main-thread tick
        // resolved it. Compared, never dereferenced, off the main thread.
        std::atomic<SDK::IActionController *> _localController {nullptr};

        std::mutex _mutex;
        std::optional<Shared::Game::BodyAction> _captured; // protected by the mutex
        SDK::EntityId _praisedDogEntityId = 0;             // protected by the mutex; resolved on take
        SDK::IAction *_heldAction         = nullptr;                                    // protected by the mutex; one reference held
        SDK::IAction *_capturedAction     = nullptr;       // one reference until pickup's blend space is installed
        std::unordered_map<SDK::IActionController *, PickingPlayback> _pickingPlayback; // protected by the mutex
        // Mirrors !_pickingPlayback.empty(), so the per-controller update hook
        // skips the mutex for every body while no pickup is replaying.
        std::atomic<bool> _pickingTracked = false;
        SDK::IAction *_diceThrow          = nullptr;       // protected by the mutex; one reference held
        std::uint32_t _diceThrowRevision  = 0;

        struct HeldMinigameAction {
            Shared::Game::MinigameActions::Action wire;
            SDK::IAction *action; // one reference held
        };
        std::vector<HeldMinigameAction> _minigameActions; // protected by the mutex; oldest first
        std::uint8_t _lastMinigameActionId = 0;           // protected by the mutex

        // Compared against the actions a body holds, never dereferenced.
        struct StationToolNotice {
            SDK::EntityId bodyEntityId = 0;
            const SDK::IAction *action = nullptr;
            Shared::Game::StationToolEvent event;
        };
        std::vector<StationToolNotice> _stationToolEvents; // protected by the mutex
    };
} // namespace KCDC::Features::Player
