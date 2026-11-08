#include <utils/safe_win32.h>

#include <MinHook.h>

#include <external/imgui/wrapper.h>
#include <utils/hooking/hook_function.h>
#include <utils/hooking/hooking.h>

#include "window.h"

#include "core/application.h"
#include "core/boot/boot_state.h"
#if defined(KCDC_AGENT_TESTING)
#include "core/boot/agent_testing.h"
#endif

#include "sdk/patterns.h"

#include "shared/network_config.h"

#include <cwchar>
#include <iterator>

/*
 * CSystem::WndProc is the procedure CD3D9Renderer registers for the
 * L"CryENGINE" window class. Every IWindowMessageHandler in its vector runs
 * unconditionally, so registering another one could observe messages but never
 * consume them -- hooking the procedure is the only way to get ahead of them.
 *
 * NOTE: KCD2 also reads keyboard and mouse through DirectInput8, which bypasses
 * the message queue. Swallowing here stops window-level input and click-through
 * but not gameplay input. That needs IActionMapManager (IGameFramework slot 27,
 * concrete vtable at WHGame.dll+0x4048018, exposed to script as
 * EnableActionMapManager(enable, resetStateOnDisable)).
 *
 * This file also owns the window's caption, which the game writes twice, both
 * times from InstallTitlePatch. CD3D9Renderer::SetWindow (0x180C1B834) creates
 * the one L"CryENGINE" window during CSystem::Init; C_GameStartup::InitGame
 * (0x180DA2D18) renames it through SetWindowTextA, which is the last word on
 * the caption in a normal boot. Both land while the mod is still scanning
 * WHGame.dll, so the MH_ALL_HOOKS pass is too late for either.
 *
 * It also owns how the game pumps messages. CSystem::PumpWindowMessage picks
 * PeekMessageW/DispatchMessageW for a Unicode window, but the loop that runs
 * the game, wh::game::C_GameStartup::Run (0x18086413C), and a second pump at
 * 0x1819DBCF8 read with PeekMessageA. Reading through the ANSI API narrows
 * every WM_CHAR to the system codepage, so a letter that codepage lacks (the
 * Czech ones on a Western install) reaches the chat and CEF as '?'. The game
 * never reads a message itself -- Run checks only WM_QUIT -- so its imports of
 * the three ANSI calls are pointed at their Unicode twins. The pairs swap
 * together, so no message is read one way and dispatched the other; a Unicode
 * dispatch to an ANSI window (the Lua debugger's) is narrowed by Windows.
 */

namespace KCDC::Core::Boot {
    namespace {
        using CSystem_WndProc_t             = LRESULT(__stdcall *)(HWND, UINT, WPARAM, LPARAM);
        CSystem_WndProc_t g_wndProcOriginal = nullptr;

        // Wide because the game's window is Unicode: SetWindow refuses to boot
        // otherwise ("Expected an UNICODE window for launcher").
#if !defined(KCDC_AGENT_TESTING)
        constexpr wchar_t kWindowTitle[] = L"Kingdoms Connected";
        static_assert(std::size(kWindowTitle) == std::size(Shared::Network::ModDisplayName), "window caption has drifted from Shared::Network::ModDisplayName");
#endif

        // Matching on the class keeps every other window the process creates --
        // the ats_window settings dialog, the overlay's own -- untouched.
        constexpr wchar_t kGameWindowClass[] = L"CryENGINE";

        using CreateWindowExW_t                     = HWND(WINAPI *)(DWORD, LPCWSTR, LPCWSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID);
        CreateWindowExW_t g_createWindowExWOriginal = nullptr;

