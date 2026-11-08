#pragma once

#include "features/area/area_collision_service.h"

#include <utils/safe_win32.h>

#include <d3d12.h>
#include <dxgi1_4.h>

#include <integrations/client/instance.h>
#include <utils/states/machine.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "feature_registry.h"
#include "local_events.h"
#include "states/session_startup.h"

#include "sdk/wh/c_readiness_progress.h"
#include "sdk/wh/s_game_session_latch.h"

#include "core/discord_presence.h"
#include "core/streamed_assets/streamed_asset_service.h"
#include "core/ui/brand_logo.h"
#include "core/ui/multiplayer_frontend.h"
#include "features/camera/camera_control_service.h"
#include "features/crafting/alchemy_service.h"
#include "features/crafting/smithing_service.h"
#include "features/dog/dog_service.h"
#include "features/door/door_service.h"
#include "features/door/gate_service.h"
#include "features/gathering/gathering_service.h"
#include "features/ground_item/ground_item_service.h"
#include "features/horse/horse_service.h"
#include "features/level_edit/level_edit_service.h"
#include "features/map/map_blip_service.h"
#include "features/map/map_service.h"
#include "features/marker/marker_service.h"
#include "features/npc/npc_locomotion_service.h"
#include "features/player/player_buff_service.h"
#include "features/player/player_duel_service.h"
#include "features/player/player_inventory_service.h"
#include "features/player/player_missile_service.h"
#include "features/player/player_progression_service.h"
#include "features/player/player_service.h"
#include "features/player/player_teleport.h"
#include "features/prop/prop_editor.h"
#include "features/prop/prop_ghost.h"
#include "features/prop/prop_placer_service.h"
#include "features/prop/prop_service.h"
#include "features/quest/quest_service.h"
#include "features/social/social_emote_service.h"
#include "features/social/social_emote_wheel.h"
#include "features/social/social_scoreboard.h"
#include "features/social/social_voice_service.h"
#include "features/stash/stash_service.h"
#include "features/ui/native_ui_service.h"
#include "features/ui/ui_service.h"
#include "features/vfx/vfx_service.h"
#include "features/light/light_service.h"
#include "features/world/world_environment_service.h"
#include "features/world/world_query_service.h"
#include "features/world/world_service.h"
#include "common/input.h"
#include "common/spawn/actor_spawner.h"
#include "common/spawn/prop_spawner.h"

