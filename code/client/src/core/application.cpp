#include "application.h"
#include "features/player/player_rock_service.h"

#include "crash_context.h"
#include "crash_reporting.h"
#include "feature_registry.h"
#include "register_entities.h"

#include <external/imgui/wrapper.h>
#include <logging/logger.h>

#include <algorithm>
#include <string_view>

#include "core/boot/boot_state.h"
#include "core/dlc.h"
#if defined(KCDC_AGENT_TESTING)
#include "core/boot/agent_testing.h"
#endif
#include "core/input/native_keybinds.h"
#include "core/ui/native_ui/native_ui_module.h"
#include "features/animation/animation_service.h"
#include "features/cook_pot/cook_pot_service.h"
#include "features/crafting/custom_recipe_module.h"
#include "features/door/door_service.h"
#include "features/gizmos/gizmos_service.h"
#include "features/ground_item/ground_item_service.h"
#include "features/npc/npc_service.h"
#include "features/player/player_buff_service.h"
#include "features/player/player_inventory_service.h"
#include "features/player/player_missile_service.h"
#include "features/player/player_service.h"
#include "features/social/social_emote_service.h"
#include "features/social/social_voice_service.h"
#include "features/stash/stash_service.h"
#include "features/world/world_service.h"
#include "core/presentation_module.h"

#include "shared/features/crafting/alchemy_message.h"
#include "shared/features/crafting/smithing_rpc.h"
#include "shared/features/world_builder/world_builder_access.h"
#include "shared/game/dlc_catalog.h"
#include "shared/game/levels.h"

#include <core_modules.h>
#include <networking/network_peer.h>

#include "scripting_registry.h"

#include "states/game_setup.h"
#include "states/initialize.h"
#include "states/main_menu.h"
#include "states/session_connected.h"
#include "states/session_connection.h"
#include "states/session_disconnection.h"
#include "states/shutdown.h"
#include "states/states.h"
#include "states/world_builder.h"

#include "core/ui/native_menu.h"

#include "sdk/cryengine/action/i_action_map_manager.h"
#include "sdk/cryengine/action/i_game_framework.h"
#include "sdk/cryengine/input/i_hardware_mouse.h"
#include "sdk/cryengine/input/i_input.h"
#include "sdk/cryengine/system/i_console.h"
#include "sdk/wh/c_wh_global_environment.h"
#include "sdk/wh/game/c_camera_manager.h"
#include "sdk/wh/guimodule/c_gui_module.h"
#include "sdk/wh/guimodule/c_ui_loading_screen.h"
#include "sdk/wh/guimodule/c_ui_menu.h"
#include "sdk/wh/guimodule/c_ui_save_load.h"
#include "sdk/wh/guimodule/c_ui_state_blockers.h"

namespace KCDC::Core {
    Globals gGlobals;
    std::unique_ptr<Application> gApplication = nullptr;

    void Application::PostInit() {
        _input = std::make_shared<KCDC::Common::GameInput>();
        _actorSpawner.LoadSoulCatalog();

        // Both peers must register the same types before any replica arrives.
        RegisterEntities();

        KCDC::Features::GroundItem::GroundItemService::Install();
        KCDC::Features::Stash::StashService::Install();
        KCDC::Features::Gathering::GatheringService::Install();
        KCDC::Features::CookPot::CookPotService::Install();
        KCDC::Features::Player::PlayerService::Install();
        KCDC::Features::Player::PlayerInventoryService::Install();
        KCDC::Features::UI::UiService::Install();
        Framework::CoreModules::GetNetworkPeer()->RegisterRPC<KCDC::Shared::RPC::SetWorldBuilderAccess>([this](const KCDC::Shared::RPC::SetWorldBuilderAccess &access, MafiaNet::Packet *) {
            if (access.IsSane()) {
                _worldBuilderAccess = access.enabled;
                if (!access.enabled) {
                    _mapEditor.SetOpen(false);
                }
            }
        });
        Framework::CoreModules::GetNetworkPeer()->RegisterRPC<KCDC::Shared::RPC::AlchemyResponse>([this](const KCDC::Shared::RPC::AlchemyResponse &response, MafiaNet::Packet *) {
            if (response.valid) {
                _alchemyService.OnServerResponse(response.body);
            }
        });
        KCDC::Features::Crafting::CustomRecipeModule::Install();
        Framework::CoreModules::GetNetworkPeer()->RegisterRPC<KCDC::Shared::RPC::SmithingVerdict>([this](const KCDC::Shared::RPC::SmithingVerdict &verdict, MafiaNet::Packet *) {
            if (verdict.IsSane()) {
                _smithingService.OnVerdict(verdict);
            }
        });
        Framework::CoreModules::GetNetworkPeer()->RegisterRPC<KCDC::Shared::RPC::SmithingClosed>([this](const KCDC::Shared::RPC::SmithingClosed &closed, MafiaNet::Packet *) {
            if (closed.IsSane()) {
                _smithingService.OnClosed(closed);
            }
        });
        KCDC::Features::Player::PlayerBuffService::Install();
        KCDC::Features::Player::PlayerProgressionService::Install();
        KCDC::Features::Player::PlayerMissileService::Install();
        KCDC::Features::Player::PlayerRockService::Install();
        KCDC::Features::Player::PlayerDuelService::Install();
        KCDC::Features::Social::EmoteService::Install();
        KCDC::Features::Social::ScoreboardService::Install();
        KCDC::Core::PresentationModule::Install();
        KCDC::Features::Door::DoorService::Install();
        KCDC::Features::World::WorldQueryService::Install();
        KCDC::Features::Prop::PropService::Install();
        KCDC::Features::Vfx::VfxService::Install();
        KCDC::Features::Camera::CameraControlService::Install();
        KCDC::Features::Quest::QuestService::Install();
        KCDC::Features::Npc::NpcService::Install();
        KCDC::Features::Animation::AnimationService::Install();

        // Built once the replica types are registered, and never rebuilt: the
        // entries are members of this object. feature_registry.cpp is the only
        // place the session's update order is written down.
        BuildSessionFeatureRegistry(*this, _sessionFeatures);

        if (Framework::External::Discord::Wrapper *presence = GetPresence()) {
            KCDC::Core::DiscordPresence::Install(*presence);
        }

        _stateMachine = std::make_shared<Framework::Utils::States::Machine>();
        _stateMachine->RegisterState<States::InitializeState>();
        _stateMachine->RegisterState<States::GameSetupState>();
        _stateMachine->RegisterState<States::MainMenuState>();
        _stateMachine->RegisterState<States::SessionConnectionState>();
        _stateMachine->RegisterState<States::SessionResourcesState>();
        _stateMachine->RegisterState<States::SessionReplicationState>();
        _stateMachine->RegisterState<States::SessionSpawnState>();
        _stateMachine->RegisterState<States::SessionFinalizeState>();
        _stateMachine->RegisterState<States::SessionConnectedState>();
        _stateMachine->RegisterState<States::SessionDisconnectionState>();
        _stateMachine->RegisterState<States::ShutdownState>();
        _stateMachine->RegisterState<States::WorldBuilderState>();
        _stateMachine->RequestNextState(States::Initialize);

        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("KCDC application initialized");
    }

