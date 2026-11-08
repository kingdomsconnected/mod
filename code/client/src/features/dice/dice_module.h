#pragma once

#include "shared/features/player/player_station.h"
#include "shared/features/dice/dice_scene.h"
#include <unordered_map>

#include "core/feature_registry.h"

#include "shared/features/dice/dice_rpc.h"
#include "shared/features/dice/dice_rules.h"

#include "sdk/cryengine/system/xml_node_ref.h"
#include "sdk/types.h"
#include "sdk/wh/entitymodule/c_actor.h"
#include "sdk/wh/playermodule/c_dice.h"
#include "sdk/wh/playermodule/c_dice_game_model.h"
#include "sdk/wh/playermodule/c_dice_game_player_model.h"
#include "sdk/wh/playermodule/c_die_set.h"
#include "sdk/wh/playermodule/c_simple_dice_ai.h"
#include "sdk/wh/playermodule/i_dice_game_model_delegate.h"
#include "sdk/wh/playermodule/i_dice_game_model_state.h"

#include <mafianet/get_time.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

namespace KCDC::Features::Dice {
    /**
     * A dice match against another player, on the game's own dice table.
     *
     * The local player plays `C_Dice` exactly as against an NPC. The opponent
     * is the other player's body -- the native player body that already
     * stands for them -- bound as the game's opponent actor, so the game seats it,
     * animates its throws and keeps its dice. Seat 0 of the game is always
     * the local player; the server's seats map onto it, and the player on the
     * server's second seat swaps its table's sides so both machines put both
     * bodies on the same chairs.
     *
     * Nothing about the dice is decided here:
     *
     * - **Faces.** Every throw is dealt by the server and lent to
     *   `RollDice` as its override for that call only, with the soul's
     *   dice-luck chances switched off, so the dice show exactly the deal.
     * - **Local moves.** Continue and Pass are held back: the press goes to
     *   the server and the game only plays it when the commit returns, which
     *   is also when the next throw's faces arrive. Holds are frozen
     *   meanwhile, so the dice the server scored are the ones held.
     * - **The opponent.** The seat keeps the game's dice AI -- the game keys
     *   the opponent's throws and camera on it -- but it never decides: it
     *   claims every PlayerTurn without acting, which is what stops the state
     *   falling through to an automatic pass, and lets Holding run its native
     *   course. The other player's committed moves are replayed onto the
     *   seat one hold at a time, then scored or passed.
     * - **Badges and barks.** A badge is never bound, so none applies, and the
     *   opponent's NPC commentary is not voiced on a player's body.
     *
     * Game-thread only: the detours, the RPC handlers and the tick all run there.
     */
    class DiceModule final: public KCDC::Core::IFeatureService {
      public:
        static DiceModule &Get();

        /** Installs the detours. Called once, at boot. */
        static void Install();

        /**
         * Gives the throw's player cues an audible range as the audio system
         * loads an ATL control document; false when the document has none of
         * them. Runs once per control file, before any can play.
         */
        static bool ExtendGameAudio(SDK::XmlNodeRef &document);

        void Update() override;
        void Reset() override;

        /** Raised on the loading thread; the tick drops the match without touching the table. */
        void NativeContextChanged() override;

        /** Whether the dice table owns this body: the opponent's copy while a match is seated. */
        bool HoldsBody(SDK::EntityId entityId) const;
        Shared::Game::StationState Station() const;

        /** A remote body is about to go. The table is closed while the actor it holds is still alive. */
        void ForgetBody(SDK::EntityId entityId);

        void ApplyScoreNames() const;

        // --- Detour callbacks ------------------------------------------------

        /** After the table's tag points are resolved: swaps the sides when the local player is the server's second seat. */
        void OnTableSceneInitialized(SDK::wh::playermodule::C_Dice *dice);

        /** Seeds the spectator view when the opponent takes the opening turn. */
        void OnNavigationCameraUpdated(SDK::wh::playermodule::C_DieSet *dice, const SDK::Vec3 &position, SDK::Quat &rotation);

        /**
         * The faces to lend a throw of `player`, or empty to let it roll
         * natively. Consumes the seat's next deal.
         */
        std::vector<std::int32_t> *TakeThrow(SDK::wh::playermodule::C_DiceGamePlayerModel *player, bool selectedOnly);

        /** The seat a Rolling transition hands the throw to: the server's first thrower on the opening throw. */
        SDK::wh::playermodule::C_DiceGamePlayerModel *RedirectThrow(SDK::wh::playermodule::I_DiceGameModelDelegate *delegate, SDK::wh::playermodule::C_DiceGamePlayerModel *player);

        /** Whether the opponent AI's turn is ours to answer, and the answer: claim PlayerTurn, decline everything else. */
        std::optional<bool> AnswerForAI(SDK::wh::playermodule::C_DiceGameModel *model, SDK::wh::playermodule::C_DiceGamePlayerModel *player, SDK::wh::playermodule::I_DiceGameModelState *state, bool *handled) const;

