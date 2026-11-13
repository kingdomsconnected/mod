#include "multiplayer_frontend.h"

#include <utils/safe_win32.h>

#include <objbase.h>
#include <shellapi.h>

#include <gui/manager.h>
#include <gui/view.h>
#include <integrations/client/instance.h>
#include <logging/logger.h>
#include <networking/rpc/client_identity.h>
#include <nlohmann/json.hpp>
#include <utils/string_utils.h>
#include <utils/url_protocol.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <string_view>
#include <thread>
#include <utility>

#include "core/application.h"
#include "core/obfuscation.h"
#include "features/prop/prop_editor_document.h"
#include "sdk/cryengine/system/i_console.h"
#include "sdk/wh/c_wh_global_environment.h"
#include "sdk/wh/guimodule/c_gui_module.h"
#include "shared/game/levels.h"
#include "shared/network_config.h"
#include "shared/version.h"

namespace KCDC::Core {
    namespace {
        constexpr const char *kLogger      = "KCDCFrontend";
        constexpr const char *kUIHost      = "kcdc-ui";
        constexpr const char *kShowHudCVar = "wh_ui_ShowHud";

        void PlayGameUISound(SDK::wh::guimodule::E_UISound sound) {
            auto *environment = SDK::wh::C_GlobalEnvironment::GetInstance();
            auto *guiModule   = environment != nullptr ? environment->GetGUIModule() : nullptr;
            if (guiModule != nullptr) {
                guiModule->PlayUISound(sound);
            }
        }

        std::filesystem::path ModuleDirectory() {
            HMODULE module = nullptr;
            if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(&ModuleDirectory), &module)) {
                return {};
            }

