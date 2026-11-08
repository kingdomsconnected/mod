// Include safe_win32 FIRST to avoid winsock conflicts
#include <utils/safe_win32.h>

#include <MinHook.h>

#include <logging/logger.h>
#include <utils/game_module.h>
#include <utils/string_utils.h>

#include <utils/hooking/hook_function.h>
#include <utils/hooking/hooking.h>
#include <utils/hooking/hooking_patterns.h>
#include <utils/hooking/pattern_table.h>

#include "core/application.h"
#include "core/boot/init_patches.h"
#include "core/crash_reporting.h"
#include "features/player/player_nametags.h"
#include "core/streamed_assets/streamed_asset_service.h"
#include "sdk/patterns.h"
#include "shared/network_config.h"
#include "shared/version.h"

extern "C" void __declspec(dllexport) InitClient(const wchar_t *projectPath) {
    KCDC::Core::gGlobals.projectPath = Framework::Utils::StringUtils::WideToNormal(projectPath);

    // Independent processes cannot share a rotating sink: their buffered
    // writes and rotations interleave or truncate each other's records. Only
    // a debug build runs several clients from one install; a release client
    // keeps the fixed name the crash reporter attaches (crash_reporting.cpp).
#ifdef NDEBUG
    Framework::Logging::GetInstance()->SetLogName("KCDC");
#else
    Framework::Logging::GetInstance()->SetLogName("KCDC-" + std::to_string(GetCurrentProcessId()));
#endif
    Framework::Logging::GetInstance()->SetLogFolder(KCDC::Core::gGlobals.projectPath + "\\logs");

    const auto log = Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT);

    // Before any hook: the client arms everything below during the launcher's
    // CRT startup, and the framework instance only comes up much later.
    if (const auto result = Framework::External::Sentry::InitCrashReporter(KCDC::Core::BuildCrashReporterOptions(KCDC::Core::gGlobals.projectPath)); !result) {
        log->warn("Crash reporting unavailable: {}", result.GetError().message);
    }
    else {
        Framework::External::Sentry::GetCrashReporter().SetTag("boot.stage", "entry");
        Framework::External::Sentry::GetCrashReporter().SetUserInformation({"", "", KCDC::Core::BuildCrashReporterUserId()});
        log->info("Crash reporting installed at entry point");
    }

    const auto initializeStatus = MH_Initialize();
    if (initializeStatus != MH_OK && initializeStatus != MH_ERROR_ALREADY_INITIALIZED) {
        log->error("MinHook initialization failed: {}", MH_StatusToString(initializeStatus));
        return;
    }

    KCDC::Core::ApplyInitPatches();

    // Everything below is armed before the game's main() runs, so no hook has
    // to beat the engine to the code it patches.
    const auto bringUp = [](HMODULE gameModule) {
        hook::set_base(reinterpret_cast<std::uintptr_t>(gameModule));

        // Seed the resolved addresses before the first pattern is asked for. Each one is still
        // verified against the bytes in the loaded WHGame.dll and anything that fails falls back
        // to a scan, so a table built for another game build only costs the scan it meant to save.
        // A hardened build ships no table: it would list every address the mod hooks.
#if !defined(KCDC_OBFUSCATE)
        hook::load_pattern_table(KCDC::Core::gGlobals.projectPath + "\\kcdc.patterns");
#endif

        SDK::Patterns::Init();

        InitFunction::RunAll();
        MH_EnableHook(MH_ALL_HOOKS);

        KCDC::Core::gApplication = std::make_unique<KCDC::Core::Application>();

        Framework::Integrations::Client::InstanceOptions opts;
        opts.useRenderer  = true;
        opts.useImGUI     = true;
        opts.usePresence  = true;
        opts.discordAppId = 1553759528430862356;
        // The renderer can only come up once CryEngine owns a swap chain; the
        // DX12 present hook calls RenderInit() at that point.
        opts.initRendererManually = true;
        opts.gameName             = KCDC::Shared::Network::GameName;
        opts.gameVersion          = KCDC::Shared::Network::GameVersion;
        opts.modSlug              = KCDC::Shared::Network::ModSlug;
        opts.modVersion           = KCDC::Shared::Version::rel;
#ifdef KCDC_RPC_IDENTIFIER_SALT
        opts.rpcIdentifierSalt = KCDC_RPC_IDENTIFIER_SALT;
#endif
        // A debug client joins any debug server: both register the shared
        // disabled token, so peers built from different commits still complete
        // the challenge. Retail verifies. KCDCServer matches this.
#ifdef NDEBUG
        opts.verifyBuildToken = true;
#else
        opts.verifyBuildToken = false;
#endif
        opts.rendererOptions.backend  = Framework::Graphics::RendererBackend::BACKEND_D3D_12;
        opts.rendererOptions.platform = Framework::Graphics::PlatformBackend::PLATFORM_WIN32;

        // Noto Serif includes the Latin Extended-A characters used in chat
        // and nametags. Its license is shipped beside the font files.
        opts.imguiFontPath = KCDC::Core::gGlobals.projectPath + "\\files\\kcdc\\fonts\\NotoSerif-Regular.ttf";
        opts.imguiFonts.push_back({KCDC::Features::Player::NametagPass::kFontName, KCDC::Core::gGlobals.projectPath + "\\files\\kcdc\\fonts\\" + KCDC::Features::Player::NametagPass::kFontName + ".ttf"});

        // Already installed by the entry point; these only let the framework decorate that reporter.
        const auto crashOptions = KCDC::Core::BuildCrashReporterOptions(KCDC::Core::gGlobals.projectPath);
        opts.sentryDSN          = crashOptions.dsn;
        opts.sentryModulePath   = crashOptions.handlerPath;
        opts.sentryRelease      = crashOptions.release;
        opts.sentryEnvironment  = crashOptions.environment;

        // Beside the mod, not in the shared MafiaHub folder under %APPDATA%: uninstalling KCDC takes
        // everything it cached with it.
        opts.cacheRoot             = KCDC::Core::gGlobals.projectPath + "\\cache";
        opts.assetConsentThreshold = KCDC::Core::StreamedAssets::StreamedAssetSettings::DefaultConsentThreshold();

        if (auto result = KCDC::Core::gApplication->Init(opts); !result) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("KCDC client failed to initialize: {}", result.GetError().message);
        }
    };

    if (auto *gameModule = KCDC::Core::LoadGameModule()) {
        bringUp(static_cast<HMODULE>(gameModule));
        return;
    }

    // The module could not be loaded early, so the engine loads it and the
    // hooks land alongside it: the reel and the front end may already be up.
    log->warn("Bringing the game SDK up alongside the engine rather than ahead of it");
    Framework::Utils::GameModule::WhenReady(L"WHGame.dll", bringUp);
}
