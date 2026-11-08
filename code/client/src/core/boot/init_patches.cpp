#include "init_patches.h"

#include <filesystem>

#include "splash.h"
#include "window.h"

#include <utils/safe_win32.h>

#include <MinHook.h>

#include <logging/logger.h>
#include <utils/hooking/hooking.h>

#include <cstring>

namespace KCDC::Core {
    namespace {
        using CreateMutexA_t = HANDLE(WINAPI *)(LPSECURITY_ATTRIBUTES, BOOL, LPCSTR);
        CreateMutexA_t g_createMutexAOriginal = nullptr;

        /*
         * Both of the game's "is another copy running" probes are named
         * mutexes read through ERROR_ALREADY_EXISTS, so masking that one error
         * code settles both:
         *
         *   WhKcd2GameMutex       KingdomCome.exe's single-instance guard,
         *                         which otherwise shows a message box and
         *                         quits ("Checking if another instance is
         *                         running..." is its last log line).
         *
         *   WarhorseApplication   CSystem::GetApplicationInstance walks
         *                         ...(N) until one is free and uses N as the
         *                         application instance index. CryEngine
         *                         appends that to the user folder, so a second
         *                         client reads "kingdomcome2(1)" -- an empty
         *                         profile with no resolution, window mode or
         *                         keybinds. Stopping the walk at 0 keeps every
         *                         client on the real profile.
         *
         * The mutexes are still created under their real names and still
         * serialise what they always did; only the verdict drawn from them
         * changes.
         */
        constexpr const char *kInstanceProbes[] = {"WhKcd2GameMutex", "WarhorseApplication"};

        bool IsInstanceProbe(LPCSTR name) {
            if (name == nullptr) {
                return false;
            }

            for (const char *probe : kInstanceProbes) {
                if (std::strncmp(name, probe, std::strlen(probe)) == 0) {
                    return true;
                }
            }
            return false;
        }

        HANDLE WINAPI CreateMutexA_Hook(LPSECURITY_ATTRIBUTES attributes, BOOL initialOwner, LPCSTR name) {
            const HANDLE handle = g_createMutexAOriginal(attributes, initialOwner, name);
            if (IsInstanceProbe(name)) {
                SetLastError(ERROR_SUCCESS);
            }
            return handle;
        }
    } // namespace

    void *LoadGameModule() {
        // The launcher reports the game executable as this process's identity,
        // so the module directory comes straight off it.
        wchar_t exePath[MAX_PATH] = {};
        if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) == 0) {
            return nullptr;
        }

        const std::filesystem::path gameDirectory = std::filesystem::path(exePath).parent_path();
        const std::filesystem::path modulePath    = gameDirectory / L"WHGame.dll";

        // Bin\Win64Shared, a sibling of the executable's directory, holds most
        // of what WHGame.dll imports -- bink2w64, the fmod pair, steam_api64,
        // dxcompiler, libxess, OpenEXR. Only six DLLs sit beside the module
        // itself, so neither the default search nor LOAD_WITH_ALTERED_SEARCH_PATH,
        // which adds that one directory alone, resolves it; both answer
        // ERROR_MOD_NOT_FOUND. The game reaches them by putting the shared
        // directory on its search path first, and so does this.
        //
        // The entries stay after the load. Only loads asking for
        // LOAD_LIBRARY_SEARCH_USER_DIRS consult them, so the game's own
        // searches are unchanged.
        using AddDllDirectoryFn = DLL_DIRECTORY_COOKIE(WINAPI *)(PCWSTR);
        const auto addDllDirectory = reinterpret_cast<AddDllDirectoryFn>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "AddDllDirectory"));
        if (addDllDirectory == nullptr) {
            return nullptr;
        }

        const std::filesystem::path sharedDirectory = gameDirectory.parent_path() / L"Win64Shared";
        addDllDirectory(gameDirectory.c_str());
        if (std::filesystem::is_directory(sharedDirectory)) {
            addDllDirectory(sharedDirectory.c_str());
        }

        auto *gameModule = LoadLibraryExW(modulePath.c_str(), nullptr, LOAD_LIBRARY_SEARCH_USER_DIRS | LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (gameModule == nullptr) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("Could not load {} early (error {}); the game SDK will come up alongside the engine instead", modulePath.string(), GetLastError());
            return nullptr;
        }

        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Game module loaded at {:#x} ahead of the engine", reinterpret_cast<std::uintptr_t>(gameModule));
        return gameModule;
    }

    void ApplyInitPatches() {
        // kernel32 forwards this to kernelbase; the address resolved here is
        // the one both the executable and WHGame.dll end up calling.
        auto *createMutexA = reinterpret_cast<LPVOID>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "CreateMutexA"));

        MH_CreateHook(createMutexA, reinterpret_cast<LPVOID>(&CreateMutexA_Hook), reinterpret_cast<LPVOID *>(&g_createMutexAOriginal));

        // Enabled here rather than in the MH_ALL_HOOKS pass, which runs once
        // WHGame.dll is mapped and is already too late for these.
        MH_EnableHook(createMutexA);

        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Instance probes masked: multi-instance enabled, user profile shared");

        // Same reason: the game window is created while the mod is still
        // scanning WHGame.dll, so the Window module never sees it born.
        Boot::Window::InstallTitlePatch();

        // Earlier still: the splash goes up from the executable's own WinMain,
        // in the very function whose mutex is masked above.
        Boot::Splash::InstallSplashPatch();
    }
} // namespace KCDC::Core