        HWND WINAPI CreateWindowExW_Hook(DWORD exStyle, LPCWSTR className, LPCWSTR windowName, DWORD style, int x, int y, int width, int height, HWND parent, HMENU menu, HINSTANCE instance, LPVOID param) {
#if defined(KCDC_AGENT_TESTING)
            const auto title = AgentTesting::Get().GetWindowTitle();
#endif
            // SetWindow passes the literal, so an atom is never the game's.
            if (!IS_INTRESOURCE(className) && std::wcscmp(className, kGameWindowClass) == 0) {
#if defined(KCDC_AGENT_TESTING)
                windowName = title.c_str();
#else
                windowName = kWindowTitle;
#endif
            }

            return g_createWindowExWOriginal(exStyle, className, windowName, style, x, y, width, height, parent, menu, instance, param);
        }

        using SetWindowTextA_t                    = BOOL(WINAPI *)(HWND, LPCSTR);
        SetWindowTextA_t g_setWindowTextAOriginal = nullptr;

        // gGlobals.window is only filled in at the first Present, long after
        // InitGame, so the class is read back off the window instead.
        bool IsGameWindow(HWND window) {
            // A wchar longer than the class, so a name merely starting with it
            // truncates and fails on the length.
            wchar_t className[std::size(kGameWindowClass) + 1] = {};

            const int length = GetClassNameW(window, className, static_cast<int>(std::size(className)));
            return length == static_cast<int>(std::size(kGameWindowClass)) - 1 && std::wcscmp(className, kGameWindowClass) == 0;
        }

        BOOL WINAPI SetWindowTextA_Hook(HWND window, LPCSTR text) {
#if defined(KCDC_AGENT_TESTING)
            if (IsGameWindow(window)) {
                const auto title = AgentTesting::Get().GetWindowTitle();
                return SetWindowTextW(window, title.c_str());
            }
            return g_setWindowTextAOriginal(window, text);
#else
            return g_setWindowTextAOriginal(window, IsGameWindow(window) ? Shared::Network::ModDisplayName : text);
#endif
        }

        // Up-transitions and system keys always reach the game, so nothing
        // sticks down and Alt+F4 keeps working.
        bool AlwaysPassThrough(UINT msg) {
            switch (msg) {
            case WM_KEYUP:
            case WM_SYSKEYUP:
            case WM_SYSKEYDOWN:
            case WM_SYSCHAR:
            case WM_LBUTTONUP:
            case WM_RBUTTONUP:
            case WM_MBUTTONUP:
            case WM_XBUTTONUP: return true;
            default: return false;
            }
        }

        bool ConsumeLockedEscape(const Application &application, UINT msg, WPARAM wParam) {
            if (wParam != VK_ESCAPE) {
                return false;
            }

            switch (msg) {
            case WM_KEYDOWN:
            case WM_SYSKEYDOWN:
                if (KCDC::Core::Boot::gBootState.lockedEscapeHeld) {
                    return true;
                }
                if (application.AreControlsLocked()) {
                    KCDC::Core::Boot::gBootState.lockedEscapeHeld = true;
                    return true;
                }
                return false;
            case WM_KEYUP:
            case WM_SYSKEYUP:
                if (KCDC::Core::Boot::gBootState.lockedEscapeHeld) {
                    KCDC::Core::Boot::gBootState.lockedEscapeHeld = false;
                    return true;
                }
                return false;
            case WM_CHAR:
            case WM_SYSCHAR: return KCDC::Core::Boot::gBootState.lockedEscapeHeld;
            default: return false;
            }
        }

