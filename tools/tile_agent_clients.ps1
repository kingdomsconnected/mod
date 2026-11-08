[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [int[]] $ProcessId,
    [ValidateRange(0, 32)]
    [int] $Columns = 0,
    [ValidateRange(0, 100)]
    [int] $Gap = 12,
    [switch] $PlanOnly
)

$ErrorActionPreference = 'Stop'

# Use physical pixels even when PowerShell was started on a scaled monitor.
if (-not ('KcdcAgentWindows' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;

public static class KcdcAgentWindows {
    [StructLayout(LayoutKind.Sequential)]
    public struct Rect { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)]
    public struct MonitorInfo { public int Size; public Rect Monitor, Work; public uint Flags; }
    public delegate bool EnumWindowProc(IntPtr window, IntPtr parameter);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowProc callback, IntPtr parameter);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr window, out uint process);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassName(IntPtr window, StringBuilder text, int count);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowText(IntPtr window, StringBuilder text, int count);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr window);
    [DllImport("user32.dll")] public static extern IntPtr MonitorFromWindow(IntPtr window, uint flags);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern bool GetMonitorInfo(IntPtr monitor, ref MonitorInfo info);
    [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr window);
    [DllImport("user32.dll")] public static extern uint GetWindowLong(IntPtr window, int index);
    [DllImport("user32.dll")] public static extern bool AdjustWindowRectExForDpi(ref Rect rect, uint style, bool menu, uint extendedStyle, uint dpi);
    [DllImport("user32.dll", SetLastError = true)] public static extern bool SetWindowPos(IntPtr window, IntPtr after, int x, int y, int width, int height, uint flags);
    [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);

    public static IntPtr Find(int processId) {
        var matches = new List<IntPtr>();
        EnumWindows((window, parameter) => {
            uint process;
            GetWindowThreadProcessId(window, out process);
            if (process != processId || !IsWindowVisible(window)) return true;
            var name = new StringBuilder(128);
            var title = new StringBuilder(256);
            GetClassName(window, name, name.Capacity);
            GetWindowText(window, title, title.Capacity);
            if (name.ToString() == "CryENGINE" && (title.ToString() == "Kingdoms Connected [Agent testing]" || title.ToString().StartsWith("Kingdoms Connected [Agent testing] | ", StringComparison.Ordinal))) matches.Add(window);
            return true;
        }, IntPtr.Zero);
        if (matches.Count != 1) throw new InvalidOperationException("Expected one visible agent-testing client window for PID " + processId + "; found " + matches.Count);
        if (IsIconic(matches[0])) throw new InvalidOperationException("Restore the minimized client for PID " + processId + " before tiling");
        return matches[0];
    }
}
'@
}

if (($ProcessId | Select-Object -Unique).Count -ne $ProcessId.Count) {
    throw 'Each client PID must appear only once.'
}

$previousDpi = [KcdcAgentWindows]::SetThreadDpiAwarenessContext([IntPtr](-4))
try {
    $windows = @($ProcessId | ForEach-Object { [KcdcAgentWindows]::Find($_) })
    $monitor = [KcdcAgentWindows]::MonitorFromWindow($windows[0], 2)
    $info = New-Object KcdcAgentWindows+MonitorInfo
    $info.Size = [Runtime.InteropServices.Marshal]::SizeOf($info)
    if (-not [KcdcAgentWindows]::GetMonitorInfo($monitor, [ref]$info)) { throw 'Cannot read the first client monitor.' }
    $work = $info.Work

    # Include window borders and the title bar in the grid calculation.
    $frames = @($windows | ForEach-Object {
        $frame = New-Object KcdcAgentWindows+Rect
        if (-not [KcdcAgentWindows]::AdjustWindowRectExForDpi([ref]$frame, [KcdcAgentWindows]::GetWindowLong($_, -16), $false, [KcdcAgentWindows]::GetWindowLong($_, -20), [KcdcAgentWindows]::GetDpiForWindow($_))) {
            throw 'Cannot calculate client window borders.'
        }
        [PSCustomObject]@{ Width = $frame.Right - $frame.Left; Height = $frame.Bottom - $frame.Top }
    })
    $frameWidth = ($frames | Measure-Object Width -Maximum).Maximum
    $frameHeight = ($frames | Measure-Object Height -Maximum).Maximum
    $candidates = if ($Columns) { @([Math]::Min($Columns, $windows.Count)) } elseif ($windows.Count -eq 2) { @(2) } else { @(1..$windows.Count) }
    $layout = @($candidates | ForEach-Object {
        $rows = [int][Math]::Ceiling($windows.Count / [double]$_)
        $cellWidth = [Math]::Floor(($work.Right - $work.Left - $Gap * ($_ + 1)) / $_)
        $cellHeight = [Math]::Floor(($work.Bottom - $work.Top - $Gap * ($rows + 1)) / $rows)
        $scale = [Math]::Min(1.0, [Math]::Min(($cellWidth - $frameWidth) / 1600.0, ($cellHeight - $frameHeight) / 900.0))
        [PSCustomObject]@{ Columns = $_; CellWidth = $cellWidth; CellHeight = $cellHeight; Scale = $scale }
    } | Sort-Object @{ Expression = 'Scale'; Descending = $true }, Columns)[0]

    $units = [int][Math]::Floor(100 * $layout.Scale)
    if ($units -lt 1) { throw 'The selected clients cannot fit in this monitor work area.' }
    $clientWidth = 16 * $units
    $clientHeight = 9 * $units
    for ($index = 0; $index -lt $windows.Count; ++$index) {
        $column = $index % $layout.Columns
        $row = [Math]::Floor($index / $layout.Columns)
        $width = $clientWidth + $frames[$index].Width
        $height = $clientHeight + $frames[$index].Height
        $left = [int]($work.Left + $Gap + $column * ($layout.CellWidth + $Gap) + [Math]::Floor(($layout.CellWidth - $width) / 2))
        $top = [int]($work.Top + $Gap + $row * ($layout.CellHeight + $Gap) + [Math]::Floor(($layout.CellHeight - $height) / 2))
        if (-not $PlanOnly) {
            # SWP_NOZORDER | SWP_NOACTIVATE: keep focus on the user's current app.
            if (-not [KcdcAgentWindows]::SetWindowPos($windows[$index], [IntPtr]::Zero, $left, $top, $width, $height, 0x14)) {
                throw "Could not tile PID $($ProcessId[$index]): $([Runtime.InteropServices.Marshal]::GetLastWin32Error())"
            }
        }
        [PSCustomObject]@{ ProcessId = $ProcessId[$index]; Left = $left; Top = $top; ClientWidth = $clientWidth; ClientHeight = $clientHeight; Planned = [bool]$PlanOnly }
    }
}
finally {
    if ($previousDpi -ne [IntPtr]::Zero) { [void][KcdcAgentWindows]::SetThreadDpiAwarenessContext($previousDpi) }
}