            wchar_t path[MAX_PATH] = {};
            const DWORD length     = GetModuleFileNameW(module, path, MAX_PATH);
            if (length == 0 || length >= MAX_PATH) {
                return {};
            }
            return std::filesystem::path(std::wstring(path, length)).parent_path();
        }

        void TrimAsciiWhitespace(std::string &value) {
            const auto first = value.find_first_not_of(" \t\r\n");
            if (first == std::string::npos) {
                value.clear();
                return;
            }
            value.erase(value.find_last_not_of(" \t\r\n") + 1);
            value.erase(0, first);
        }

        bool ContainsNull(const std::string &value) {
            return value.find('\0') != std::string::npos;
        }

        constexpr std::size_t kMaxLinkLength = 512;

        /**
         * The bounds an operator's directory link is held to before the shell sees it. A masterlist
         * row is operator-supplied text and ShellExecuteW opens whatever scheme a handler claims, so
         * only a plain web address gets through -- and only one the shell cannot read as arguments
         * of its own.
         */
        bool IsOpenableWebUrl(const std::string &url) {
            const auto hasScheme = [&url](std::string_view scheme) { return url.size() > scheme.size() && std::equal(scheme.begin(), scheme.end(), url.begin(), [](char expected, char character) { return expected == std::tolower(static_cast<unsigned char>(character)); }); };
            if (url.size() > kMaxLinkLength || (!hasScheme("https://") && !hasScheme("http://"))) {
                return false;
            }
            return std::none_of(url.begin(), url.end(), [](char character) { return static_cast<unsigned char>(character) <= 0x20 || character == '"'; });
        }

        /**
         * Hands an operator's link to the player's own browser. The page cannot follow it itself --
         * it is the menu, and a CEF view that navigates away has no way back -- and the shell call
         * blocks while it resolves the handler and starts the browser, so it runs on a thread of
         * its own rather than in the CEF pump that asked for it.
         */
        void OpenWebUrl(const std::string &url) {
            if (!IsOpenableWebUrl(url)) {
                Framework::Logging::GetLogger(kLogger)->warn("Refused a menu link that is not a plain web address");
                return;
            }
            std::thread([wide = Framework::Utils::StringUtils::Utf8ToWide(url)] {
                CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
                ShellExecuteW(nullptr, L"open", wide.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                CoUninitialize();
            }).detach();
        }

        /** The bounds every connection intent is held to, whether the page or a launch link sent it. */
        std::optional<ConnectRequest> Bounded(ConnectRequest request) {
            TrimAsciiWhitespace(request.nickname);
            TrimAsciiWhitespace(request.host);

            if (request.nickname.empty()) {
                request.nickname = "Player";
            }
            if (request.nickname.size() > 63 || request.host.empty() || request.host.size() > 127 || request.password.size() > 63 || request.ticket.size() > Framework::Networking::RPC::ClientIdentity::kMaxTicketLength || ContainsNull(request.nickname)
                || ContainsNull(request.host) || ContainsNull(request.password) || ContainsNull(request.ticket)) {
                return std::nullopt;
            }
            return request;
        }

        std::optional<ConnectRequest> ParseConnectRequest(const std::string &payload) {
            KCDC_JUNK;
            const nlohmann::json root = nlohmann::json::parse(payload, nullptr, false);
            if (!root.is_object() || !KCDC_OPAQUE_TRUE) {
                return std::nullopt;
            }

            const auto nickname = root.find("nickname");
            const auto host     = root.find("host");
            const auto port     = root.find("port");
            const auto password = root.find("password");
            if (nickname == root.end() || !nickname->is_string() || host == root.end() || !host->is_string() || port == root.end() || password == root.end() || !password->is_string()) {
                return std::nullopt;
            }

            std::uint64_t parsedPort = 0;
            if (port->is_number_unsigned()) {
                parsedPort = port->get<std::uint64_t>();
            }
            else if (port->is_number_integer()) {
                const std::int64_t signedPort = port->get<std::int64_t>();
                if (signedPort <= 0) {
                    return std::nullopt;
                }
                parsedPort = static_cast<std::uint64_t>(signedPort);
            }
            else {
                return std::nullopt;
            }
            if (parsedPort == 0 || parsedPort > 65535 || !KCDC_OPAQUE_TRUE) {
                return std::nullopt;
            }

            ConnectRequest request;
            request.nickname = nickname->get<std::string>();
            request.host     = host->get<std::string>();
            request.port     = static_cast<std::int32_t>(parsedPort);
            request.password = password->get<std::string>();

            return Bounded(std::move(request));
        }
    } // namespace

    std::optional<ConnectRequest> ParseLaunchLink(std::string_view url) {
        KCDC_JUNK;
        const std::optional<Framework::Utils::UrlProtocol::ParsedUrl> link = Framework::Utils::UrlProtocol::Parse(Shared::Network::ModSlug, url);
        if (!link || !KCDC_OPAQUE_TRUE) {
            return std::nullopt;
        }

        ConnectRequest request;
        request.host = link->host;
        request.port = link->port.has_value() ? *link->port : Shared::Network::Port;
        if (const std::optional<std::string_view> nickname = link->Query("nickname")) {
            request.nickname = *nickname;
        }
        if (const std::optional<std::string_view> password = link->Query("password")) {
            request.password = *password;
        }
        if (const std::optional<std::string_view> ticket = link->Query("ticket")) {
            request.ticket = *ticket;
        }
        return Bounded(std::move(request));
    }

    MultiplayerFrontend::~MultiplayerFrontend() = default;

    bool MultiplayerFrontend::Initialize(Core::Application &application) {
        auto *renderer = application.GetRenderer();
        auto *web      = application.GetWebManager();
        if (_initializationAttempted || renderer == nullptr || !renderer->IsInitialized() || web == nullptr || Core::gGlobals.window == nullptr) {
            return false;
        }
        _initializationAttempted = true;

        const std::filesystem::path moduleDirectory = ModuleDirectory();
        const std::filesystem::path uiRoot          = moduleDirectory / "files" / "kcdc" / "ui";
        const std::filesystem::path pagePath        = uiRoot / "index.html";
        if (moduleDirectory.empty() || !std::filesystem::is_regular_file(pagePath)) {
            Framework::Logging::GetLogger(kLogger)->error("Multiplayer UI page is missing: {}", pagePath.string());
            return false;
        }

        RECT clientRect {};
        GetClientRect(Core::gGlobals.window, &clientRect);
        const int width  = std::max<LONG>(clientRect.right - clientRect.left, 1);
        const int height = std::max<LONG>(clientRect.bottom - clientRect.top, 1);

        // GPU compositing for every view: CEF paints into a shared texture the D3D12
        // backend copies on the GPU, with no per-frame CPU readback. The backend
        // falls back to CPU paint on its own when the shared copy cannot be set up.
        if (auto result = web->Init(moduleDirectory.string(), {width, height}, renderer, true); !result) {
            Framework::Logging::GetLogger(kLogger)->error("Could not initialize the multiplayer web UI: {}", result.GetError().message);
            return false;
        }

        // The framework serves this from its own secure scheme: reads happen on
        // a CEF file thread rather than the IO thread, the page gets a real
        // origin, and a missing asset comes back as a 404 the network panel can
        // show instead of a scheme-level load failure.
        web->RegisterResourceDirectory(kUIHost, uiRoot);

        const std::string pageURL = Framework::GUI::Manager::ResourceURL(kUIHost, "index.html");
        _viewId                   = web->CreateView(pageURL, 0, 0);
        auto *view                = web->GetView(_viewId);
        if (_viewId < 0 || view == nullptr) {
            Framework::Logging::GetLogger(kLogger)->error("Could not create the multiplayer frontend view");
            _viewId = -1;
            return false;
        }

        view->SetZIndex(100);
        view->LockToOrigin(pageURL);
        view->SetOnViewEventCallback([this](const Framework::GUI::ViewEventData &event) {
            if (event.event == Framework::GUI::ViewEvent::DocumentReady && event.isMainFrame) {
                MarkDocumentReady();
            }
        });

        BindPageEvents(application);
        ApplyViewVisibility(application);
        Framework::Logging::GetLogger(kLogger)->info("Kingdoms Connected multiplayer frontend initialized");
        return true;
    }

    void MultiplayerFrontend::BindPageEvents(Core::Application &application) {
        auto *view = application.GetWebManager()->GetView(_viewId);
        if (view == nullptr) {
            return;
        }

        view->AddEventListener("menu.connect", [this](const std::string &payload) {
            auto request = ParseConnectRequest(payload);
            if (!request) {
                ShowError("Enter a valid address and a port between 1 and 65535.");
                return;
            }
            _message.clear();
            _messageSticky  = false;
            _connectRequest = std::move(request);
        });
        view->AddEventListener("menu.ready", [this](const std::string &) {
            MarkDocumentReady();
        });
        view->AddEventListener("menu.leave", [this](const std::string &) {
            // Latched like quit: SessionConnection disconnects from the tick, not the CEF pump.
            _leaveRequested = true;
        });
        view->AddEventListener("menu.acceptDownload", [this](const std::string &) {
            _downloadAccepted = true;
        });
        view->AddEventListener("menu.acceptScripts", [this](const std::string &) {
            _scriptsAccepted = true;
        });
        view->AddEventListener("menu.worldBuilder", [this](const std::string &payload) {
            // Latched like quit: the level load and the file dialog both belong to the tick.
            const nlohmann::json request = nlohmann::json::parse(payload, nullptr, false);
            if (!request.is_object()) {
                return;
            }
            if (const auto open = request.find("openProject"); open != request.end() && open->is_boolean() && open->get<bool>()) {
                _builderRequest = WorldBuilderRequest {WorldBuilderRequest::Action::OpenProject, {}};
                return;
            }
            if (const auto last = request.find("lastProject"); last != request.end() && last->is_boolean() && last->get<bool>()) {
                _builderRequest = WorldBuilderRequest {WorldBuilderRequest::Action::LastProject, {}};
                return;
            }
            const auto level = request.find("level");
            if (level != request.end() && level->is_string() && Shared::Game::FindPlayableLevel(level->get<std::string>()) != nullptr) {
                _builderRequest = WorldBuilderRequest {WorldBuilderRequest::Action::NewProject, level->get<std::string>()};
            }
        });
        view->AddEventListener("menu.quit", [this](const std::string &) {
            // Only latched: this runs inside the CEF pump, and the quit it asks for takes the
            // browser down with the rest of the mod. MainMenuState takes it from the tick.
            _quitRequested = true;
        });
        view->AddEventListener("menu.openUrl", [](const std::string &payload) {
            // The selected realm's Discord or website, from the details panel. Nothing is latched:
            // the browser starts on a thread of its own and no mod state is touched.
            const nlohmann::json request = nlohmann::json::parse(payload, nullptr, false);
            if (!request.is_object()) {
                return;
            }
            const auto url = request.find("url");
            if (url != request.end() && url->is_string()) {
                OpenWebUrl(url->get<std::string>());
            }
        });
        view->AddEventListener("ui.confirm", [](const std::string &) {
            PlayGameUISound(SDK::wh::guimodule::E_UISound::ModalConfirm);
        });
        view->AddEventListener("ui.cancel", [](const std::string &) {
            PlayGameUISound(SDK::wh::guimodule::E_UISound::ModalCancel);
        });
    }

    /** Both the document load and the page's own announcement land here. */
    void MultiplayerFrontend::MarkDocumentReady() {
        if (!_documentReady) {
            Framework::Logging::GetLogger(kLogger)->debug("Menu page is ready to draw");
        }
        _documentReady     = true;
        _presentationDirty = true;
    }

    void MultiplayerFrontend::Update(Core::Application &application) {
        if (_viewId < 0 && !Initialize(application)) {
            if (_initializationAttempted && _visible) {
                application.HideMultiplayerFrontend();
            }
            return;
        }

        if (_gameHudSuppressed) {
            UpdateHudVisibility();
        }
        else if (_hudVisibilityCaptured) {
            RestoreHudVisibility();
        }

        if (!_documentReady || _viewId < 0) {
            return;
        }
        if (_presentationDirty) {
            PushBootstrap(application);
            PushPresentation(application);
        }
    }

    void MultiplayerFrontend::Shutdown(Core::Application &application) {
        RestoreHudVisibility();
        if (_viewId >= 0) {
            if (auto *web = application.GetWebManager(); web && web->IsInitialized()) {
                web->DestroyView(_viewId);
            }
        }
        _viewId        = -1;
        _documentReady = false;
    }

    void MultiplayerFrontend::SetVisible(bool visible) {
        if (_visible == visible) {
            return;
        }
        _visible           = visible;
        _presentationDirty = true;
        if (_visible) {
            PlayGameUISound(SDK::wh::guimodule::E_UISound::MenuOpen);
        }
        Framework::Logging::GetLogger(kLogger)->debug("Multiplayer frontend {}", _visible ? "shown" : "hidden");

        if (auto *application = Core::gApplication.get(); application != nullptr) {
            ApplyViewVisibility(*application);
        }
    }

    void MultiplayerFrontend::SetGameHudSuppressed(bool suppressed) {
        if (_gameHudSuppressed == suppressed) {
            return;
        }
        _gameHudSuppressed = suppressed;
        if (_gameHudSuppressed) {
            UpdateHudVisibility();
        }
        else {
            RestoreHudVisibility();
        }
    }

    void MultiplayerFrontend::ApplyViewVisibility(Core::Application &application) const {
        if (_viewId < 0) {
            return;
        }
        if (auto *view = application.GetWebManager()->GetView(_viewId)) {
            // Stop compositing immediately. Level loading can block CEF before
            // the page paints its hidden state, leaving the menu texture up.
            view->Display(_visible);
            view->Focus(_visible);
        }
    }

    void MultiplayerFrontend::ShowMenu(const std::string &message) {
        if (!message.empty() || !_messageSticky) {
            _message       = message;
            _messageSticky = !message.empty();
        }
        _presentationDirty = true;
    }

    void MultiplayerFrontend::ShowError(const std::string &message) {
        _message           = message;
        _messageSticky     = true;
        _presentationDirty = true;
    }

    void MultiplayerFrontend::ShowAdmission(const std::string &message) {
        if (_admission == message) {
            return;
        }
        _admission         = message;
        _presentationDirty = true;
        if (message.empty()) {
            _leaveRequested = false;
        }
    }

    bool MultiplayerFrontend::ConsumeLeaveRequest() {
        return std::exchange(_leaveRequested, false);
    }

    void MultiplayerFrontend::ShowDownload(std::optional<DownloadProgress> progress) {
        if (_download == progress) {
            return;
        }
        // An accept latched for a prompt that is no longer up must not carry over to the next one.
        if (!progress || !progress->awaitingConsent) {
            _downloadAccepted = false;
        }
        _download          = progress;
        _presentationDirty = true;
    }

    bool MultiplayerFrontend::ConsumeDownloadConsent() {
        return std::exchange(_downloadAccepted, false);
    }

    void MultiplayerFrontend::ShowScriptApproval(std::vector<std::string> mods) {
        if (_scriptApproval == mods) {
            return;
        }
        // An accept latched for a card that is no longer up must not answer the next one.
        if (mods.empty()) {
            _scriptsAccepted = false;
        }
        _scriptApproval    = std::move(mods);
        _presentationDirty = true;
    }

    bool MultiplayerFrontend::ConsumeScriptApproval() {
        return std::exchange(_scriptsAccepted, false);
    }

    std::optional<ConnectRequest> MultiplayerFrontend::ConsumeConnectRequest() {
        std::optional<ConnectRequest> request;
        request.swap(_connectRequest);
        return request;
    }

    std::optional<WorldBuilderRequest> MultiplayerFrontend::ConsumeBuilderRequest() {
        return std::exchange(_builderRequest, std::nullopt);
    }

    bool MultiplayerFrontend::ConsumeQuitRequest() {
        return std::exchange(_quitRequested, false);
    }

    void MultiplayerFrontend::PushBootstrap(Core::Application &application) {
        auto *view = application.GetWebManager()->GetView(_viewId);
        if (view == nullptr) {
            return;
        }

        nlohmann::json bootstrap;
        bootstrap["brand"]       = Shared::Network::ModDisplayName;
        bootstrap["version"]     = KCDC::Shared::Version::rel;
        bootstrap["defaultHost"] = "127.0.0.1";
        bootstrap["defaultPort"] = Shared::Network::Port;
        bootstrap["world"]       = "Kuttenberg";
        // The page fetches the list itself; one masterlist serves every MafiaHub game, so it needs
        // the gamemode the server announces under to know which rows are ours.
        bootstrap["masterlistUrl"] = Shared::Network::MasterlistUrl;
        bootstrap["gameMode"]      = Shared::Network::ModSlug;
        const auto recentProjects  = Features::Prop::MapFiles::RecentProjects();
        bootstrap["lastProject"]   = recentProjects.empty() ? std::string {} : recentProjects.front();
        view->EvaluateScript("window.KCDCMenu && window.KCDCMenu.setBootstrap(" + bootstrap.dump() + ");");
    }

    void MultiplayerFrontend::PushPresentation(Core::Application &application) {
        auto *view = application.GetWebManager()->GetView(_viewId);
        if (view == nullptr) {
            return;
        }

        nlohmann::json state;
        state["visible"]   = _visible;
        state["message"]   = _message;
        state["admission"] = _admission;
        if (_download) {
            state["download"] = {{"received", _download->received}, {"total", _download->total}, {"awaitingConsent", _download->awaitingConsent}, {"bytesPerSecond", _download->bytesPerSecond}, {"waitingForSlot", _download->waitingForSlot}, {"queuePosition", _download->queuePosition}};
        }
        else {
            state["download"] = nullptr;
        }
        state["scriptApproval"] = _scriptApproval;
        // A typed address, a server's own kick reason and its admission status all land in here, so neither is known to
        // be valid UTF-8, and dump() throws rather than escaping what is not.
        view->EvaluateScript("window.KCDCMenu && window.KCDCMenu.setState(" + state.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) + ");");
        _presentationDirty = false;
    }

    void MultiplayerFrontend::UpdateHudVisibility() {
        if (!SDK::IConsole::HasCVar(kShowHudCVar)) {
            return;
        }
        if (!_hudVisibilityCaptured) {
            _hudVisibilityBeforeSuppression = SDK::IConsole::GetIntCVar(kShowHudCVar, 1);
            _hudVisibilityCaptured          = true;
        }
        if (SDK::IConsole::GetIntCVar(kShowHudCVar, 1) != 0) {
            SDK::IConsole::TrySetIntCVar(kShowHudCVar, 0);
        }
    }

    void MultiplayerFrontend::RestoreHudVisibility() {
        if (!_hudVisibilityCaptured) {
            return;
        }
        if (!SDK::IConsole::HasCVar(kShowHudCVar) || !SDK::IConsole::TrySetIntCVar(kShowHudCVar, _hudVisibilityBeforeSuppression)) {
            return;
        }
        _hudVisibilityCaptured = false;
    }

} // namespace KCDC::Core