namespace KCDC::Core {
    /**
     * KCDC client application.
     *
     * Update/PostUpdate runs on the game thread (CCryAction::PostUpdate),
     * Render/PostRender on the render thread (the DX12 present hook).
     */
    class Application final: public Framework::Integrations::Client::Instance {
      private:
        std::shared_ptr<KCDC::Common::GameInput> _input;
        LocalEvents _localEvents;
        States::SessionLoadContext _sessionLoad;
        // The Framework's reason for a failed download, kept here because the load context points at it.
        std::string _downloadFailure;
        States::SessionStartupState *_sessionStartupState = nullptr;
        // The game's own readiness task, holding every load completion -- the
        // loading screen, the level switch's end, gameplay start -- while a
        // session joins. Built on the first join, destroyed in PreShutdown.
        std::optional<SDK::wh::C_ReadinessProgress> _sessionHold;
        // Set by EndGameSessionWhenItStarts; the latch is captured when the
        // game's session starts and ended on the next tick, outside the
        // observer callback that started it.
        bool _endAbandonedGameSession                           = false;
        SDK::wh::S_GameSessionLatch *_abandonedGameSessionLatch = nullptr;
        BrandLogo _brandLogo;
        DiscordPresence _discordPresence;
        KCDC::Features::Prop::MapEditor _mapEditor;
        bool _worldBuilderAccess = false;
        // Offline World Builder: no connection, the level the menu asked for.
        std::string _offlineBuilderLevel;
        bool _offlineBuilder      = false;
        bool _offlineBuilderReady = false;
        bool _offlineBuilderEnded = false;
        KCDC::Common::Spawn::ActorSpawner _actorSpawner;
        KCDC::Common::Spawn::PropSpawner _propSpawner;
        KCDC::Features::World::WorldEnvironmentService _worldEnvironmentService;
        KCDC::Features::Player::PlayerTeleporter _playerTeleporter;
        KCDC::Features::Map::MapBlipService _mapBlipService;
        MultiplayerFrontend _multiplayerFrontend;
        StreamedAssets::StreamedAssetService _streamedAssetService;
        std::optional<ConnectRequest> _launchConnection;
        KCDC::Features::GroundItem::GroundItemService _groundItemService;
        KCDC::Features::Gathering::GatheringService _gatheringService;
        KCDC::Features::Stash::StashService _stashService;
        KCDC::Features::Door::DoorService _doorService;
        KCDC::Features::Door::GateService _gateService;
        KCDC::Features::Horse::HorseService _horseService;
        KCDC::Features::Dog::DogService _dogService;
        KCDC::Features::Prop::PropService _propService;
        KCDC::Features::LevelEdit::LevelEditService _levelEditService;
        KCDC::Features::Area::AreaCollisionService _areaCollisionService;
        KCDC::Features::World::WorldService _worldService;
        KCDC::Features::World::WorldQueryService _worldQueryService;
        KCDC::Features::Social::EmoteService _emoteService;
        KCDC::Features::Social::EmoteWheelService _emoteWheelService;
        KCDC::Features::Social::ScoreboardService _scoreboardService;
        KCDC::Features::Social::VoiceService _voiceService;
        KCDC::Features::Player::PlayerService _playerService;
        KCDC::Features::Player::PlayerBuffService _playerBuffService;
        KCDC::Features::Player::PlayerProgressionService _playerProgressionService;
        KCDC::Features::Player::PlayerMissileService _playerMissileService;
        KCDC::Features::Crafting::AlchemyService _alchemyService;
        KCDC::Features::Crafting::SmithingService _smithingService;
        KCDC::Features::Player::PlayerInventoryService _playerInventoryService;
        KCDC::Features::Npc::NpcLocomotionService _npcLocomotionService;
        KCDC::Features::Player::PlayerDuelService _playerDuelService;
        KCDC::Features::UI::UiService _uiService;
        KCDC::Features::UI::NativeUIService _nativeUIService;
        KCDC::Features::Map::MapService _mapService;
        KCDC::Features::Marker::MarkerService _markerService;
        KCDC::Features::Quest::QuestService _questService;
        KCDC::Features::Vfx::VfxService _vfxService;
        KCDC::Features::Light::LightService _lightService;
        KCDC::Features::Camera::CameraControlService _noClipService;
        KCDC::Features::Prop::PropGhostService _propGhostService;
        KCDC::Features::Prop::PropPlacerService _propPlacerService;

        // Borrowed pointers to the services declared above, in the order the
        // session ticks them. Built once in PostInit from feature_registry.cpp,
        // which is the only place that order is written down.
        std::vector<Core::IFeatureService *> _sessionFeatures;

        // The level the engine has loaded, learned on every level edge. The
        // fog hook writes it from the loading thread and the overlay reads it
        // from the render thread.
        mutable std::mutex _levelNameMutex;
        std::string _levelName;

        std::shared_ptr<Framework::Utils::States::Machine> _stateMachine;
        const Framework::Utils::States::IState *_lastLoggedState = nullptr;

        // Refcounted so one owner releasing the lock cannot re-enable input
        // for another that still holds it. The cursor is counted separately:
        // typing in the chat box holds the game's input without wanting one.
        int _controlsLocked = 0;
        int _cursorHolders  = 0;

        // Whether the chat input currently holds the control lock, so the
        // box opening and closing turns into one paired take and release.
        bool _chatInputHeld = false;