    void Application::Shutdown() {
        _shuttingDown.store(true, std::memory_order_release);

        // First: other clients drop the body as soon as this lands.
        if (Features::World::WorldService::IsSessionActive()) {
            if (const auto result = GetNetworkingEngine()->GetNetworkClient()->Disconnect(); !result) {
                Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("Disconnect on quit failed: {}", result.GetError().message);
            }
        }

        // Waits out a frame already drawing the overlay on the render thread.
        { const std::lock_guard<std::mutex> drain(Boot::gBootState.presentResizeMutex); }

        Framework::Integrations::Client::Instance::Shutdown();
    }

    void Application::PreShutdown() {
        CancelSessionLoading();
        // Unregisters from the readiness service while CSystem still exists.
        _sessionHold.reset();
        // Before the framework's voice client shuts down, so the streams and
        // the recording stop while there is still an FMOD to stop them on --
        // and before the game's own teardown, which saves the profile through
        // every listener still registered.
        _voiceService.Shutdown();
        _streamedAssetService.Shutdown();

        _nativeUIService.Reset();
        KCDC::Core::NativeUIModule::Get().Shutdown();
        GetChatBox().CloseInput(false);
        if (_chatInputHeld) {
            _chatInputHeld = false;
            LockControls(false, /*withCursor=*/false);
        }
        if (_webFocusControlsHeld) {
            _webFocusControlsHeld = false;
            LockControls(false);
        }
        _multiplayerFrontend.SetGameHudSuppressed(false);
        HideMultiplayerFrontend();
        _multiplayerFrontend.Shutdown(*this);

        // Releases the camera, the input hold and every object it placed.
        _mapEditor.Shutdown();

        // The other holder of that one camera; a shutdown can arrive without
        // the session sweep having ended the flight.
        _noClipService.Reset();

        // Every mark the mod owns holds a native reference on the game's heap.
        _mapBlipService.RemoveAll();
        _mapBlipService.Shutdown();
        _mapService.Reset();

        if (_stateMachine) {
            _stateMachine->RequestNextState(States::Shutdown);
            // Exit, Next and Enter each take a tick.
            UpdateStateMachine();
            UpdateStateMachine();
            UpdateStateMachine();
        }
    }

