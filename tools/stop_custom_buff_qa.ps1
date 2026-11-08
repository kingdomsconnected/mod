param([Parameter(Mandatory)][string]$SessionPath)
$ErrorActionPreference = 'Stop'
$session = Get-Content -LiteralPath $SessionPath -Raw | ConvertFrom-Json
$slots = Join-Path (Split-Path -Parent $PSScriptRoot) 'scripts\kcdc-qa-slots.ps1'
$stopped = @()
foreach ($entry in @($session.clients) + @([pscustomobject]@{ gamePid = $session.serverPid; startedAt = $session.serverStartedAt; runtime = $session.serverRuntime })) {
    if ($null -eq $entry.gamePid) { continue }
    $process = Get-Process -Id $entry.gamePid -ErrorAction SilentlyContinue
    if ($null -eq $process) { continue }
    if ($process.StartTime.ToUniversalTime().Ticks -ne ([datetime]$entry.startedAt).ToUniversalTime().Ticks) { throw 'PID was reused; refusing to stop an unrelated process.' }
    $directory = [IO.Path]::GetFullPath($entry.runtime).TrimEnd('\') + '\'
    if (-not $process.Path.StartsWith($directory, [StringComparison]::OrdinalIgnoreCase)) {
        # The launcher maps KingdomCome.exe, which changes its reported image path.
        $executable = Join-Path $entry.runtime $(if ($entry.nickname) { 'KCDCLauncher.exe' } else { 'KCDCServer.exe' })
        $expectedPrefix = '"' + $executable + '"'
        $nativeProcess = Get-CimInstance Win32_Process -Filter ('ProcessId = ' + $process.Id)
        $commandLine = $nativeProcess.CommandLine
        if (-not $commandLine -or -not $commandLine.StartsWith($expectedPrefix, [StringComparison]::OrdinalIgnoreCase) -or ($commandLine.Length -gt $expectedPrefix.Length -and $commandLine[$expectedPrefix.Length] -ne ' ')) { throw 'Process is outside its recorded QA runtime.' }
    }
    Stop-Process -Id $process.Id
    $stopped += $process
}
foreach ($process in $stopped) { $process.WaitForExit(30000) | Out-Null }

# The slots stay leased until the ports are free and the evidence is out of them.
$deadline = (Get-Date).AddSeconds(30)
while ((Get-NetUDPEndpoint -LocalPort $session.port -ErrorAction SilentlyContinue) -or (Get-NetTCPConnection -LocalPort ($session.port + 1) -State Listen -ErrorAction SilentlyContinue)) {
    if ((Get-Date) -gt $deadline) { throw "QA ports $($session.port)/$($session.port + 1) are still bound; the slots stay leased to $($session.runId)." }
    Start-Sleep -Milliseconds 500
}
if ($session.serverRuntime -and (Test-Path -LiteralPath $session.serverRuntime)) {
    foreach ($item in @(Get-ChildItem -LiteralPath $session.serverRuntime -Filter 'buff-qa-*.json') + @(Get-Item -LiteralPath (Join-Path $session.serverRuntime 'logs') -ErrorAction SilentlyContinue)) {
        Copy-Item -LiteralPath $item.FullName -Destination $session.evidence -Recurse -Force
    }
}
foreach ($slot in @($session.slots)) {
    # A slot this run never got to stage may still belong to another run.
    $lease = (& $slots -Action Status -Slot $slot | Out-String).Trim()
    if ($lease -and $lease -ne 'null' -and ($lease | ConvertFrom-Json).runId -eq $session.runId) {
        & $slots -Action Release -Slot $slot -RunId $session.runId
    }
}
$session | Add-Member -NotePropertyName stoppedAt -NotePropertyValue ([DateTime]::UtcNow.ToString('o')) -Force
$session | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $SessionPath
Write-Output "Stopped recorded QA processes and released slots: $SessionPath"