        // Sampled once per tick on the game thread; the chat's draw lambda
        // replays later and reads this rather than walking the GUI module.
        bool _nativeMenuOpen = false;

        // A single paired input hold follows the full-screen multiplayer
        // frontend across browser, direct-connect and connection screens.
        bool _frontendControlsHeld = false;

        // The hold a focused resource panel implies, derived once a tick.
        bool _webFocusControlsHeld = false;

        // CEF uses the OS cursor. Keeping it out of the ImGui draw list avoids
        // D3D12 back-buffer trails; the frontend's turn with it is one
        // IHardwareMouse increment, tracked here so it is released once.
        bool _frontendHardwareCursorHeld = false;

        // gEnv->pInput state captured when the lock was taken, so releasing it
        // restores what the game had rather than force-enabling input.
        bool _eventPostingWasEnabled = true;

        bool _serverDisconnectRequested = false;

        // Read from the render thread by the Present detour. Never lowered.
        std::atomic_bool _shuttingDown {false};

        void ApplyGameInputLock(bool locked);
        void UpdateCursorPresentation();
        void UpdateStateMachine();

        void UpdateDiscordPresence();

        /** Takes and releases the input hold a focused resource panel implies. */
        void ReconcileWebFocusLock();

        /** Opens, closes and gates the framework's chat box. */
        void UpdateChat();

      public:
        /** Game thread: the native escape menu or an apse tab is on screen. */
        bool IsNativeMenuOpen() const;

        void PostInit() override;
        void PreShutdown() override;
        void PostUpdate() override;
        void PostRender() override;

        /** Disconnects, stops the overlay, then hands over to the framework. */
        void Shutdown() override;

        bool IsShuttingDown() const noexcept {
            return _shuttingDown.load(std::memory_order_acquire);
        }

        void OnConnectionFinalized(float serverTickRate) override;
        Framework::Integrations::Client::InitialAssetProcessingDecision OnInitialAssetDownloadReady(std::uint64_t generation, const Framework::Integrations::Client::AssetDownloadStatus &status) override;
        void OnAssetsDownloadFinished(bool success) override;
        void OnResourceAssetPaksChanged() override;
        void OnInitialReplicationDownloaded() override;

        States::SessionLoadContext &GetSessionLoadContext() {
            return _sessionLoad;
        }
        void SetSessionStartupState(States::SessionStartupState *state) {
            _sessionStartupState = state;
        }
        void BeginSessionLoading();
        /** Ends the join attempt and lets the game finish its own load completion. */
        void CancelSessionLoading();
        void FailSessionLoading(const char *message);

        /** Holds the game's next load completion behind the session's readiness task. From LoadSessionLevel. */
        void ArmSessionHold();
        /** Lets the game's load completion run: the loading screen comes down and gameplay starts. */
        void ReleaseSessionHold();
        /** True while the session's readiness task holds the game's load completion. */
        bool IsSessionHeld() const;

        /** A load-completion observer began waiting on readiness. From the C_ReadinessObserver::Start detour. */
        void OnLoadReadinessAwaited();
        /** The game dispatched ESYSTEM_EVENT_LEVEL_GAMEPLAY_START. From the detour; only latches. */
        void OnLevelGameplayStarted();
        /** The game's session latch started (message 52) or ended (77). From the detours; only latches. */
        void OnGameSessionLatch(bool started, SDK::wh::S_GameSessionLatch *latch);

        /**
         * The menu came back over an abandoned load before the game's session
         * started. C_NewGameHelper still starts it from its own readiness wait
         * a second or two later; the next tick then ends it with the game's own
         * session-end, the call ReturnToMainMenuState makes, so the game is
         * left as a normal disconnect leaves it.
         */
        void EndGameSessionWhenItStarts();
        void OnSessionSpawnAnswered(std::uint64_t placement);
        void OnSessionPlacementSettled(std::uint64_t placement, bool success);

