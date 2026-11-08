param(
    [ValidateRange(1, 2)][int]$Clients = 1,
    [int]$Port = 27731,
    [switch]$Manual,
    [switch]$ServerOnly
)
$ErrorActionPreference = 'Stop'
$modRoot = Split-Path -Parent $PSScriptRoot
$frameworkRoot = (Resolve-Path (Join-Path $modRoot '..\..\..')).Path
$build = Join-Path $frameworkRoot 'builds\build-64'
$bin = Join-Path $build 'bin'
if (-not $ServerOnly -and -not (Select-String -LiteralPath (Join-Path $build 'CMakeCache.txt') -Pattern '^KCDC_AGENT_TESTING:BOOL=ON$' -Quiet)) { throw 'Build KCDCClient with KCDC_AGENT_TESTING=ON first.' }
$clientCount = if ($ServerOnly) { 0 } else { $Clients }
if ((Get-NetUDPEndpoint -LocalPort $Port -ErrorAction SilentlyContinue) -or (Get-NetTCPConnection -LocalPort ($Port + 1) -ErrorAction SilentlyContinue)) { throw 'QA ports are already occupied.' }
$intent = 'Custom buffs: native effects and live XP rollback'
$expected = 'Custom effects attach, refresh, stack, tick and expire; native stats change; online levels and XP can decrease; exact snapshots restore.'
# Bundles and evidence live here; the runtimes launch only from the permanent
# QA slots, as docs/kcdc-qa-runtime.md requires.
$runId = 'custom-buff-qa-' + (Get-Date -Format 'yyyyMMdd-HHmmss')
$run = Join-Path $build $runId
$slots = Join-Path $modRoot 'scripts\kcdc-qa-slots.ps1'
$serverDir = Join-Path $run 'server'
New-Item -ItemType Directory -Path $serverDir | Out-Null
$clientExecutables = @('KCDCLauncher.exe', 'cef_subprocess.exe', 'crashpad_handler.exe', 'MafiaHubUpdater.exe')
$clientDirectories = @()
for ($index = 0; $index -lt $clientCount; ++$index) {
    $clientDir = Join-Path $run ('client-' + $index)
    New-Item -ItemType Directory -Path $clientDir | Out-Null
    foreach ($file in Get-ChildItem -LiteralPath $bin -File) {
        if ($file.Extension -in @('.dll', '.pak', '.dat', '.bin') -or $file.Name -in $clientExecutables -or $file.Name -in @('kcdc.patterns', 'steam_appid.txt', 'vk_swiftshader_icd.json')) { Copy-Item -LiteralPath $file.FullName -Destination $clientDir }
    }
    foreach ($directory in @('locales', 'files')) { Copy-Item -LiteralPath (Join-Path $bin $directory) -Destination $clientDir -Recurse }
    Copy-Item -LiteralPath (Join-Path $bin 'KCDC_launcher.json') -Destination $clientDir
    $clientDirectories += $clientDir
}
foreach ($file in Get-ChildItem -LiteralPath $bin -File) {
    if (($file.Extension -eq '.dll' -and $file.Name -ne 'KCDCClient.dll') -or $file.Name -eq 'KCDCServer.exe') { Copy-Item -LiteralPath $file.FullName -Destination $serverDir }
}
New-Item -ItemType Directory -Path (Join-Path $serverDir 'resources') | Out-Null
Copy-Item -LiteralPath (Join-Path $bin 'resources\kcdc-gamemode') -Destination (Join-Path $serverDir 'resources') -Recurse
$entry = Join-Path $serverDir 'resources\kcdc-gamemode\dist\server'
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'qa_custom_buffs.js') -Destination (Join-Path $entry 'buff_qa.js')
$auto = if ($Manual) { 'false' } else { 'true' }
Add-Content -LiteralPath (Join-Path $entry 'index.js') -Value ("`nrequire('./buff_qa.js')(commands, {auto: " + $auto + '});')
$config = Get-Content -LiteralPath (Join-Path $bin 'server.json') -Raw | ConvertFrom-Json
$config.port = $Port
$config.apiport = $Port + 1
$config.host = '127.0.0.1'
$config.apihost = '127.0.0.1'
$config.mod.world_resources = @()
foreach ($setting in @('npcEnabled', 'npcAmbientHumans', 'npcAnimals')) { $config.mod | Add-Member -NotePropertyName $setting -NotePropertyValue $false -Force }
$config | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $serverDir 'server.json')
$session = [ordered]@{
    intent = $intent; expected = $expected; port = $Port; runId = $runId; evidence = $run; slots = @(); serverRuntime = $null; serverPid = $null; clients = @()
    revision = (git -C $modRoot rev-parse HEAD)
    frameworkRevision = (git -C $frameworkRoot rev-parse HEAD)
    workingTreeChanges = @(git -C $modRoot status --short)
    clientSha256 = $(if ($clientCount -gt 0) { (Get-FileHash -LiteralPath (Join-Path $clientDirectories[0] 'KCDCClient.dll')).Hash } else { $null })
    serverSha256 = (Get-FileHash -LiteralPath (Join-Path $serverDir 'KCDCServer.exe')).Hash
}
$sessionPath = Join-Path $run 'session.json'
try {
    Write-Host "Test intent: $intent. Expected: $expected"
    # Stage every bundle before anything launches: staging refuses while a game process runs.
    $session.slots += 'server'
    $session | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $sessionPath
    $session.serverRuntime = @(& $slots -Action Stage -Slot server -RunId $runId -Source $serverDir)[-1]
    $clientRuntimes = @()
    for ($index = 0; $index -lt $clientCount; ++$index) {
        $slot = 'client-' + [char](97 + $index)
        $session.slots += $slot
        $session | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $sessionPath
        $clientRuntimes += @(& $slots -Action Stage -Slot $slot -RunId $runId -Source $clientDirectories[$index])[-1]
    }
    $launched = (& $slots -Action Launch -Slot server -RunId $runId -Executable 'KCDCServer.exe' | Out-String) | ConvertFrom-Json
    $server = Get-Process -Id $launched.processId
    $session.serverPid = $server.Id
    $session.serverStartedAt = $server.StartTime.ToUniversalTime().ToString('o')
    $session | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $sessionPath
    $deadline = (Get-Date).AddSeconds(45)
    while (-not (Get-NetUDPEndpoint -LocalPort $Port -ErrorAction SilentlyContinue | Where-Object OwningProcess -eq $server.Id)) {
        if ($server.HasExited -or (Get-Date) -gt $deadline) { throw "QA server did not start; read the logs in $($session.serverRuntime)" }
        Start-Sleep -Milliseconds 500
    }
    $previousIntent = $env:KCDC_TEST_INTENT
    try {
        $env:KCDC_TEST_INTENT = $intent
        for ($index = 0; $index -lt $clientCount; ++$index) {
            $nickname = 'BuffQA_' + [char](65 + $index)
            $slot = 'client-' + [char](97 + $index)
            $link = 'kcdc://127.0.0.1:' + $Port + '?nickname=' + [Uri]::EscapeDataString($nickname)
            $launched = (& $slots -Action Launch -Slot $slot -RunId $runId -Executable 'KCDCLauncher.exe' -Arguments $link | Out-String) | ConvertFrom-Json
            $client = Get-Process -Id $launched.processId
            $session.clients += [ordered]@{ intent = $intent; nickname = $nickname; slot = $slot; runtime = $clientRuntimes[$index]; gamePid = $client.Id; startedAt = $client.StartTime.ToUniversalTime().ToString('o') }
            $session | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $sessionPath
            Write-Host "Client $nickname; PID $($client.Id); slot $slot"
        }
    } finally { $env:KCDC_TEST_INTENT = $previousIntent }
    if ($clientCount -gt 1) {
        $deadline = (Get-Date).AddSeconds(60)
        do {
            $ready = @($session.clients | Where-Object { (Get-Process -Id $_.gamePid -ErrorAction SilentlyContinue).MainWindowTitle -like '*[[]Agent testing[]]*' })
            if ($ready.Count -eq $clientCount) { & (Join-Path $PSScriptRoot 'tile_agent_clients.ps1') -ProcessId @($session.clients.gamePid) | Out-Host; break }
            Start-Sleep -Milliseconds 500
        } while ((Get-Date) -lt $deadline)
    }
    Write-Output $sessionPath
} catch {
    if (Test-Path -LiteralPath $sessionPath) { & (Join-Path $PSScriptRoot 'stop_custom_buff_qa.ps1') -SessionPath $sessionPath }
    throw
}