        /** Whether a local key press may reach the native handler. A held-back Continue or Pass is sent to the server instead. */
        bool AllowsHold(SDK::wh::playermodule::C_Dice *dice) const;
        bool AllowsMove(SDK::wh::playermodule::C_Dice *dice, Shared::Game::Dice::MoveKind kind);

        /** The game ended itself: a natural end, or the local player giving up. */
        void OnNativeEnding(SDK::wh::playermodule::I_DiceGameModelDelegate *delegate, SDK::wh::playermodule::C_DiceGamePlayerModel *player, std::uint32_t result);

        /** The session closed natively, however it ended. */
        void OnNativeFinished(SDK::wh::playermodule::C_Dice *dice);

        /** Whether a badge may be bound: never while a match runs. */
        bool RefusesBadge() const;

        /** Whether an actor's bark must stay silent: the opponent's copy is a person, not an NPC. */
        bool SilencesBark(const SDK::wh::entitymodule::C_Actor *actor) const;

      private:
        enum class Phase : std::uint8_t {
            Idle,
            /** The match is known; the table has not been started yet. */
            Starting,
            /** `StartGameWithTargetScore` succeeded; the table is ours until it finishes. */
            Seated,
        };

        struct ObserverScene {
            std::array<SDK::EntityId, 13> entities {};
            std::array<std::uint32_t, 13> models {};
            Shared::Game::Dice::Scene previous;
            Shared::Game::Dice::Scene target;
            std::uint64_t sampledAt = 0;
            bool touched            = false;
        };
        std::unordered_map<std::uint64_t, ObserverScene> _scenes;
        DiceModule();
        std::vector<std::size_t> _sceneModels;
        void UpdateScenes();
        void ClearScenes(bool removeEntities);
        void ClearScene(ObserverScene &scene, bool removeEntities);
        Shared::Game::Dice::Scene CaptureScene() const;
        void InstallRPCs();
        void OnMatchStart(const Shared::RPC::DiceMatchStart &start);
        void OnCommit(const Shared::RPC::DiceCommit &commit);
        void OnRefuse(const Shared::RPC::DiceRefuse &refuse);
        void OnMatchEnd(const Shared::RPC::DiceMatchEnd &end);

        /** Finds the table and the opponent's body and starts the native game. False while either is not there yet. */
        bool TryStart();

        /** Plays the next committed move onto the table once the seat it belongs to is ready for it. */
        void ApplyCommits();

        /** Ends the table natively for a match the server ended. */
        void ForceNativeEnd();

        void QueueDeals(const std::vector<Shared::RPC::DiceDeal> &deals);
        void Leave(Shared::Game::Dice::EndReason reason);
        void Clear();

        /** The game's seat for a server seat: 0 is always the local player. */
        std::int32_t EngineSeat(std::uint8_t serverSeat) const {
            return serverSeat == _localSeat ? 0 : 1;
        }

        /** The table's game model, null before setup or without a table. */
        SDK::wh::playermodule::C_DiceGameModel *Model() const;

        /** The dice the local seat holds right now, as a die-index mask, and their faces. */
        std::uint8_t LocalHeldMask(std::vector<std::uint8_t> &faces) const;

        std::array<std::string, 2> _names;
        Phase _phase                         = Phase::Idle;
        std::uint32_t _matchId               = 0;
        std::uint8_t _localSeat              = 0;
        std::uint8_t _firstSeat              = 0;
        std::uint64_t _opponentNetworkId     = 0;
        SDK::EntityId _opponentEntityId      = 0;
        std::uint32_t _targetScore           = 0;
        SDK::Vec3 _anchor                    = {};
        SDK::wh::playermodule::C_Dice *_dice = nullptr;
        MafiaNet::Time _startDeadline        = 0;

        /** Dealt faces not yet thrown, per game seat, in the order the throws come. */
        std::array<std::deque<std::vector<std::int32_t>>, 2> _deals;

        /** The faces lent to the `RollDice` in progress; kept alive across the call. */
        std::vector<std::int32_t> _lent;

        /** Commits not yet played onto the table, in order. */
        std::deque<Shared::RPC::DiceCommit> _commits;
        std::uint32_t _commitsReceived = 0;

        /** The die-index mask still to hold for the front commit, when it is the opponent's. */
        std::uint8_t _replayHolds = 0;
        bool _replayStarted       = false;

        bool _movePending          = false;
        std::uint32_t _pendingMove = 0;
        bool _applyingLocal        = false;
        bool _firstThrowRedirected = false;
        bool _cameraInitialized = false;

        bool _serverEnded               = false;
        Shared::Game::Dice::EndReason _endReason = Shared::Game::Dice::EndReason::Stopped;
        std::uint8_t _winnerSeat        = Shared::Game::Dice::kNoSeat;
        MafiaNet::Time _endDeadline     = 0;
        bool _nativeEnded               = false;
        bool _leftSent                  = false;

        bool _rpcsInstalled = false;

        /** Set from NativeContextChanged, acted on by the tick. */
        std::atomic<bool> _nativeContextLost = false;
    };
} // namespace KCDC::Features::Dice