        /** Publishes the mod's own client bindings. Client scripting comes up only after a connection and is torn down with it, so this runs once per session. */
        void ModuleRegister(Framework::Scripting::Engine *engine) override;

        /** Holds the spawn barrier until SessionConnection has the level. */
        bool RequiresExplicitConnectionSpawnReady() const override {
            return true;
        }

        void OnConnectionClosed() override;

        /**
         * The server's admission gate sent the player a line while the
         * connection waits to be let in. Raises the frontend's admission card
         * over the menu backdrop: the game's own loading screen does not exist
         * yet, since no level is asked for before admission.
         */
        void OnAdmissionStatus(const std::string &message) override;

        /**
         * Takes the admission card down, if it is up, and pushes that to the
         * page at once. True when there was a card: the page still needs a
         * few frames to paint itself away, which SessionConnection gives it
         * before a level load freezes the view on its last frame.
         */
        bool EndAdmissionWait();

        /**
         * The server's streamed assets on the admission card, which brings the menu back over the
         * dial as an admission status does; nullopt takes the card's download part down.
         * EndAdmissionWait clears it with the rest of the card.
         */
        void ShowDownload(std::optional<Core::DownloadProgress> progress);

        /**
         * The launcher was started with a kcdc:// link. Called once, at the
         * end of Init; the link is only held here, and the main menu takes it
         * instead of waiting for the page.
         */
        void OnProtocolLaunch(const std::string &url) override;

        /** The launch link's connection, once: taking it clears it, so a later disconnect shows the menu rather than reconnecting. */
        std::optional<ConnectRequest> TakeLaunchConnection() {
            return std::exchange(_launchConnection, std::nullopt);
        }

        /** Speaks an incoming chat line over the body that sent it. */
        void OnChatMessageReceived(const Framework::Networking::RPC::ChatMessage &msg) override;

        Framework::Input::IInput *GetBaseInput() const override {
            return _input.get();
        }

        std::shared_ptr<KCDC::Common::GameInput> GetInput() const {
            return _input;
        }

        LocalEvents &GetLocalEvents() {
            return _localEvents;
        }

        BrandLogo &GetBrandLogo() {
            return _brandLogo;
        }

        /**
         * True while a screen of the game's own covers the HUD: the escape
         * menu and the tab pages, and every state the game suppresses its HUD
         * for -- dialogue, trade, cutscenes, minigames, photo mode, a fade.
         * The corner logo hides with them.
         */
        bool IsHudCovered() const;

        /**
         * True while a load still owns the view, read off the game's global UI
         * states rather than any one system: `LoadScreen`, which
         * C_UILoadingScreen::SetState blocks for every load and
         * ResetLoadingState releases as it tears the screen down, and then
         * `Fader`, which the fade every load ends with holds until the world
         * is fully shown (0x1808BB608 / 0x1808BB70C). The game's own HUD waits
         * for the same pair; so does everything the mod draws over play.
         */
        bool IsGameLoading() const;

        /** The runtime world builder, opened with F7 or through `MapEditor`. */
        KCDC::Features::Prop::MapEditor &GetMapEditor() {
            return _mapEditor;
        }

        KCDC::Common::Spawn::ActorSpawner &GetActorSpawner() {
            return _actorSpawner;
        }

        KCDC::Common::Spawn::PropSpawner &GetPropSpawner() {
            return _propSpawner;
        }

        StreamedAssets::StreamedAssetService &GetStreamedAssetService() {
            return _streamedAssetService;
        }

        KCDC::Features::World::WorldEnvironmentService &GetWorldEnvironmentService() {
            return _worldEnvironmentService;
        }

        KCDC::Features::Player::PlayerTeleporter &GetPlayerTeleporter() {
            return _playerTeleporter;
        }

        /** The mod's own compass marks, and later the server's and scripting's. */
        KCDC::Features::Map::MapBlipService &GetMapBlipService() {
            return _mapBlipService;
        }