    void Application::UpdateStateMachine() {
        _stateMachine->Update();

        const auto *current = _stateMachine->GetCurrentState();
        if (current != _lastLoggedState) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("State '{}' -> '{}'", _lastLoggedState ? _lastLoggedState->GetName() : "<none>", current ? current->GetName() : "<none>");
            _lastLoggedState = current;
        }
    }

    void Application::PostUpdate() {
        // Read the live native setting on the game thread. The shared overlay
        // converts sRGB to scRGB, where UI white at scale 1 is 80 nits.
        GetImGUI()->SetHDRUIBrightnessScale(SDK::IConsole::GetFloatCVar("r_HDRUIBrightness", 80.0f) / 80.0f);

        if (_abandonedGameSessionLatch != nullptr) {
            SDK::wh::S_GameSessionLatch *latch = _abandonedGameSessionLatch;
            _abandonedGameSessionLatch         = nullptr;
            _endAbandonedGameSession           = false;
            latch->End();
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Ended the game session the abandoned load started behind the menu");
        }

        if (_serverDisconnectRequested) {
            _serverDisconnectRequested = false;
            if (KCDC::Features::World::WorldService::IsSessionActive()) {
                if (const auto result = GetNetworkingEngine()->GetNetworkClient()->Disconnect(); !result) {
                    Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("Disconnect failed: {}", result.GetError().message);
                }
            }
        }

        // The game's own menu keeps running behind the mod's page; only what
        // it draws is taken away, and the movie can restore that on its own.
        NativeMenu::HideFrontEndContent();

        // Replica destruction can also arrive from the loading-time peer pump,
        // where the entity system is part-way through its own work. Settle its
        // copied body identities only here, on an ordinary game-thread tick.
        _playerService.DrainBodyDespawns();

        // Same pump, same thread -- and the last props go when the session does.
        _propService.DrainDespawns();

        // Before the state machine, because the session sweep it runs is
        // where a mod key is read: a press this frame has to be visible to
        // the feature that acts on it, not to the next frame's.
        NativeKeybinds::Get().Update();

        // Networking ran earlier in Instance::Update, so this frame's replicas
        // are already constructed.
        UpdateStateMachine();

        UpdateDiscordPresence();

        // A generation the server published while connected: its new paks mount a pak a frame.
        _streamedAssetService.Update();

        // After the state machine, so the local player this reads is the one
        // this frame settled on. The framework pumped voice earlier in
        // Instance::Update, so the listener and push-to-talk state published
        // here are the next frame's -- which is what publishing a pose means
        // anyway, and is a frame the ear cannot hear.
        _voiceService.Update();

        // CEF comes up only after the present hook has recovered the native
        // renderer. Menu intent is retained until then.
        _multiplayerFrontend.Update(*this);

        // Before the re-assert below, so a panel focused this frame holds
        // input on this frame rather than the next.
        ReconcileWebFocusLock();

        // The game re-enables input on its own across level loads and UI
        // screens, so the hold is re-asserted every frame instead of once.
        if (AreControlsLocked()) {
            if (auto *actionMaps = SDK::IActionMapManager::GetInstance()) {
                actionMaps->Enable(false, false);
            }
            if (auto *input = SDK::IInput::GetInstance()) {
                input->EnableEventPosting(false);
            }
        }

        // Drains the overlay's spawn requests on the game thread, which is the
        // only thread allowed to touch the entity and actor systems.
        _actorSpawner.Update();
        _propSpawner.Update();
        _playerTeleporter.Update();

        // Offline there is no session, so no feature sweep: tick the services
        // the editor draws through -- its effects, its ghosts, and the native
        // gizmo renderer behind area outlines and selection boxes.
        if (_offlineBuilderReady) {
            _vfxService.Update();
            _lightService.Update();
            _propGhostService.Update();
            Features::Gizmos::GizmosService::Get().Update();
        }

        // Owns the camera, the placed objects and the physics queries behind
        // them, all of which are game-thread only.
        if (_input && _input->IsKeyPressed(FW_KEY_F7) && CanToggleMapEditor()) {
            _mapEditor.SetOpen(!_mapEditor.IsOpen());
        }
        _mapEditor.Update();

        // Same rule: CTimeOfDay and the weather system are game-thread only.
        _worldEnvironmentService.Update();

        // Reconciles the mod's compass marks, and puts back anything the
        // game's own compass reset dropped.
        _mapBlipService.Update();

        // Reconciles native input focus and hands this frame's movie events
        // to whatever asked for them.
        _nativeUIService.Tick();

        UpdateChat();

        // Replayed by ImGUI::Wrapper::Update() on the next frame.
        if (GetImGUI() && GetImGUI()->IsInitialized()) {
            GetImGUI()->PushWidget([this]() {
                if (auto *web = GetWebManager(); web && web->IsInitialized()) {
                    web->SubmitImGuiDraws();
                }
                _mapEditor.Render();

                // The frontend, the game's own menus and its loading screen
                // cover the screen, and a line typed under them is not one.
                if (GetChatBox().IsVisible() && !IsMultiplayerFrontendVisible() && !_nativeMenuOpen && !IsGameLoading()) {
                    GetChatBox().Render();
                }
            });

            UpdateCursorPresentation();
        }

        // Clears this frame's pressed/released edges; must run after every
        // consumer above.
        if (_input) {
            _input->Update();
        }
    }

    bool Application::IsLocalInputAvailable() const {
        return !AreControlsLocked() && !IsMultiplayerFrontendVisible() && !_nativeMenuOpen && !KCDC::Core::NativeUIModule::Get().IsFocusRequested() && Framework::Integrations::Client::Instance::IsLocalInputAvailable();
    }

    void Application::PostRender() {}

    void Application::RenderWebViews() {
        if (auto *web = GetWebManager(); web && web->IsInitialized()) {
            web->Render();
        }
    }

    /**
     * The framework owns the chat box, its session flag and the send path;
     * this owns when it takes the keyboard.
     */
    void Application::UpdateChat() {
        auto &chatBox   = GetChatBox();
        _nativeMenuOpen = IsNativeMenuOpen();

        if (chatBox.IsInputActive() && (!chatBox.IsVisible() || IsMultiplayerFrontendVisible() || _nativeMenuOpen || HasExclusiveWebInput())) {
            chatBox.CloseInput(false);
        }

        // ConsumeLockedEscape takes Escape away from ImGui while the chat
        // holds the controls, so the box never sees its own cancel key.
        if (chatBox.IsInputActive() && _input && _input->IsKeyPressed(FW_KEY_ESCAPE)) {
            chatBox.CloseInput(false);
        }

        if (_input && _input->IsKeyPressed('T') && chatBox.IsVisible() && !chatBox.IsInputActive() && !AreControlsLocked() && !IsMultiplayerFrontendVisible() && !_nativeMenuOpen && IsLocalInputAvailable()) {
            chatBox.OpenInput();
        }

        if (chatBox.IsInputActive() != _chatInputHeld) {
            _chatInputHeld = chatBox.IsInputActive();
            LockControls(_chatInputHeld, /*withCursor=*/false);
        }
    }

    /**
     * Two screens, two signals. The escape menu is a C_UIMenu state; the tab
     * pages -- inventory, map, codex, journal -- are the Apse, which blocks
     * E_UIState::Apse from its open (0x181F4AA00) until its teardown
     * (0x180C42D98) and never touches the menu's state.
     */
    bool Application::IsNativeMenuOpen() const {
        const SDK::wh::guimodule::C_UIStateBlockers *blockers = SDK::wh::guimodule::C_UIStateBlockers::GetInstance();
        if (blockers != nullptr && blockers->IsHeld(SDK::wh::guimodule::E_UIState::Apse)) {
            return true;
        }

        auto *environment = SDK::wh::C_GlobalEnvironment::GetInstance();
        auto *guiModule   = environment != nullptr ? environment->GetGUIModule() : nullptr;
        auto *menu        = guiModule != nullptr ? guiModule->GetMenu() : nullptr;
        return menu != nullptr && menu->IsOpen();
    }

    bool Application::IsGameLoading() const {
        const SDK::wh::guimodule::C_UIStateBlockers *blockers = SDK::wh::guimodule::C_UIStateBlockers::GetInstance();
        return blockers != nullptr && (blockers->IsHeld(SDK::wh::guimodule::E_UIState::LoadScreen) || blockers->IsHeld(SDK::wh::guimodule::E_UIState::Fader));
    }

    bool Application::IsHudCovered() const {
        if (IsMultiplayerFrontendVisible() || IsNativeMenuOpen()) {
            return true;
        }

        const SDK::wh::guimodule::C_UIStateBlockers *blockers = SDK::wh::guimodule::C_UIStateBlockers::GetInstance();
        if (blockers == nullptr) {
            return false;
        }

        // The states that take the screen over. Left out are the ones that
        // only mute one HUD element (the Block* family, HitReaction), and
        // Saving, whose indicator plays over normal play.
        constexpr SDK::wh::guimodule::E_UIState kCovering[] = {
            SDK::wh::guimodule::E_UIState::Fader,
            SDK::wh::guimodule::E_UIState::Dialog,
            SDK::wh::guimodule::E_UIState::Cutscene,
            SDK::wh::guimodule::E_UIState::CutsceneCapture,
            SDK::wh::guimodule::E_UIState::TextCutscene,
            SDK::wh::guimodule::E_UIState::GameOver,
            SDK::wh::guimodule::E_UIState::Menu,
            SDK::wh::guimodule::E_UIState::LoadScreen,
            SDK::wh::guimodule::E_UIState::Shop,
            SDK::wh::guimodule::E_UIState::ItemSelection,
            SDK::wh::guimodule::E_UIState::ItemTransfer,
            SDK::wh::guimodule::E_UIState::FastTravel,
            SDK::wh::guimodule::E_UIState::SkipTimeDialog,
            SDK::wh::guimodule::E_UIState::SkipTimeRunning,
            SDK::wh::guimodule::E_UIState::MinigameAlchemy,
            SDK::wh::guimodule::E_UIState::MinigameHerbGathering,
            SDK::wh::guimodule::E_UIState::MinigameHoleDigging,
            SDK::wh::guimodule::E_UIState::MinigamePickpocketing,
            SDK::wh::guimodule::E_UIState::MinigamePickpocketingCharge,
            SDK::wh::guimodule::E_UIState::MinigameReading,
            SDK::wh::guimodule::E_UIState::MinigameLockPicking,
            SDK::wh::guimodule::E_UIState::MinigameSharpening,
            SDK::wh::guimodule::E_UIState::MinigameDice,
            SDK::wh::guimodule::E_UIState::MinigameArchery,
            SDK::wh::guimodule::E_UIState::MinigameBlacksmithing,
            SDK::wh::guimodule::E_UIState::MinigameStoneThrowing,
            SDK::wh::guimodule::E_UIState::MinigameBattleArchery,
            SDK::wh::guimodule::E_UIState::MinigameForgeBuilder,
            SDK::wh::guimodule::E_UIState::HelpOverlay,
            SDK::wh::guimodule::E_UIState::HorseInspect,
            SDK::wh::guimodule::E_UIState::CrimeList,
            SDK::wh::guimodule::E_UIState::Photomode,
            SDK::wh::guimodule::E_UIState::CameraWithoutOverlay,
            SDK::wh::guimodule::E_UIState::DialogBarber,
            SDK::wh::guimodule::E_UIState::DialogShieldPainting,
        };
        return std::ranges::any_of(kCovering, [blockers](SDK::wh::guimodule::E_UIState state) {
            return blockers->IsHeld(state);
        });
    }

    void Application::OnConnectionFinalized(float serverTickRate) {
        if (_sessionLoad.active && _sessionStartupState) {
            _sessionStartupState->OnConnectionReady();
        }
        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Connected (server tick rate {:.2f} Hz)", serverTickRate);
        CrashContext::Breadcrumb("session", "Connection finalized");
    }

    Framework::Integrations::Client::InitialAssetProcessingDecision Application::OnInitialAssetDownloadReady(std::uint64_t, const Framework::Integrations::Client::AssetDownloadStatus &) {
        // A cache hit can arrive inside the loading-time peer pump, while the
        // level is still being built. Scripts start only from
        // SessionResourcesState::OnEnter, on a normal tick after the load.
        if (_sessionLoad.active && _sessionLoad.levelLoaded && _sessionStartupState) {
            _sessionStartupState->OnLevelAndAssetsReady();
        }
        return Framework::Integrations::Client::InitialAssetProcessingDecision::Defer;
    }

    void Application::OnAssetsDownloadFinished(bool success) {
        if (success) {
            return;
        }
        // The Framework names the failure (a stall, an exhausted retry budget, a refused file) and
        // closes the connection right after; the reason is kept for the menu the player lands on.
        _downloadFailure = GetAssetDownloadStatus().failure;
        if (_downloadFailure.empty()) {
            _downloadFailure = "The server's resources could not be downloaded.";
        }
        FailSessionLoading(_downloadFailure.c_str());
    }

    void Application::OnResourceAssetPaksChanged() {
        // A refresh or a stop mid-session; the first set is mounted by the join gate.
        _streamedAssetService.OnResourceAssetPaksChanged();
    }

    void Application::OnInitialReplicationDownloaded() {
        if (_sessionLoad.active && _sessionStartupState) {
            _sessionStartupState->OnReplicationReady();
        }
    }

    void Application::BeginSessionLoading() {
        _worldBuilderAccess = false;
        _mapEditor.SetOpen(false);
        _sessionLoad         = {};
        _sessionLoad.active  = true;
        _sessionStartupState = nullptr;
    }

    void Application::CancelSessionLoading() {
        _sessionLoad.active  = false;
        _sessionStartupState = nullptr;
        // The game finishes its own load completion once the hold lets go:
        // the loading screen comes down and gameplay starts, exactly as it
        // would have. SessionDisconnection waits for that before it puts the
        // menu backdrop back.
        ReleaseSessionHold();
    }

    void Application::ArmSessionHold() {
        // Built once, in the readiness service's own task type, and kept: the
        // game's streaming systems keep theirs for the process lifetime too.
        if (!_sessionHold) {
            _sessionHold.emplace(SDK::ScopedCryString("SessionJoin"), States::kSessionHoldCategory);
        }
        _sessionHold->SetPending(1);
    }

    void Application::ReleaseSessionHold() {
        if (_sessionHold && !_sessionHold->IsReady()) {
            _sessionHold->SetPending(0);
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Released the session's readiness hold");
        }
    }

    bool Application::IsSessionHeld() const {
        return _sessionHold && !_sessionHold->IsReady();
    }

    void Application::FailSessionLoading(const char *message) {
        if (!_sessionLoad.active) {
            return;
        }
        _sessionLoad.error = message;
        CancelSessionLoading();
        // Never disconnect, run scripts or alter native UI from a detour.
        // SessionDisconnection owns those operations on its normal entry.
        _stateMachine->RequestNextState(States::SessionDisconnection);
    }

    void Application::OnLoadReadinessAwaited() {
        if (!_sessionLoad.active || !IsSessionHeld() || _sessionLoad.levelLoaded) {
            return;
        }
        _sessionLoad.levelLoaded = true;
        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Session level loaded; the game waits on the session's readiness hold");
        if (_sessionStartupState && IsInitialAssetProcessingDeferred()) {
            _sessionStartupState->OnLevelAndAssetsReady();
        }
    }

    void Application::OnLevelGameplayStarted() {
        if (_offlineBuilder) {
            _offlineBuilderReady = true;
            return;
        }
        _sessionLoad.gameplayStarted = true;
        // Behind the readiness wait, which the session's load completion
        // starts without the game's 50 s timeout (LoadReadinessTimeout): a
        // long download must not be cut off by it, and the join's own
        // deadlines refuse a stuck one. Gameplay starting while still held
        // means something released the wait anyway (an observer disabled by
        // wh_sys_ReadinessObserverEnabled); carrying on would leave the
        // player in a world with no session.
        if (_sessionLoad.active && IsSessionHeld()) {
            FailSessionLoading("The server did not complete the join in time. It may be running a different build of the mod.");
        }
        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("The game started gameplay (load completion finished)");
        _localEvents.GameplayStarted();
    }

    void Application::OnGameSessionLatch(bool started, SDK::wh::S_GameSessionLatch *latch) {
        if (started) {
            _sessionLoad.gameSessionStarted = true;
            if (_endAbandonedGameSession) {
                _abandonedGameSessionLatch = latch;
            }
        }
        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("The game session {} (module message {})", started ? "started" : "ended", started ? 52 : 77);
    }

    void Application::EndGameSessionWhenItStarts() {
        _endAbandonedGameSession   = true;
        _abandonedGameSessionLatch = nullptr;
    }

    void Application::OnSessionSpawnAnswered(std::uint64_t placement) {
        if (_sessionLoad.active && _sessionStartupState) {
            _sessionStartupState->OnSpawnAnswered(placement);
        }
    }

    void Application::OnSessionPlacementSettled(std::uint64_t placement, bool success) {
        if (_sessionLoad.active && _sessionStartupState) {
            _sessionStartupState->OnPlacementSettled(placement, success);
        }
    }

    std::string Application::GetSessionLevelName() const {
        const auto &config = GetServerConfig();
        const auto level   = config.find("level");
        return level != config.end() && level->is_string() ? level->get<std::string>() : std::string {};
    }

    std::string Application::GetSessionServerName() const {
        const auto &config = GetServerConfig();
        const auto name    = config.find("name");
        return name != config.end() && name->is_string() ? name->get<std::string>() : std::string {};
    }

    void Application::UpdateDiscordPresence() {
        Framework::External::Discord::Wrapper *presence = GetPresence();
        if (presence == nullptr || !presence->IsInitialized()) {
            return;
        }

        const Framework::Utils::States::IState *current = _stateMachine->GetCurrentState();
        KCDC::Core::DiscordPresence::Phase phase        = KCDC::Core::DiscordPresence::Phase::Starting;
        switch (current != nullptr ? current->GetId() : States::Initialize) {
        case States::MainMenu: phase = KCDC::Core::DiscordPresence::Phase::MainMenu; break;
        case States::SessionConnection:
        case States::SessionResources:
        case States::SessionReplication:
        case States::SessionSpawn:
        case States::SessionFinalize: phase = KCDC::Core::DiscordPresence::Phase::Joining; break;
        case States::SessionConnected: phase = KCDC::Core::DiscordPresence::Phase::Playing; break;
        case States::SessionDisconnection: phase = KCDC::Core::DiscordPresence::Phase::Leaving; break;
        case States::WorldBuilder: phase = KCDC::Core::DiscordPresence::Phase::WorldBuilder; break;
        default: break;
        }

        const bool builder = phase == KCDC::Core::DiscordPresence::Phase::WorldBuilder;
        _discordPresence.Update(*presence, phase, builder ? std::string {} : GetSessionServerName(), builder ? GetCurrentLevelName() : GetSessionLevelName());
    }

    std::uint16_t Application::GetSessionRequiredDLC() const {
        const auto &config   = GetServerConfig();
        const auto requested = config.find("required_dlc");
        if (requested == config.end() || !requested->is_array()) {
            return 0;
        }

        std::uint16_t required = 0;
        for (const auto &entry : *requested) {
            if (!entry.is_string()) {
                continue;
            }
            // An unknown name is one this build's catalog predates, not a
            // requirement to fail on.
            const std::string name = entry.get<std::string>();
            if (const KCDC::Shared::Game::DLCEntry *dlc = KCDC::Shared::Game::FindDLCByName(name); dlc != nullptr) {
                required |= KCDC::Shared::Game::DLCBit(dlc->id);
            }
            else {
                Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("The server requires a DLC this build does not know: '{}'", name);
            }
        }
        return required;
    }

    bool Application::IsSessionScoreboardEnabled() const {
        const auto &config = GetServerConfig();
        const auto enabled = config.find("scoreboard");
        return enabled == config.end() || !enabled->is_boolean() || enabled->get<bool>();
    }

    bool Application::IsSessionMapRevealed() const {
        const auto &config  = GetServerConfig();
        const auto revealed = config.find("reveal_map");
        return revealed == config.end() || !revealed->is_boolean() || revealed->get<bool>();
    }

    bool Application::CanToggleMapEditor() const {
        // Closing is always fine; opening wants the player's controls free and nothing else holding the keyboard.
        return IsMapEditorEnabled() && !IsGameLoading() && (_mapEditor.IsOpen() || !AreControlsLocked()) && !IsMultiplayerFrontendVisible() && !IsNativeMenuOpen() && !HasExclusiveWebInput() && !NativeUIModule::Get().IsFocusRequested() && !GetChatBox().IsInputActive();
    }

    bool Application::IsMapEditorEnabled() const {
        const bool multiplayer = GetNetworkingEngine()->GetNetworkClient()->GetConnectionState() != Framework::Networking::PeerState::DISCONNECTED || (_sessionLoad.active && !_offlineBuilder);
        return !multiplayer || _worldBuilderAccess;
    }

    bool Application::RequestOfflineBuilderLevel(const std::string &levelName, std::string &error) {
        const auto *playable = Shared::Game::FindPlayableLevel(levelName);
        if (playable == nullptr) {
            error = "Unsupported level; use wb_levels_list to find supported levels";
            return false;
        }
        if (!playable->dlc.empty()) {
            const auto *dlc = Shared::Game::FindDLCByName(playable->dlc);
            if (dlc != nullptr && MissingDLC(Shared::Game::DLCBit(dlc->id))) {
                error = "This level requires the " + std::string(dlc->displayName) + " DLC";
                return false;
            }
        }
        const auto *state = _stateMachine->GetCurrentState();
        if (!IsOfflineBuilderReady() || state == nullptr || state->GetId() != States::WorldBuilder) {
            error = "The offline World Builder must finish loading before switching levels";
            return false;
        }
        // Machine allows re-entering its current state. This also reloads the
        // same level through HidingMenu -> Loading -> Editing, including the
        // normal timeout and camera reopen behavior.
        if (!_stateMachine->RequestNextState(States::WorldBuilder)) {
            error = "A client state transition is already pending";
            return false;
        }
        _offlineBuilderReady = false;
        _mapEditor.SetOpen(false);
        _mapEditor.ClearAndRelease();
        _mapEditor.RequestNewProject();
        SetOfflineBuilderLevel(levelName);
        HideMultiplayerFrontend();
        error.clear();
        return true;
    }

    bool Application::LoadSessionLevel(const std::string &levelName, bool worldBuilder) {
        auto *environment = SDK::wh::C_GlobalEnvironment::GetInstance();
        auto *guiModule   = environment != nullptr ? environment->GetGUIModule() : nullptr;
        auto *menu        = guiModule != nullptr ? guiModule->GetMenu() : nullptr;
        if (menu == nullptr) {
            return false;
        }

        auto *saveLoad = menu->GetSaveLoad();
        if (saveLoad->IsStartPending()) {
            return false;
        }

        // The camera manager survives StartNewGame. Its selector owns bone
        // cameras for the old character, so release them before level teardown
        // destroys that character, rather than at the post-load notification.
        if (auto *cameras = environment->GetCameraManager(); cameras != nullptr && cameras->GetBoneAnimatedCamera()->ReleaseActor()) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Released animated camera before session level load");
        }

        // The same flow the game stamps for `+map`: a level with no savegame
        // and no story bootstrap, which is the only start a session wants.
        saveLoad->m_startFlow = static_cast<std::uint8_t>(SDK::wh::guimodule::C_UISaveLoad::E_StartFlow::CommandLineMap);
        // Armed first, so a gameplay start raised from inside the request
        // still counts.
        _offlineBuilder      = worldBuilder;
        _offlineBuilderReady = false;
        if (worldBuilder) {
            // No session to join, so nothing may hold the load back.
            _sessionLoad = {};
            ReleaseSessionHold();
        }
        else {
            _localEvents.SessionLevelRequested();
            // Held before the load starts, so every completion the game waits
            // on for it -- the loading screen, the level switch's end and
            // gameplay start -- stays behind the session's own readiness until
            // it joins.
            ArmSessionHold();
        }
        _sessionLoad.levelRequested = true;
        // A new load supersedes the abandoned one; its own session start is
        // this session's.
        _endAbandonedGameSession   = false;
        _abandonedGameSessionLatch = nullptr;
        saveLoad->InvokeStartNewGameRaw(1, "", levelName.c_str());

        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Loading the session level '{}'", levelName);
        return true;
    }

    bool Application::OpenSession() {
        // Validate once at the completion edge; never infer loading progress
        // from an actor pointer that can still belong to the previous level.
        auto *framework = SDK::IGameFramework::GetInstance();
        if (framework == nullptr || framework->GetClientActor() == nullptr || framework->GetClientEntity() == nullptr) {
            return false;
        }

        _worldService.BeginSession();
        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Session level is up and the local player is live");

        if (Framework::External::Sentry::Wrapper *reporter = CrashContext::Reporter()) {
            const auto state = GetCurrentState();
            reporter->SetUserInformation({"", state.nickname, BuildCrashReporterUserId()});
            reporter->SetTag("session.state", "in-session");
            reporter->SetTag("net.connected", "true");
            reporter->SetTag("session.level", GetSessionLevelName());
            reporter->SetContext("network", {{"host", state.host}, {"port", state.port}, {"serverIdHash", static_cast<int32_t>(state.serverIDHash)}});
            reporter->AddBreadcrumb("session", "Session opened", Framework::External::Sentry::Level::Info, {{"host", state.host}, {"port", state.port}});
        }
        return true;
    }

    void Application::EndOfflineBuilder() {
        _mapEditor.SetOpen(false);
        _mapEditor.ClearAndRelease();
        _offlineBuilder      = false;
        _offlineBuilderReady = false;
        _offlineBuilderEnded = true;
        _offlineBuilderLevel.clear();
        ReturnToMenu();
    }

    void Application::ReturnToMenu() {
        // Only the menu state, not a context teardown: the game leaves a
        // session the same way, and the level stays up until the next
        // StartNewGame replaces it.
        auto *environment   = SDK::wh::C_GlobalEnvironment::GetInstance();
        auto *guiModule     = environment != nullptr ? environment->GetGUIModule() : nullptr;
        auto *loadingScreen = guiModule != nullptr ? guiModule->GetLoadingScreen() : nullptr;
        if (loadingScreen == nullptr) {
            return;
        }

        // A connection that never reached a level leaves the backdrop
        // already playing, and re-entering the state would stop it to open a
        // second copy that fails while the first is still alive -- a black
        // frame, since this state has no loading-screen fallback.
        if (loadingScreen->IsMainMenuState()) {
            return;
        }

        loadingScreen->ReturnToMainMenuState();
        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Returned to the menu backdrop");
    }

    void Application::OnProtocolLaunch(const std::string &url) {
        // Never log the link itself: it can carry the server's password.
        _launchConnection = ParseLaunchLink(url);
        if (!_launchConnection) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("Ignoring a malformed launch link");
            return;
        }
