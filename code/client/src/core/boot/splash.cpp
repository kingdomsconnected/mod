#include <utils/safe_win32.h>

#include <MinHook.h>

#include <logging/logger.h>

#include "splash.h"

#include <string>

/*
 * KingdomCome.exe raises the boot splash from its own WinMain (0x140004700),
 * with CreateDialogParamA(&__ImageBase, MAKEINTRESOURCE(231), ...) at
 * 0x140002a50. Dialog 231 holds a single SS_BITMAP static, id 1001, showing
 * the executable's RT_BITMAP 108 -- swapping that one image is the override,
 * and the game keeps the window it owns.
 *
 * The static resizes itself to the image rather than scaling it, and the
 * template is 10px wider than the artwork, so the window has to be refitted
 * afterwards or it exposes a strip of dialog background as the game's own
 * splash does.
 *
 * The executable skips the splash altogether on -no_splash.
 */

namespace KCDC::Core::Boot {
    namespace {
        constexpr ULONG_PTR kSplashDialogId = 231;
        constexpr int kSplashImageControlId = 1001;

        using CreateDialogParamA_t = HWND(WINAPI *)(HINSTANCE, LPCSTR, HWND, DLGPROC, LPARAM);
        CreateDialogParamA_t g_createDialogParamAOriginal = nullptr;

        std::wstring SplashBitmapPath() {
            HMODULE module = nullptr;
            if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(&SplashBitmapPath), &module)) {
                return {};
            }

            wchar_t path[MAX_PATH] = {};
            const DWORD length     = GetModuleFileNameW(module, path, MAX_PATH);
            if (length == 0 || length >= MAX_PATH) {
                return {};
            }

            const std::wstring full(path, length);
            const auto separator = full.find_last_of(L"\\/");
            return separator == std::wstring::npos ? std::wstring {} : full.substr(0, separator) + L"\\files\\kcdc\\splash.bmp";
        }

        HBITMAP LoadSplashBitmap() {
            const std::wstring path = SplashBitmapPath();
            if (path.empty()) {
                return nullptr;
            }

            return static_cast<HBITMAP>(LoadImageW(nullptr, path.c_str(), IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE | LR_CREATEDIBSECTION));
        }

        // Takes the window to the size the static just took, centred on the
        // monitor DS_CENTER picked.
        void FitToImage(HWND dialog, HBITMAP bitmap) {
            BITMAP info = {};
            if (GetObject(bitmap, sizeof(info), &info) == 0) {
                return;
            }

            MONITORINFO monitor = {sizeof(monitor)};
            if (!GetMonitorInfoW(MonitorFromWindow(dialog, MONITOR_DEFAULTTOPRIMARY), &monitor)) {
                return;
            }

            RECT frame = {0, 0, info.bmWidth, info.bmHeight};
            AdjustWindowRectEx(&frame, GetWindowLongW(dialog, GWL_STYLE), FALSE, GetWindowLongW(dialog, GWL_EXSTYLE));

            const LONG width  = frame.right - frame.left;
            const LONG height = frame.bottom - frame.top;
            const LONG x      = monitor.rcWork.left + ((monitor.rcWork.right - monitor.rcWork.left) - width) / 2;
            const LONG y      = monitor.rcWork.top + ((monitor.rcWork.bottom - monitor.rcWork.top) - height) / 2;

            SetWindowPos(dialog, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
        }

        HWND WINAPI CreateDialogParamA_Hook(HINSTANCE instance, LPCSTR templateName, HWND parent, DLGPROC dialogFunc, LPARAM initParam) {
            const HWND dialog = g_createDialogParamAOriginal(instance, templateName, parent, dialogFunc, initParam);
            if (!dialog || !IS_INTRESOURCE(templateName) || reinterpret_cast<ULONG_PTR>(templateName) != kSplashDialogId) {
                return dialog;
            }

            const auto log       = Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT);
            const HBITMAP bitmap = LoadSplashBitmap();
            if (!bitmap) {
                log->warn("Splash: files\\kcdc\\splash.bmp is missing or not a bitmap, keeping the game's artwork");
                return dialog;
            }

            // Left to go with the window, which the game destroys on its way
            // into CSystem::Init and never creates again.
            SendDlgItemMessageW(dialog, kSplashImageControlId, STM_SETIMAGE, IMAGE_BITMAP, reinterpret_cast<LPARAM>(bitmap));
            FitToImage(dialog, bitmap);

            log->info("Splash artwork replaced with the mod's own");
            return dialog;
        }
    } // namespace

    namespace Splash {
        void InstallSplashPatch() {
            auto *createDialogParamA = reinterpret_cast<LPVOID>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "CreateDialogParamA"));

            MH_CreateHook(createDialogParamA, reinterpret_cast<LPVOID>(&CreateDialogParamA_Hook), reinterpret_cast<LPVOID *>(&g_createDialogParamAOriginal));

            // Enabled here, not in the MH_ALL_HOOKS pass, which is already too
            // late by the time WHGame.dll is mapped.
            MH_EnableHook(createDialogParamA);
        }
    } // namespace Splash
} // namespace KCDC::Core::Boot