        MultiplayerFrontend &GetMultiplayerFrontend() {
            return _multiplayerFrontend;
        }

        /** Drop and pickup bookkeeping spanning more than one ground stack. */
        KCDC::Features::Gathering::GatheringService &GetGatheringService() {
            return _gatheringService;
        }

        KCDC::Features::GroundItem::GroundItemService &GetGroundItemService() {
            return _groundItemService;
        }

        /** The replicated containers, and the native edges they answer to. */
        KCDC::Features::Stash::StashService &GetStashService() {
            return _stashService;
        }

        /** Door state and the local edges the engine's door hooks report. */
        KCDC::Features::Door::DoorService &GetDoorService() {
            return _doorService;
        }

        /** The level's gates, found by GUID and driven from the server's cycle. */
        KCDC::Features::Door::GateService &GetGateService() {
            return _gateService;
        }

        /** Everything about horses that is not about one horse: seats and the local ride. */
        KCDC::Features::Horse::HorseService &GetHorseService() {
            return _horseService;
        }

        /** Everything about dogs that is not about one dog: which brains this client must not run. */
        KCDC::Features::Dog::DogService &GetDogService() {
            return _dogService;
        }

        /** The replicated props, and the level edge that invalidates what they built. */
        KCDC::Features::Prop::PropService &GetPropService() {
            return _propService;
        }

        KCDC::Features::Area::AreaCollisionService &GetAreaCollisionService() {
            return _areaCollisionService;
        }

        KCDC::Features::LevelEdit::LevelEditService &GetLevelEditService() {
            return _levelEditService;
        }

        /** The mod's grip on the native clock and weather, and the session it is synchronized against. */
        KCDC::Features::World::WorldService &GetWorldService() {
            return _worldService;
        }

        /** Traces, ground probes and sphere queries, for this machine's own scripts and for the server, which has no world to look in. */
        KCDC::Features::World::WorldQueryService &GetWorldQueryService() {
            return _worldQueryService;
        }

        /** The emotes this peer plays, and the ones the server relays to it. */
        KCDC::Features::Social::EmoteService &GetEmoteService() {
            return _emoteService;
        }

        /** The wheel the emote key opens, and the only thing that drives the service above by hand. */
        KCDC::Features::Social::EmoteWheelService &GetEmoteWheelService() {
            return _emoteWheelService;
        }

        /** The player list the scoreboard key holds open, and the server roster behind it. */
        KCDC::Features::Social::ScoreboardService &GetScoreboardService() {
            return _scoreboardService;
        }

        /** Every ground marker this machine is showing: the decal nodes the replicated markers stream in. */
        /** Every quest the server wrote into this machine's journal. */
        KCDC::Features::Quest::QuestService &GetQuestService() {
            return _questService;
        }

        KCDC::Features::Marker::MarkerService &GetMarkerService() {
            return _markerService;
        }

        /** The free camera a client resource flies, and the body it carries. */
        KCDC::Features::Camera::CameraControlService &GetCameraControl() {
            return _noClipService;
        }

        /** Every preview this machine is drawing: brush render nodes that are in the world without being part of it. */
        KCDC::Features::Prop::PropGhostService &GetPropGhostService() {
            return _propGhostService;
        }

        /** The placement mode, when a resource has one running: a ghost under the crosshair and the input that confirms it. */
        KCDC::Features::Prop::PropPlacerService &GetPropPlacerService() {
            return _propPlacerService;
        }

        /** Every particle effect this machine is playing: its own resources', the server's bursts, and the replicated ones the interest grid streams in. */
        KCDC::Features::Light::LightService &GetLightService() { return _lightService; }

        KCDC::Features::Vfx::VfxService &GetVfxService() {
            return _vfxService;
        }

        /** The mod's grip on the game's audio, and the framework's voice output. */
        KCDC::Features::Social::VoiceService &GetVoiceService() {
            return _voiceService;
        }