#if defined(KCDC_AGENT_TESTING)
        Boot::AgentTesting::Get().SetNickname(_launchConnection->nickname);
#endif
        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Launch link to {}:{}; connecting once the menu is up", _launchConnection->host, _launchConnection->port);
    }

    void Application::OnAdmissionStatus(const std::string &message) {
        if (IsAdmitted()) {
            return;
        }
        _multiplayerFrontend.ShowAdmission(message);
        ShowMultiplayerFrontend();
    }

    void Application::ShowDownload(std::optional<Core::DownloadProgress> progress) {
        const bool showing = progress.has_value();
        _multiplayerFrontend.ShowDownload(std::move(progress));
        if (showing && !IsShuttingDown()) {
            ShowMultiplayerFrontend();
        }
    }

    bool Application::EndAdmissionWait() {
        if (!_multiplayerFrontend.IsShowingAdmission()) {
            return false;
        }
        _multiplayerFrontend.ShowAdmission({});
        _multiplayerFrontend.ShowDownload(std::nullopt);
        _multiplayerFrontend.ShowScriptApproval({});
        HideMultiplayerFrontend();
        _multiplayerFrontend.Update(*this);
        return true;
    }

    void Application::OnConnectionClosed() {
        _worldBuilderAccess        = false;
        _serverDisconnectRequested = false;
        CancelSessionLoading();
        _localEvents.Reset();
        _worldService.Reset();

        // A placement still waiting for ground would keep re-asserting a
        // pose onto a body that is about to stop existing.
        _playerTeleporter.Reset();

        // Leaving a session closes the editor: a camera pointed at
        // entities that are gone is a camera stuck in the ground. The editor's
        // objects live in that level too, and their handles die with it.
        _mapEditor.SetOpen(false);
        _mapEditor.ClearAndRelease();

        const std::string &reason = GetLastDisconnectionReason();
        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Disconnected{}{}", reason.empty() ? "" : ": ", reason);

        CrashContext::SetTag("session.state", "disconnected");
        CrashContext::SetTag("net.connected", "false");

        // Empty reason = locally initiated disconnect (user quit), nothing to surface.
        if (!reason.empty()) {
            CrashContext::Breadcrumb("session", "Disconnected: " + reason, Framework::External::Sentry::Level::Warning);
        }
        else {
            CrashContext::Breadcrumb("session", "Disconnected (local)");
        }

        // No menu to go back to when quitting, and the machine keeps the
        // first pending request, not the last.
        if (!IsShuttingDown() && _stateMachine->GetCurrentState()->GetId() != States::SessionDisconnection) {
            _stateMachine->RequestNextState(States::SessionDisconnection);
        }
    }

    void Application::ShowMultiplayerFrontend() {
        _multiplayerFrontend.SetVisible(true);
        if (!_frontendControlsHeld) {
            _frontendControlsHeld = true;
            LockControls(true);
        }
    }

    void Application::HideMultiplayerFrontend() {
        _multiplayerFrontend.SetVisible(false);
        if (_frontendControlsHeld) {
            _frontendControlsHeld = false;
            if (_controlsLocked == 1) {
                // Level startup can temporarily disable event posting before
                // the frontend takes ownership. Closing the frontend is an
                // explicit hand-off to gameplay, so do not restore that stale
                // startup state.
                _eventPostingWasEnabled = true;
            }
            LockControls(false);
        }

        if (!AreControlsLocked()) {
            _eventPostingWasEnabled = true;
            ApplyGameInputLock(false);
            if (GetImGUI() && GetImGUI()->IsInitialized()) {
                Framework::External::ImGUI::Wrapper::ShowCursor(false);
            }
            if (gGlobals.window != nullptr) {
                SetFocus(gGlobals.window);
            }
        }
        UpdateCursorPresentation();
    }

    void Application::OnChatMessageReceived(const Framework::Networking::RPC::ChatMessage &msg) {
        KCDC::Features::Player::PlayerService::TalkFromChat(msg.senderNetworkId, msg.text);
    }

    void Application::UpdateSessionFeatures() {
        for (IFeatureService *service : _sessionFeatures) {
            service->Update();
        }
    }

    void Application::ResetSessionFeatures() {
        for (IFeatureService *service : _sessionFeatures) {
            service->Reset();
        }
    }

    void Application::NotifyNativeContextChanged() {
        for (IFeatureService *service : _sessionFeatures) {
            service->NativeContextChanged();
        }

        // The editor holds raw render-node pointers into the level that is
        // going away. This only raises a flag; the tick does the dropping,
        // because this runs on the loading thread.
        _mapEditor.NativeContextChanged();
    }

    void Application::SetCurrentLevelName(const char *name) {
        if (name == nullptr) {
            return;
        }

        // The engine names a level either bare or by its data path; the pak
        // directory is the last segment either way, and that is what the
        // shipped tables are keyed on.
        std::string_view level {name};
        const std::size_t slash = level.find_last_of("/\\");
        if (slash != std::string_view::npos) {
            level.remove_prefix(slash + 1);
        }

        std::lock_guard lock(_levelNameMutex);
        if (_levelName == level) {
            return;
        }
        _levelName.assign(level);
        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Level is now '{}'", _levelName);
    }

    std::string Application::GetCurrentLevelName() const {
        std::lock_guard lock(_levelNameMutex);
        return _levelName;
    }

    void Application::LockControls(bool lock, bool withCursor) {
        const bool wasLocked = AreControlsLocked();

        if (lock) {
            ++_controlsLocked;
        }
        else if (_controlsLocked > 0) {
            --_controlsLocked;
        }

        const bool isLocked = AreControlsLocked();
        if (isLocked != wasLocked) {
            ApplyGameInputLock(isLocked);
        }

        if (withCursor) {
            HoldCursor(lock);
        }
    }

    void Application::HoldCursor(bool hold) {
        const bool hadCursor = _cursorHolders > 0;

        if (hold) {
            ++_cursorHolders;
        }
        else if (_cursorHolders > 0) {
            --_cursorHolders;
        }

        if ((_cursorHolders > 0) != hadCursor) {
            UpdateCursorPresentation();
        }
    }

    void Application::ReconcileWebFocusLock() {
        // Derived rather than paired: every way focus can end -- blur, hide,
        // destroy, the resource stopping, the manager taking the view off
        // screen -- lands here. Resource views only; the frontend and the
        // chat line hold their own, and the chat line wants no cursor.
        auto *web          = GetWebManager();
        const bool focused = web != nullptr && web->IsAnyGCViewFocused();
        if (focused != _webFocusControlsHeld) {
            _webFocusControlsHeld = focused;
            LockControls(focused);
        }
    }

    void Application::UpdateCursorPresentation() {
        const bool frontendVisible = IsMultiplayerFrontendVisible();
        if (GetImGUI() && GetImGUI()->IsInitialized()) {
            Framework::External::ImGUI::Wrapper::ShowCursor(!frontendVisible && _cursorHolders > 0);
        }

        if (frontendVisible == _frontendHardwareCursorHeld) {
            return;
        }

        // One increment for as long as the frontend is up, so the engine's
        // own holders keep theirs and the count returns to what it was.
        auto *mouse = SDK::IHardwareMouse::GetInstance();
        if (mouse == nullptr) {
            return;
        }

        if (frontendVisible) {
            mouse->IncrementCounter();
        }
        else {
            mouse->DecrementCounter();
        }
        _frontendHardwareCursorHeld = frontendVisible;
    }

    void Application::ApplyGameInputLock(bool locked) {
        // Swallowing window messages is not enough: KCD2 reads the mouse
        // through DirectInput8, so the camera keeps turning. These two levers
        // are what the engine itself uses.
        auto *actionMaps = SDK::IActionMapManager::GetInstance();
        auto *input      = SDK::IInput::GetInstance();

        if (locked) {
            // resetStateOnDisable releases whatever is currently held, so a key
            // that was down when the overlay opened does not stay down.
            if (actionMaps) {
                actionMaps->Enable(false, true);
            }
            if (input) {
                _eventPostingWasEnabled = input->IsEventPostingEnabled();
                input->EnableEventPosting(false);
            }
        }
        else {
            if (input) {
                input->EnableEventPosting(_eventPostingWasEnabled);
            }
            if (actionMaps) {
                actionMaps->Enable(true, false);
            }
        }

        if (!actionMaps || !input) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("Game input lock is incomplete (action maps: {}, input: {})", actionMaps != nullptr, input != nullptr);
        }
    }

    void Application::ModuleRegister(Framework::Scripting::Engine *engine) {
        v8::Isolate *isolate = engine != nullptr ? engine->GetIsolate() : nullptr;
        if (isolate == nullptr) {
            return;
        }

        v8::Locker locker(isolate);
        v8::Isolate::Scope isolateScope(isolate);
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = engine->GetContext();
        if (context.IsEmpty()) {
            return;
        }
        v8::Context::Scope contextScope(context);

        RegisterScripting(isolate, context->Global());
        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->debug("Registered KCDC client scripting bindings");
    }
} // namespace KCDC::Core