        LRESULT __stdcall CSystem_WndProc_Hook(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
            // Backstop for a rename this procedure is armed in time to see;
            // DefWindowProcW at its end is what stores the string.
            if (msg == WM_SETTEXT) {
#if defined(KCDC_AGENT_TESTING)
                const auto title = AgentTesting::Get().GetWindowTitle();
                return g_wndProcOriginal(hWnd, msg, wParam, reinterpret_cast<LPARAM>(title.c_str()));
#else
                return g_wndProcOriginal(hWnd, msg, wParam, reinterpret_cast<LPARAM>(kWindowTitle));
#endif
            }

            auto *app = gApplication.get();
            if (!app || !app->IsInitialized()) {
                return g_wndProcOriginal(hWnd, msg, wParam, lParam);
            }

            // Unconditional: the overlay's toggle keys are read from this mirror
            // and must keep working while their messages are swallowed below.
            if (const auto input = app->GetInput()) {
                input->ProcessEvent(hWnd, msg, wParam, lParam);
            }

            auto *web = app->GetWebManager();
            if (web && web->IsInitialized()) {
                if (msg == WM_SIZE) {
                    web->Resize(LOWORD(lParam), HIWORD(lParam));
                }
                // No gate of our own on either: a view drops what it does not
                // hold focus for, and `Web.focusView` is what sets that.
                if (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) {
                    web->ProcessMouseEvent(hWnd, msg, wParam, lParam);
                }
                if (msg >= WM_KEYFIRST && msg <= WM_KEYLAST) {
                    web->ProcessKeyboardEvent(hWnd, msg, wParam, lParam);
                }
            }

            // Escape belongs to whichever overlay holds input for the complete
            // press/release pair. Its release may arrive after that overlay
            // closes and must not open KCD2's pause menu.
            if (ConsumeLockedEscape(*app, msg, wParam)) {
                return 0;
            }

            auto *imgui = app->GetImGUI();
            const auto imguiState = imgui && imgui->IsInitialized() ? imgui->ProcessEvent(hWnd, msg, wParam, lParam) : Framework::External::ImGUI::InputState::PASS;

            if (app->AreControlsLocked()) {
                if (msg == WM_SETCURSOR && imguiState == Framework::External::ImGUI::InputState::BLOCK) {
                    return TRUE;
                }

                const bool isKeyboardMsg = msg >= WM_KEYFIRST && msg <= WM_KEYLAST;
                const bool isMouseMsg    = msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST;
                if ((isKeyboardMsg || isMouseMsg) && !AlwaysPassThrough(msg)) {
                    return 0;
                }
            }

            const LRESULT result = g_wndProcOriginal(hWnd, msg, wParam, lParam);
            // Record the complete gesture after native processing, and only
            // for messages that were allowed through to the native map.
            if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP || msg == WM_LBUTTONDBLCLK || msg == WM_MOUSEMOVE) {
                app->GetMapEditor().ObserveMapPointer((wParam & MK_LBUTTON) != 0, static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam)));
            }
            else if (msg == WM_KILLFOCUS) {
                app->GetMapEditor().ResetMapPointer();
            }
            return result;
        }
    } // namespace

    namespace Window {
        void InstallTitlePatch() {
            const HMODULE user32 = GetModuleHandleW(L"user32.dll");

            auto *createWindowExW = reinterpret_cast<LPVOID>(GetProcAddress(user32, "CreateWindowExW"));
            auto *setWindowTextA  = reinterpret_cast<LPVOID>(GetProcAddress(user32, "SetWindowTextA"));

            MH_CreateHook(createWindowExW, reinterpret_cast<LPVOID>(&CreateWindowExW_Hook), reinterpret_cast<LPVOID *>(&g_createWindowExWOriginal));
            MH_CreateHook(setWindowTextA, reinterpret_cast<LPVOID>(&SetWindowTextA_Hook), reinterpret_cast<LPVOID *>(&g_setWindowTextAOriginal));

            // Enabled here rather than in the MH_ALL_HOOKS pass, which waits
            // on the pattern sweep and is already too late for both.
            MH_EnableHook(createWindowExW);
            MH_EnableHook(setWindowTextA);
        }
    } // namespace Window
} // namespace KCDC::Core::Boot

static InitFunction init(
    []() {
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.CSystem__WndProc), reinterpret_cast<LPVOID>(&KCDC::Core::Boot::CSystem_WndProc_Hook), reinterpret_cast<LPVOID *>(&KCDC::Core::Boot::g_wndProcOriginal));

        hook::iat("user32.dll", &PeekMessageW, "PeekMessageA");
        hook::iat("user32.dll", &GetMessageW, "GetMessageA");
        hook::iat("user32.dll", &DispatchMessageW, "DispatchMessageA");
    },
    "Window");