        /** Everything about players that is not about one player. */
        KCDC::Features::Player::PlayerService &GetPlayerService() {
            return _playerService;
        }

        /** The server-decided status effects on the local player's body. */
        KCDC::Features::Player::PlayerBuffService &GetPlayerBuffService() {
            return _playerBuffService;
        }

        /** The local player's skills, stats and perks, as the server orders them, and the perks of everyone else's copy. */
        KCDC::Features::Player::PlayerProgressionService &GetPlayerProgressionService() {
            return _playerProgressionService;
        }

        /** Every shot in flight: the local ones reported and the relayed ones replayed. */
        KCDC::Features::Player::PlayerMissileService &GetPlayerMissileService() {
            return _playerMissileService;
        }

        /** The local player's native brewing, gated step by step on the server. */
        KCDC::Features::Crafting::AlchemyService &GetAlchemyService() {
            return _alchemyService;
        }

        /** The local player's native smithing, with the server holding the materials and the product. */
        KCDC::Features::Crafting::SmithingService &GetSmithingService() {
            return _smithingService;
        }

        /** The owner's item identities, and the grants the server sends it. */
        KCDC::Features::Player::PlayerInventoryService &GetPlayerInventoryService() {
            return _playerInventoryService;
        }

        /** What the locomotion hooks impose on every replicated NPC and dog. */
        KCDC::Features::Npc::NpcLocomotionService &GetNpcLocomotionService() {
            return _npcLocomotionService;
        }

        KCDC::Features::Player::PlayerDuelService &GetPlayerDuelService() {
            return _playerDuelService;
        }

        /** The ring of strings the mod's own localization keys resolve to. */
        KCDC::Features::UI::UiService &GetUiService() {
            return _uiService;
        }

        /** The Scaleform screens client resources have open at this machine. */
        KCDC::Features::UI::NativeUIService &GetNativeUIService() {
            return _nativeUIService;
        }

        /** What the map screen and its marker report happened on this client. */
        KCDC::Features::Map::MapService &GetMapService() {
            return _mapService;
        }

        /**
         * Also false while a native screen holds input focus.
         *
         * The framework's own gate knows about the chat box and about a
         * focused web view, which are the two surfaces it owns. Native
         * screens are the third, and a key bind that kept firing underneath
         * one would reach both the script that opened it and the game.
         */
        bool IsLocalInputAvailable() const override;

        /** Ticks every registered session participant, in registration order. */
        void UpdateSessionFeatures();

        /** Drops what the session left on every registered participant. */
        void ResetSessionFeatures();

        /** Invalidates native world handles after a level reset or save load, including same-level loads. */
        void NotifyNativeContextChanged();

        /** The level the connected server named, or empty if it named none. */
        std::string GetSessionLevelName() const;

        /** What the connected server calls itself, or empty if it named nothing. */
        std::string GetSessionServerName() const;

        /** The DLCs the connected server requires, as a `KCDC::Shared::Game` mask. Zero when it requires none. */
        std::uint16_t GetSessionRequiredDLC() const;

        /** Whether the connected server shows the player list. True unless it turned it off, including a server too old to say. */
        bool IsSessionScoreboardEnabled() const;

        /** Whether the connected server reveals the whole map. True unless it turned it off, including a server too old to say, and offline. */
        bool IsSessionMapRevealed() const;

        /** Always allowed offline; multiplayer requires a grant for this connection. */
        bool IsMapEditorEnabled() const;

        /** Whether F7 may open or close the editor right now. */
        bool CanToggleMapEditor() const;

        /** Starts loading `levelName`; false while the game cannot take it. `worldBuilder` loads it offline, with no session hold to release. */
        bool LoadSessionLevel(const std::string &levelName, bool worldBuilder = false);

