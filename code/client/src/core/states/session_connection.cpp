#include "session_connection.h"
#include "states.h"

#include <logging/logger.h>
#include <utils/states/machine.h>

#include "core/application.h"
#include "core/crash_context.h"
#include "core/dlc.h"
#include "core/obfuscation.h"

#include "features/player/player_service.h"

#include "shared/game/dlc_catalog.h"

#include <mafianet/get_time.h>

#include <cstdint>
#include <string>

namespace KCDC::Core::States {
    namespace {
        // How long the page gets to paint the admission card away. The level load stops the view
        // repainting, so a card still in its last frame would sit over the loading screen until the
        // level is up. There is no paint callback to wait on; a few frames is all it takes.
        constexpr std::uint64_t kAdmissionCardClearMs = 250;

        constexpr std::uint64_t kDownloadRefreshMs   = 100;
        constexpr std::uint64_t kDownloadCardDelayMs = 400;
    } // namespace

    const char *SessionConnectionState::GetName() const {
        return "SessionConnection";
    }

    int32_t SessionConnectionState::GetId() const {
        return StateIds::SessionConnection;
    }

    bool SessionConnectionState::OnEnter(Framework::Utils::States::Machine *machine) {
        KCDC_JUNK;
        _levelRequested          = false;
        _dlcChecked              = false;
        _assetsBegun             = false;
        _assetsReady             = false;
        _downloadShownAtMs       = 0;
        _downloadBeganAtMs       = 0;
        _levelRequestNotBeforeMs = 0;
        _admittedAtMs            = 0;
        gApplication->BeginSessionLoading();
        if (!Enter(machine)) {
            return false;
        }

        // The menu goes, and nothing of the mod's takes its place. The game's own loading screen
        // owns the view for the rest of the dial, and a second loader drawn over it only reads as
        // a popup interrupting it.
        const auto state = gApplication->GetCurrentState();
        gApplication->HideMultiplayerFrontend();

        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Connecting to {}:{} as '{}'", state.host, state.port, state.nickname);

        CrashContext::SetTag("session.state", "connecting");
        CrashContext::Breadcrumb("state", "Connecting", Framework::External::Sentry::Level::Info, {{"host", state.host}, {"port", state.port}});

        if (const auto result = gApplication->ConnectToServer(state.host, state.port, state.password); !result || !KCDC_OPAQUE_TRUE) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("Unable to connect to {}:{}: {}", state.host, state.port, result.GetError().message);
            gApplication->GetMultiplayerFrontend().ShowError(result.GetError().message);
            gApplication->CancelSessionLoading();
            machine->RequestNextState(StateIds::MainMenu);

            // False is what sends the machine to Exit; returning true would
            // park it in OnUpdate, which only ever waits for a handshake that
            // is no longer coming.
            return false;
        }
        return machine->GetNextState() == nullptr;
    }

    bool SessionConnectionState::OnUpdate(Framework::Utils::States::Machine *machine) {
        // Until the server's admission gate answers there is no connection, no
        // level name and nothing of the server's to show; the only thing the
        // player can do is give up, from the card a queue raises.
        if (!gApplication->IsAdmitted()) {
            if (gApplication->GetMultiplayerFrontend().ConsumeLeaveRequest()) {
                Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Left while the server was deciding on the connection");
                if (const auto result = gApplication->GetNetworkingEngine()->GetNetworkClient()->Disconnect(); !result) {
                    machine->RequestNextState(StateIds::MainMenu);
                }
            }
            return false;
        }

        if (_admittedAtMs == 0) {
            _admittedAtMs = MafiaNet::GetTime();
        }
        // Admitted but no resource list: nothing the server sends over RPC is
        // arriving. The download itself is not timed; it may legitimately be
        // large.
        if (gApplication->GetConnectionPhase() == Framework::Integrations::Client::ConnectionPhase::Authenticating && MafiaNet::GetTime() - _admittedAtMs >= kServerAnswerTimeoutMs) {
            gApplication->FailSessionLoading(kServerSilent);
            return false;
        }
        // Both the asset paks ahead of the level and the resource packages
        // behind it come through this one status, and the level's own load
        // completion now waits on them without a clock. The Framework's own
        // watch (InstanceOptions::downloadStallTimeoutMs) refuses a download
        // that has gone silent, and a wait in a busy server's queue is not
        // silence; the join itself never times the download.

        // The level name arrives in the session handshake, ahead of the
        // download and the spawn barrier, so the load overlaps them.
        if (!_levelRequested) {
            const std::string level = gApplication->GetSessionLevelName();
            if (level.empty()) {
                // Admission has already supplied the session config. Waiting
                // for finalization here would deadlock the resource barrier.
                constexpr const char *message = "The server did not name a level to load.";
                Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error(message);
                gApplication->GetMultiplayerFrontend().ShowError(message);
                if (const auto result = gApplication->GetNetworkingEngine()->GetNetworkClient()->Disconnect(); !result) {
                    machine->RequestNextState(StateIds::MainMenu);
                }
                return false;
            }

            // The requirement rode in with the level name, so refusing here
            // costs one round trip rather than a download and a level load.
            if (!_dlcChecked) {
                _dlcChecked                  = true;
                const std::uint16_t required = gApplication->GetSessionRequiredDLC();
                const std::uint16_t missing  = KCDC::Core::MissingDLC(required);
                if (missing != 0 || !KCDC_OPAQUE_TRUE) {
                    const std::string message = "This server requires " + KCDC::Shared::Game::DLCMaskToLabels(missing) + ".";
                    Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("{}", message);
                    gApplication->GetMultiplayerFrontend().ShowError(message);
                    if (const auto result = gApplication->GetNetworkingEngine()->GetNetworkClient()->Disconnect(); !result) {
                        machine->RequestNextState(StateIds::MainMenu);
                    }
                    return false;
                }
            }

            // The level is asked for only once every streamed pak is mounted: CryPak resolves a
            // path when it is loaded, and the load's own level edge is what flushes every cache
            // that could still hold a stock copy.
            if (!_assetsReady) {
                KCDC::Core::StreamedAssets::StreamedAssetService &assets = KCDC::Core::gApplication->GetStreamedAssetService();
                Core::MultiplayerFrontend &frontend                          = gApplication->GetMultiplayerFrontend();
                if (!_assetsBegun) {
                    _assetsBegun = true;
                    assets.BeginJoin();
                }
                const KCDC::Core::StreamedAssets::StreamedAssetService::JoinStatus status = assets.PollJoin();
                const bool leaving                                                            = frontend.ConsumeLeaveRequest();
                if (status == KCDC::Core::StreamedAssets::StreamedAssetService::JoinStatus::Failed || leaving) {
                    frontend.ShowDownload(std::nullopt);
                    if (leaving) {
                        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Left while the server's assets were downloading");
                    }
                    else {
                        frontend.ShowError(assets.GetError());
                    }
                    if (const auto result = gApplication->GetNetworkingEngine()->GetNetworkClient()->Disconnect(); !result) {
                        machine->RequestNextState(StateIds::MainMenu);
                    }
                    return false;
                }

                if (status == KCDC::Core::StreamedAssets::StreamedAssetService::JoinStatus::AwaitingConsent) {
                    gApplication->ShowDownload(Core::DownloadProgress {0, gApplication->GetAssetConsentBytes(), true, 0, false, -1});
                    bool accepted = frontend.ConsumeDownloadConsent();
#if defined(KCDC_AGENT_TESTING)
                    // An agent-led test has nobody at the screen to accept the size.
                    if (!accepted) {
                        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Agent testing: accepted the asset download without asking");
                        accepted = true;
                    }
#endif
                    if (accepted) {
                        gApplication->GrantAssetConsent();
                    }
                    return false;
                }
                if (status != KCDC::Core::StreamedAssets::StreamedAssetService::JoinStatus::Ready) {
                    // Only a download long enough to watch gets the card: a cache hit or a small
                    // fetch is over before a popup over the loading screen would read as anything
                    // but a flicker. The page is redrawn from a script evaluation, so progress goes
                    // out ten times a second rather than every frame.
                    const MafiaNet::Time now = MafiaNet::GetTime();
                    if (_downloadBeganAtMs == 0) {
                        _downloadBeganAtMs = now;
                    }
                    const Framework::Integrations::Client::AssetDownloadStatus &progress = gApplication->GetAssetDownloadStatus();
                    if (progress.downloading && progress.bytesTotal > 0 && now >= _downloadBeganAtMs + kDownloadCardDelayMs && now >= _downloadShownAtMs + kDownloadRefreshMs) {
                        _downloadShownAtMs = now;
                        gApplication->ShowDownload(Core::DownloadProgress {progress.bytesDownloaded, progress.bytesTotal, false, progress.bytesPerSecond, progress.waitingForSlot, progress.queuePosition});
                    }
                    return false;
                }
                // Taken down with the rest of the card by EndAdmissionWait, just below.
                _assetsReady = true;
            }

            if (gApplication->EndAdmissionWait()) {
                _levelRequestNotBeforeMs = MafiaNet::GetTime() + kAdmissionCardClearMs;
            }
            if (MafiaNet::GetTime() < _levelRequestNotBeforeMs) {
                return false;
            }

            _levelRequested = gApplication->LoadSessionLevel(level);
            return false;
        }

        // Application requests SessionResources through
        // OnLevelAndAssetsReady once the level has loaded (the game now waits
        // on the session's readiness hold) and the assets are downloaded.
        return false;
    }

    void SessionConnectionState::OnLevelAndAssetsReady() {
        _machine->RequestNextState(StateIds::SessionResources);
    }

    bool SessionConnectionState::OnExit(Framework::Utils::States::Machine *machine) {
        // A refusal or a dropped connection leaves while the card may still be up, and may
        // leave a download running.
        gApplication->EndAdmissionWait();
        gApplication->GetMultiplayerFrontend().ShowDownload(std::nullopt);
        return SessionStartupState::OnExit(machine);
    }
} // namespace KCDC::Core::States
