#include <Windows.h>
#include <launcher/project.h>

#include "patcher.h"

#include <filesystem>
#include <string>

namespace {
    std::filesystem::path LauncherExecutablePath() {
        wchar_t path[MAX_PATH] = {L'\0'};
        const DWORD length     = GetModuleFileNameW(nullptr, path, MAX_PATH);
        if (length == 0 || length == MAX_PATH) {
            return {};
        }
        return std::filesystem::path(std::wstring(path, length));
    }
} // namespace

int main(void) {
    // Patch first: the updater replaces files this process would hold open.
    const auto launcherExecutable = LauncherExecutablePath();
    if (launcherExecutable.empty()) {
        MessageBoxW(nullptr, L"Could not resolve the Kingdoms Connected launcher path.", L"Kingdoms Connected", MB_ICONERROR);
        return 1;
    }
    if (!KCDC::Launcher::RunBuildPatcher(launcherExecutable)) {
        return 0; // the updater relaunches us
    }

    Framework::Launcher::ProjectConfiguration config;
    config.destinationDllName = L"KCDCClient.dll";
    config.executableName     = L"KingdomCome.exe";
    config.name               = "KCDC";
    config.launchType         = Framework::Launcher::ProjectLaunchType::PE_LOADING;
    config.useAlternativeWorkDir = true;
    config.alternativeWorkDir = L"Bin\\Win64MasterMasterSteamPGO";
    config.alternativeWorkDirFallbacks = {L"Bin\\Win64MasterMasterGogPGO"};

    // Steam first, then the Microsoft Store / Xbox app (PC Game Pass) copy,
    // then ask for the executable, so a GOG or other copy can be played. GOG
    // keeps the executable in Bin\Win64MasterMasterGogPGO rather than Steam's
    // Bin\Win64MasterMasterSteamPGO, and the Store copy at its package root;
    // the prompt strips either back to the game root. The pick is remembered
    // and wins over the stores from then on.
    config.platforms = {
        std::make_shared<Framework::Launcher::Platforms::Steam>(Framework::Launcher::Platforms::Steam::Options {.appId = 1771300}),
        std::make_shared<Framework::Launcher::Platforms::MicrosoftStore>(Framework::Launcher::Platforms::MicrosoftStore::Options {.packageFamily = L"DeepSilver.77536C3FE941_hmv7qcest37me"}),
        std::make_shared<Framework::Launcher::Platforms::Classic>(Framework::Launcher::Platforms::Classic::Options {
            .title      = "Select your Kingdom Come: Deliverance II executable (Bin\\Win64MasterMasterSteamPGO or Win64MasterMasterGogPGO\\KingdomCome.exe)",
            .filter     = "KingdomCome.exe",
            .filterName = "KingdomCome.exe",
        }),
    };

    // "kcdc://host[:port][?nickname=Name&password=Secret&ticket=Token]" connects
    // straight to that server, whether it arrives on the command line or from a
    // Join button on a website; see Application::OnProtocolLaunch. The ticket
    // reaches the server's playerConnecting handlers untouched. The scheme is
    // Shared::Network::ModSlug, and setting it here claims it for the player's
    // own account on every launch.
    config.urlProtocolScheme = L"kcdc";

    // No `+map`: the level a session runs is the server's to name, and the
    // menu needs none at all. The argument would suppress the game's own
    // main-menu state, which is what the mod's page is drawn over.

#ifdef FW_DEBUG
    config.allocateDeveloperConsole = true;
    config.developerConsoleTitle    = L"kcdc: dev-console";
#endif

    Framework::Launcher::Project project(config);
    if (!project.Launch()) {
        return 1;
    }
    return 0;
}