        /** Game-thread offline switch with catalog/DLC checks and the normal load lifecycle. Discards the old project only when accepted. */
        bool RequestOfflineBuilderLevel(const std::string &levelName, std::string &error);

        /** The level the menu chose for the offline World Builder; WorldBuilderState loads it. */
        void SetOfflineBuilderLevel(std::string level) {
            _offlineBuilderLevel = std::move(level);
        }
        const std::string &GetOfflineBuilderLevel() const {
            return _offlineBuilderLevel;
        }

        /** Gameplay has started on the offline World Builder's level. */
        bool IsOfflineBuilderReady() const {
            return _offlineBuilder && _offlineBuilderReady;
        }

        /** Closes the editor, forgets its scene and puts the menu backdrop back. The page itself is MainMenuState's. */
        void EndOfflineBuilder();

        /** Whether the menu is being entered from the World Builder rather than from a session. Clears it. */
        bool ConsumeOfflineBuilderEnded() {
            return std::exchange(_offlineBuilderEnded, false);
        }

        /** Opens the session from the completed loading transaction. */
        bool OpenSession();

        /** Puts the menu backdrop back over whatever level is loaded. */
        void ReturnToMenu();

        /** Game-thread request; teardown runs after native UI callbacks return. */
        void RequestServerDisconnect() {
            _serverDisconnectRequested = true;
        }

        /** Shows the frontend without taking a second input/cursor hold. */
        void ShowMultiplayerFrontend();

        /** Hides the frontend and releases only the frontend's input hold. */
        void HideMultiplayerFrontend();

        bool IsMultiplayerFrontendVisible() const {
            return _multiplayerFrontend.IsVisible();
        }

        /**
         * True while CEF owns keyboard input: the frontend or a resource's
         * panel. The frontend's own flags stay in because its
         * view is only focused to match them later in the tick.
         */
        bool HasExclusiveWebInput() const {
            const Framework::GUI::Manager *web = GetWebManager();
            return _multiplayerFrontend.IsVisible() || (web != nullptr && web->IsAnyViewFocused());
        }

        /** Uploads CEF surfaces inside the active D3D12 command list. */
        void RenderWebViews();

        /**
         * The three shipped levels reuse one coordinate space, so world
         * coordinates only name a place once the level is known.
         * `ILevelSystem::GetCurrentLevel` would answer it, but the chain from
         * there to the name runs through two interfaces whose layouts are not
         * modeled. The name arrives for free instead: the fog of war is
         * interned per level, so `C_FOWManager::GetOrCreateFogOfWar` is handed
         * it on level edges and ordinary lookups. Context invalidation comes
         * separately from the manager's Reset and Load paths.
         */
        void SetCurrentLevelName(const char *name);

        /** The level pak name, e.g. `trosecko`. Empty before the first level. */
        std::string GetCurrentLevelName() const;

        /**
         * Takes or releases a hold on the game's own input.
         *
         * withCursor also presents a cursor, which most holders want and a
         * chat line being typed does not.
         */
        void LockControls(bool lock, bool withCursor = true);

        bool AreControlsLocked() const {
            return _controlsLocked > 0;
        }

        /** The count `LockControls` keeps with `withCursor`, for a holder that wants a pointer and not the input. */
        void HoldCursor(bool hold);

        bool IsCursorHeld() const {
            return _cursorHolders > 0;
        }
    };

    /** Native handles recovered by the render hooks. Borrowed; the engine owns them. */
    struct Globals {
        HWND window                      = nullptr;
        ID3D12Device *device             = nullptr;
        ID3D12CommandQueue *commandQueue = nullptr;
        IDXGISwapChain3 *swapChain       = nullptr;

        // The mod's own directory, handed to the entry point by the launcher.
        // crashpad_handler.exe, the Sentry cache and the logs all sit beneath it.
        std::string projectPath;
    };

    extern Globals gGlobals;
    extern std::unique_ptr<Application> gApplication;
} // namespace KCDC::Core
