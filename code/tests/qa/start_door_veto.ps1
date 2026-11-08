param(
    [string]$Nickname = 'DoorQA',
    [string]$ObserverNickname = '',
    [ValidateRange(1024, 65534)][int]$Port = 27355,
    [string]$GamePath = 'C:\Program Files (x86)\Steam\steamapps\common\KingdomComeDeliverance2'
)
$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($Nickname)) { throw 'Nickname must not be blank.' }
if ($ObserverNickname -and ([string]::IsNullOrWhiteSpace($ObserverNickname) -or $ObserverNickname -eq $Nickname)) { throw 'ObserverNickname must be distinct and nonblank.' }
$modRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
# KCDC builds on its own against the Framework SDK, into its own build tree.
$buildRoot = Join-Path $modRoot 'builds\build-64'
$bin = Join-Path $buildRoot 'bin'
if (-not (Select-String -LiteralPath (Join-Path $buildRoot 'CMakeCache.txt') -Pattern '^KCDC_AGENT_TESTING:BOOL=ON$' -Quiet)) {
    throw 'Build KCDCClient with -DKCDC_AGENT_TESTING=ON through builds\build.bat first.'
}
if (-not (Test-Path -LiteralPath (Join-Path $GamePath 'Bin\Win64MasterMasterSteamPGO\KingdomCome.exe'))) { throw 'GamePath must name the installed game directory.' }
if (Get-NetUDPEndpoint -LocalPort $Port -ErrorAction SilentlyContinue) { throw "Port $Port is in use; stop the previous QA server or select another port." }
$run = Join-Path $buildRoot ('door-veto-qa-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
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
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'door_veto.js') -Destination (Join-Path $entry 'door_veto_qa.js')
Add-Content -LiteralPath (Join-Path $entry 'index.js') -Value "`nrequire('./door_veto_qa.js')(commands);"
# The client half goes out with the resource's other client files.
$clientEntry = Join-Path $serverDir 'resources\kcdc-gamemode\dist\client'
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'door_veto_client.js') -Destination (Join-Path $clientEntry 'door_veto_client_qa.js')
Add-Content -LiteralPath (Join-Path $clientEntry 'index.js') -Value "`nrequire('./door_veto_client_qa.js')();"
@{ game_executable_name = 'KingdomCome.exe'; game_path = $GamePath; game_path_manual = $false; mod_dll_name = 'KCDCClient.dll'; steam_app_id = 1771300 } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $clientDir 'KCDC_launcher.json')
# The build tree has a server.json only once a server has run from it; one the server fills in is enough.
$configPath = Join-Path $bin 'server.json'
$config = if (Test-Path -LiteralPath $configPath) { Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json } else { [pscustomobject]@{ port = 0; host = ''; mod = [pscustomobject]@{ world_resources = @() } } }
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
$intent = 'Door veto: a refused open leaves the door shut'
Write-Host "QA intent: $intent. Expected: /doorqa server refuses opens on the server and the door swings back shut; /doorqa client refuses them on the player's machine and nothing moves; /doorqa bar takes every door's prompt away. Clients: $($launches.nickname -join ', ')."
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
Write-Host "Record the game PIDs after launch and verify their [Agent testing] titles. Logs and session.json: $run"
