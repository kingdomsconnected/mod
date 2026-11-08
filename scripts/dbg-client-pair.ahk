#Requires AutoHotkey v2.0
#Warn

; Launches two KCDC clients side by side for local netcode testing: game
; windows across the top half, their dev consoles docked underneath.
;
; Both connect on their own through a kcdc:// launch link, as Player1 and
; Player2, so start KCDCServer.exe first. The server defaults to
; 127.0.0.1:27015; pass another as the first argument:
;
;     dbg-client-pair.ahk 192.168.1.20:27015
;
; Running two copies at all, and having both read the same user profile,
; comes from the client's boot patches (code/client/src/core/boot/init_patches.cpp).
; Tiling additionally needs that profile set to windowed: a full-screen window
; re-claims the monitor on every activation.

SetTitleMatchMode 2

; `dbg-client-pair.ahk --validate` parses and exits without launching.
if A_Args.Length && A_Args[1] = "--validate" {
    ExitApp
}

BinaryFolder := A_ScriptDir "\..\builds\build-64\bin"
LauncherExe := BinaryFolder "\KCDCLauncher.exe"
ServerAddress := A_Args.Length ? A_Args[1] : "127.0.0.1:27015"

if !FileExist(LauncherExe) {
    MsgBox "KCDCLauncher.exe was not found in:`n" BinaryFolder "`n`nBuild it with: builds\build.bat KCDCLauncher 64"
    ExitApp
}

SetWorkingDir BinaryFolder

PaneWidth := A_ScreenWidth // 2
GameHeight := A_ScreenHeight * 2 // 3
ConsoleHeight := A_ScreenHeight - GameHeight

; Only PIDs from Run() ever land here -- see LaunchInstance.
pids := []

pid1 := LaunchInstance(1, 0, ServerAddress, LauncherExe)
if !pid1 {
    Cleanup(pids)
    ExitApp
}
pids.Push(pid1)

Sleep 500

pid2 := LaunchInstance(2, PaneWidth, ServerAddress, LauncherExe)
if !pid2 {
    Cleanup(pids)
    ExitApp
}
pids.Push(pid2)

; Both up: verify neither died during warmup.
Sleep 5000
for processId in pids {
    if !ProcessExist(processId) {
        MsgBox "Instance (pid " processId ") died; tearing down."
        Cleanup(pids)
        ExitApp
    }
}

; Launch one instance connected to `address`, dock its windows at left edge x,
; return its PID (0 on timeout).
LaunchInstance(index, x, address, launcherExe) {
    global PaneWidth, GameHeight, ConsoleHeight

    ; The PID must come from Run, not from the dev-console window: a console
    ; window belongs to its host process -- conhost.exe, or one shared
    ; WindowsTerminal.exe serving every console on the machine -- never to the
    ; app. WinGetPID on it matches no game window and would hand ProcessClose
    ; the user's terminal. The launcher PE-loads the game into its own process,
    ; so this PID is the game's.
    ;
    ; The link is quoted whole so its `?` reaches the launcher inside one
    ; argument. The launcher hands it to the client, which connects on its
    ; first menu entry instead of showing the page.
    Run Format('"{}" "kcdc://{}?nickname=Player{}"', launcherExe, address, index), , , &gamePid

    ; Matched on the class CD3D9Renderer registers, not the caption: KCD2
    ; rewrites its title while booting.
    game := WinWait("ahk_class CryENGINE ahk_pid " gamePid,, 180)
    if !game {
        MsgBox "Timeout waiting for game window " index
        ProcessClose gamePid
        return 0
    }

    WinActivate game
    WinSetTitle "kcdc-game-" index, game
    DockWindow(game, x, 0, PaneWidth, GameHeight)

    DockConsole(index, x, GameHeight, PaneWidth, ConsoleHeight)
    return gamePid
}

; Park a window and hold it there. One WinMove is not enough: the renderer
; applies its own rect after creating the window and again once the swap chain
; settles, silently undoing an early move.
DockWindow(hwnd, x, y, w, h) {
    deadline := A_TickCount + 8000
    loop {
        try {
            WinMove x, y, w, h, hwnd
            Sleep 250
            WinGetPos &cx, &cy, &cw, &ch, hwnd
            if (Abs(cx - x) <= 2 && Abs(cy - y) <= 2 && Abs(cw - w) <= 2 && Abs(ch - h) <= 2) {
                return true
            }
        }
    } until A_TickCount > deadline
    return false
}

; Dock the dev console below its game window, when that is safe.
;
; Skipped under Windows Terminal: that process also owns the user's other
; terminal windows, and with the console surfaced as a tab the window carrying
; our title can be one of them. The console then stays where it opened, and
; instance 2 finds and skips it the same way.
DockConsole(index, x, y, w, h) {
    con := WinWait("kcdc: dev-console",, 10)
    if !con {
        return false
    }

    try {
        if (ProcessGetName(WinGetPID(con)) = "WindowsTerminal.exe") {
            return false
        }
    }

    WinSetTitle "kcdc-cli-" index, con
    DockWindow(con, x, y, w, h)
    return true
}

Cleanup(list) {
    for processIdToClose in list {
        if ProcessExist(processIdToClose) {
            ProcessClose processIdToClose
        }
    }
}
