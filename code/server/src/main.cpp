#include "core/server.h"

#include "features/navigation/navmesh_discovery.h"

#include "shared/network_config.h"
#include "shared/streamed_assets/streamed_asset_policy.h"
#include "shared/version.h"

#include <logging/logger.h>
#include <utils/config_schema.h>

#include <string>

int main(int argc, char **argv) {
    Framework::Integrations::Server::InstanceOptions options;
    options.bindHost          = "0.0.0.0";
    options.bindPort          = KCDC::Shared::Network::Port;
    options.maxPlayers        = KCDC::Shared::Network::MaxPlayers;
    options.maxPlayersHardCap = KCDC::Shared::Network::MaxPlayers;
    options.modName           = KCDC::Shared::Network::ModName;
    options.modSlug           = KCDC::Shared::Network::ModSlug;

    // Both replicated: a client loads the level, and checks the DLC list,
    // before it downloads a byte. `level` takes the pak directories under
    // Data/Levels; `required_dlc` takes the game's own E_DLC names, whose
    // spelling PostInit checks because `allowed` constrains whole string
    // fields rather than array members. The DLC list is advisory -- clients
    // honour it, nothing here enforces it. `scoreboard` is on unless a
    // server sets it to false; then no client shows the player list or asks
    // for one. `reveal_map` is on unless a server sets it to false; then
    // every client keeps the game's own map discovery and fog of war. Both
    // must be known before the level loads, which no script can be.
    // World Builder access is granted per player by scripts.
    // `navmesh` stays on the server: the mesh is the
    // game's own and cannot ship with it, so it is optional.
    options.modConfigSchema = {{"name", Framework::Utils::ConfigFieldType::String, "", /*required=*/false, /*replicated=*/true, {}, "The server name shown in players' Discord presence."},
        {"level", Framework::Utils::ConfigFieldType::String, "kutnohorsko", /*required=*/false, /*replicated=*/true, KCDC::Features::Navigation::kLevelNames, "The level clients load when they join."},
        {"required_dlc", Framework::Utils::ConfigFieldType::Json, nlohmann::json::array(), /*required=*/false, /*replicated=*/true, {},
            "DLCs a client must own to join, spelled as the game spells them (e.g. ForgeTycoon). Empty requires none. Advisory: clients check it themselves."},
        {"scoreboard", Framework::Utils::ConfigFieldType::Bool, true, /*required=*/false, /*replicated=*/true, {}, "Shows the player list while a client holds Tab. Set to false to turn it off."},
        {"reveal_map", Framework::Utils::ConfigFieldType::Bool, true, /*required=*/false, /*replicated=*/true, {},
            "Shows every location and point of interest on the map, with no fog, from the start. Set to false to keep the game's own discovery: a place appears once that player finds it."},
        {"world_resources", Framework::Utils::ConfigFieldType::Json, nlohmann::json::array(), /*required=*/false, /*replicated=*/false, {}, "World exports to load automatically: paths or {name, path} objects. Paths are relative to the server working directory; names must be unique."},
        {"navmesh", Framework::Utils::ConfigFieldType::String, "", /*required=*/false, /*replicated=*/false, {}, "Optional file or directory to check before searching files/ recursively for the level's recast.pak. Navigation.ready reports whether a mesh loaded."}};

    options.gameName    = KCDC::Shared::Network::GameName;
    options.gameVersion = KCDC::Shared::Network::GameVersion;
    options.modVersion  = KCDC::Shared::Version::rel;
#ifdef KCDC_RPC_IDENTIFIER_SALT
    options.rpcIdentifierSalt = KCDC_RPC_IDENTIFIER_SALT;
#endif

    // Crash reporting (Sentry). crashpad_handler ships next to the server exe.
    options.sentryDSN     = "https://8339b4120163fbb5c43b199ccaffbf31@o982453.ingest.us.sentry.io/4512090251460608";
    options.sentryRelease = std::string("kcdc@") + KCDC::Shared::Version::rel;
#ifdef NDEBUG
    options.sentryEnvironment = "retail";
#else
    options.sentryEnvironment = "dev";
#endif

    // A debug server takes any client: the build token folds in the Framework
    // and mod versions, and a local client and server are rarely built from
    // the same commit. Retail keeps the challenge. KCDCClient matches this.
#ifdef NDEBUG
    options.verifyBuildToken = true;
#else
    options.verifyBuildToken = false;
#endif

    options.bindPassword  = "";
    options.bindSecretKey = "";
    options.enableSignals = true;
    options.argc          = argc;
    options.argv          = argv;

    options.services.masterlistUrl = KCDC::Shared::Network::MasterlistUrl;

    // Resources stream models, textures, clips and sounds from their `stream/` and `replace/`
    // folders; the policy says what those may carry.
    options.assetPakPolicy = KCDC::Shared::StreamedAssets::Policy::Get();

    KCDC::Server server;

    if (const auto result = server.Init(options); !result) {
        Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->error("Failed to start KCDCServer: {}", result.GetError().message);
        return 1;
    }

    server.LoadWorldResources();

    // PostInit runs after the port is bound, so a bad `mod` object cannot
    // fail Init itself. Refuse here, before a single tick.
    if (const std::string &configError = server.GetConfigError(); !configError.empty()) {
        Framework::Logging::GetLogger(FRAMEWORK_INNER_SERVER)->error("Refusing to run: {}", configError);
        server.Shutdown();
        return 1;
    }

    server.Run();
    server.Shutdown();

    return 0;
}
