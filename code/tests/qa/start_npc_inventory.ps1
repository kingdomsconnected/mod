param(
    [string]$Nickname = 'LootQA',
    [string]$ObserverNickname = '',
    [ValidateRange(1024, 65534)][int]$Port = 27341,
    [string]$GamePath = 'C:\Program Files (x86)\Steam\steamapps\common\KingdomComeDeliverance2'
)
$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($Nickname)) { throw 'Nickname must not be blank.' }
if ($ObserverNickname -and ([string]::IsNullOrWhiteSpace($ObserverNickname) -or $ObserverNickname -eq $Nickname)) { throw 'ObserverNickname must be distinct and nonblank.' }
$modRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$frameworkRoot = (Resolve-Path (Join-Path $modRoot '..\..\..')).Path
$buildRoot = Join-Path $frameworkRoot 'builds\build-64'
$bin = Join-Path $buildRoot 'bin'
if (-not (Select-String -LiteralPath (Join-Path $buildRoot 'CMakeCache.txt') -Pattern '^KCDC_AGENT_TESTING:BOOL=ON$' -Quiet)) {
    throw 'Build KCDCClient with -DKCDC_AGENT_TESTING=ON through builds\build.bat first.'
}
if (-not (Test-Path -LiteralPath (Join-Path $GamePath 'Bin\Win64MasterMasterSteamPGO\KingdomCome.exe'))) { throw 'GamePath must name the installed game directory.' }
if (Get-NetUDPEndpoint -LocalPort $Port -ErrorAction SilentlyContinue) { throw "Port $Port is in use; stop the previous QA server or select another port." }
$run = Join-Path $buildRoot ('npc-loot-qa-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
$serverDir = Join-Path $run 'server'
$clientDir = Join-Path $run 'client'
New-Item -ItemType Directory -Path $serverDir, $clientDir | Out-Null

# Independent runtime copies keep logs, resources and CEF state in the repo.
$clientExecutables = @('KCDCLauncher.exe', 'cef_subprocess.exe', 'crashpad_handler.exe', 'MafiaHubUpdater.exe')
foreach ($file in Get-ChildItem -LiteralPath $bin -File) {
    if ($file.Extension -eq '.dll') { Copy-Item -LiteralPath $file.FullName -Destination $serverDir }
    if ($file.Extension -in @('.dll', '.pak', '.dat', '.bin') -or $file.Name -in $clientExecutables -or $file.Name -in @('kcdc.patterns', 'steam_appid.txt', 'vk_swiftshader_icd.json')) {
        Copy-Item -LiteralPath $file.FullName -Destination $clientDir
    }
}
Copy-Item -LiteralPath (Join-Path $bin 'KCDCServer.exe') -Destination $serverDir
Copy-Item -LiteralPath (Join-Path $bin 'locales') -Destination $clientDir -Recurse
Copy-Item -LiteralPath (Join-Path $bin 'files') -Destination $clientDir -Recurse
New-Item -ItemType Directory -Path (Join-Path $serverDir 'resources') | Out-Null
Copy-Item -LiteralPath (Join-Path $bin 'resources\kcdc-gamemode') -Destination (Join-Path $serverDir 'resources') -Recurse
$entry = Join-Path $serverDir 'resources\kcdc-gamemode\dist\server'
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'npc_inventory.js') -Destination (Join-Path $entry 'npc_inventory_qa.js')
Add-Content -LiteralPath (Join-Path $entry 'index.js') -Value "`nrequire('./npc_inventory_qa.js')(commands);"
@{ game_executable_name = 'KingdomCome.exe'; game_path = $GamePath; game_path_manual = $false; mod_dll_name = 'KCDCClient.dll'; steam_app_id = 1771300 } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $clientDir 'KCDC_launcher.json')
$config = Get-Content -LiteralPath (Join-Path $bin 'server.json') -Raw | ConvertFrom-Json
$config.port = $Port
$config.host = '127.0.0.1'
$config.mod.world_resources = @()
$config | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $serverDir 'server.json')
$launches = @([ordered]@{ nickname = $Nickname; runtime = $clientDir })
if ($ObserverNickname) {
    $observerDir = Join-Path $run 'observer'
    Copy-Item -LiteralPath $clientDir -Destination $observerDir -Recurse
    $launches += [ordered]@{ nickname = $ObserverNickname; runtime = $observerDir }
}
$intent = 'NPC loot: visible knife and shared butchered model'
Write-Host "QA intent: $intent. Expected: knife visible during butchering, removed on exit, identical butchered models, prepared stock with no reroll, shared busy state and safe cancellation. Clients: $($launches.nickname -join ', ')."
$server = Start-Process -FilePath (Join-Path $serverDir 'KCDCServer.exe') -WorkingDirectory $serverDir -WindowStyle Hidden -RedirectStandardOutput (Join-Path $run 'server.stdout.log') -RedirectStandardError (Join-Path $run 'server.stderr.log') -PassThru
$previousIntent = $env:KCDC_TEST_INTENT
$clients = @()
try {
    $env:KCDC_TEST_INTENT = $intent
    foreach ($launch in $launches) {
        $link = 'kcdc://127.0.0.1:' + $Port + '?nickname=' + [Uri]::EscapeDataString($launch.nickname)
        $client = Start-Process -FilePath (Join-Path $launch.runtime 'KCDCLauncher.exe') -WorkingDirectory $launch.runtime -ArgumentList $link -WindowStyle Hidden -RedirectStandardOutput (Join-Path $launch.runtime 'stdout.log') -RedirectStandardError (Join-Path $launch.runtime 'stderr.log') -PassThru
        $clients += [ordered]@{ intent = $intent; nickname = $launch.nickname; runtime = $launch.runtime; launcherPid = $client.Id; gamePid = $client.Id }
    }
} catch {
    foreach ($started in $clients) { Stop-Process -Id $started.launcherPid -ErrorAction SilentlyContinue }
    Stop-Process -Id $server.Id -ErrorAction SilentlyContinue
    throw
} finally { $env:KCDC_TEST_INTENT = $previousIntent }
$session = [ordered]@{ intent = $intent; nickname = $Nickname; port = $Port; serverPid = $server.Id; launcherPid = $clients[0].launcherPid; gamePid = $clients[0].gamePid; serverRuntime = $serverDir; clientRuntime = $clientDir; clients = $clients }
$session | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $run 'session.json')
$session | ConvertTo-Json
Write-Host "Record the game PID after launch and verify its [Agent testing] title and graphics log. Logs and session.json: $run"
