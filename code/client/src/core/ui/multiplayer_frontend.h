#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace Framework::Integrations::Client {
    struct AssetDownloadStatus;
    struct CurrentState;
    enum class ConnectionPhase;
} // namespace Framework::Integrations::Client

namespace KCDC::Core {
    class Application;

    /** A validated connection intent, from the local CEF page or a launch link. */
    struct ConnectRequest {
        std::string nickname;
        std::string host;
        std::int32_t port = 0;
        std::string password;
        /** Handed to the server's playerConnecting handlers untouched. Only a launch link carries one. */
        std::string ticket;
    };

    /** The menu's World Builder request; only NewProject uses `level`. */
    struct WorldBuilderRequest {
        enum class Action {
            NewProject,
            OpenProject,
            LastProject
        };
        Action action = Action::NewProject;
        std::string level;
    };

    /**
     * A "kcdc://host[:port][?nickname=Name&password=Secret&ticket=Token]"
     * link, held to the same bounds as a connect from the page. The scheme is
     * the mod's slug and a missing port is the server's default; the nickname
     * falls back to "Player" like an empty one typed in. The ticket is for a
     * server's own launcher or website to pass a one-time join token its
     * scripts check. Nullopt for anything else.
     */
    std::optional<ConnectRequest> ParseLaunchLink(std::string_view url);

    /** What the admission card shows of a streamed asset download. */
    struct DownloadProgress {
        std::uint64_t received = 0;
        std::uint64_t total    = 0;
        bool awaitingConsent   = false;
        // Recent throughput; 0 until bytes move.
        std::uint64_t bytesPerSecond = 0;
        // The server has every download slot busy and this join waits for one; queuePosition is how
        // many joins are ahead (0 is next), -1 when the server did not say.
        bool waitingForSlot        = false;
        std::int32_t queuePosition = -1;

        bool operator==(const DownloadProgress &) const = default;
    };

    /**
     * Multiplayer menu rendered by one framework CEF view.
     *
     * All values crossing its event bridge are parsed and bounded here before
     * the state machine sees them. The server list is the one thing this does
     * not carry: the page reads the masterlist itself over the fw:// scheme,
     * which is CORS- and fetch-enabled, and is handed only the address and the
     * gamemode to keep in PushBootstrap(). Doing it here would cost a worker
     * thread and a per-frame poll for a list nothing else needs.
     */
    class MultiplayerFrontend final {
      public:
        ~MultiplayerFrontend();

        void Update(Core::Application &application);
        void Shutdown(Core::Application &application);

        void SetVisible(bool visible);
        bool IsVisible() const {
            return _visible;
        }

        /** Keeps the native HUD hidden while the connection scene is active. */
        void SetGameHudSuppressed(bool suppressed);

        void ShowMenu(const std::string &message = {});
        void ShowError(const std::string &message);

        /**
         * The card drawn in place of the menu while the server's admission
         * gate holds the connection: its line of status and a way to leave.
         * Empty takes it down. Visibility is still SetVisible's.
         */
        void ShowAdmission(const std::string &message);
        /** Whether the card is up, for an admission status or a download. */
        bool IsShowingAdmission() const {
            return !_admission.empty() || _download.has_value();
        }

        /** The player gave up waiting on the admission card. Answered by SessionConnection, from the tick. */
        bool ConsumeLeaveRequest();

        /**
         * The server's streamed assets on the admission card: bytes fetched of the total, or, while
         * `awaitingConsent`, the size the player is asked to accept. Nullopt takes it down.
         */
        void ShowDownload(std::optional<DownloadProgress> progress);

        /** The player accepted the download's size. Answered by SessionConnection, from the tick. */
        bool ConsumeDownloadConsent();

        std::optional<ConnectRequest> ConsumeConnectRequest();

        /** The player asked to close the game. Answered by MainMenuState, from the tick -- see the listener. */
        bool ConsumeQuitRequest();

        /** The player asked for the World Builder. Answered by MainMenuState, from the tick. The level is one the game ships; the DLC check is the state's. */
        std::optional<WorldBuilderRequest> ConsumeBuilderRequest();

      private:
        bool Initialize(Core::Application &application);
        void MarkDocumentReady();
        void ApplyViewVisibility(Core::Application &application) const;
        void BindPageEvents(Core::Application &application);
        void PushBootstrap(Core::Application &application);
        void PushPresentation(Core::Application &application);
        void UpdateHudVisibility();
        void RestoreHudVisibility();

        int _viewId                   = -1;
        bool _initializationAttempted = false;
        bool _documentReady           = false;
        bool _visible                 = false;
        bool _gameHudSuppressed       = false;
        bool _presentationDirty       = true;

        std::string _message;
        bool _messageSticky = false;
        std::string _admission;
        bool _leaveRequested = false;
        std::optional<DownloadProgress> _download;
        bool _downloadAccepted = false;

        std::optional<ConnectRequest> _connectRequest;
        bool _quitRequested = false;
        std::optional<WorldBuilderRequest> _builderRequest;

        bool _hudVisibilityCaptured         = false;
        int _hudVisibilityBeforeSuppression = 1;
    };
} // namespace KCDC::Core
