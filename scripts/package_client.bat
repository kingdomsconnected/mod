@echo off
setlocal enabledelayedexpansion

REM ============================================================================
REM  KCDC standalone client packager
REM
REM  Builds the client, launcher and CEF helper through the canonical build
REM  script, then assembles a self-contained folder (and .zip) that a player can
REM  unpack anywhere and run:
REM
REM    KCDC-<VERSION>-<SHA>\
REM      KCDCLauncher.exe         bootstrapper - run this
REM      KCDCClient.dll           the mod, PE-loaded into the launcher process
REM      FrameworkLoaderData.dll  loader payload the launcher imports
REM      fw_steam_api64.dll       Steam bridge used to locate the game
REM      MafiaHubUpdater.exe      applies a downloaded update; release builds only
REM      libcef.dll + friends     CEF runtime behind the multiplayer menu
REM      libnode.dll              scripting runtime the client imports
REM      locales\                 CEF locale packs
REM      files\kcdc\              the mod's own mounted data (UI, souls, splash)
REM      LICENSE.txt + NOTICE.txt project license and attribution
REM      licenses\               bundled Framework license
REM
REM  The build tree's bin\ is SHARED by every project in this repo, so nothing is
REM  bulk-copied out of it: each runtime file is named explicitly and the mounted
REM  data comes from the project's own files\ tree. Files from other builds
REM  are excluded from the archive.
REM
REM  Deliberately NOT packaged:
REM    KCDC_launcher.json   machine-specific; the launcher regenerates it from
REM                         Steam on first run (app 1771300)
REM    steam_appid.txt      the launcher writes it before it touches Steam
REM    cache\ logs\ launcher_cache\   runtime state
REM    *.pdb                unless --with-symbols is passed (KCDCClient.pdb
REM                         alone is ~330 MB)
REM
REM  Usage:  package_client.bat [options]
REM    --skip-build     package whatever is already in builds\build-64
REM    --no-zip         assemble the folder only, do not create the .zip
REM    --with-symbols   include KCDCClient.pdb / KCDCLauncher.pdb
REM    --out <dir>      write the distribution under <dir> (default builds\dist)
REM ============================================================================

REM %~dp0 is captured up front: parsing options below uses shift, which
REM renumbers %0 and would make %~dp0 resolve against the current directory.
set "SCRIPT_DIR=%~dp0"
for %%i in ("%SCRIPT_DIR%..") do set "PROJECT_DIR=%%~fi"
set "BIN=%PROJECT_DIR%\builds\build-64\bin"
set "DIST_ROOT=%PROJECT_DIR%\builds\dist"

set "DO_BUILD=1"
set "DO_ZIP=1"
set "DO_SYMBOLS=0"

:parse_args
if "%~1"=="" goto args_done
if /I "%~1"=="--skip-build"   ( set "DO_BUILD=0"     & shift & goto parse_args )
if /I "%~1"=="--no-zip"       ( set "DO_ZIP=0"       & shift & goto parse_args )
if /I "%~1"=="--with-symbols" ( set "DO_SYMBOLS=1"   & shift & goto parse_args )
if /I "%~1"=="--out"          ( set "DIST_ROOT=%~f2" & shift & shift & goto parse_args )
if /I "%~1"=="--help" goto usage
if /I "%~1"=="-h"     goto usage
echo [ERROR] Unknown option: %~1
goto usage
:args_done

REM ---------------------------------------------------------------------------
REM Resolve the archive name: KCDC-<VERSION>-<SHA>.
REM ---------------------------------------------------------------------------
set "VER=dev"
if exist "%PROJECT_DIR%\VERSION" set /p VER=<"%PROJECT_DIR%\VERSION"
if "%VER%"=="" set "VER=dev"

set "SHA=local"
for /f "delims=" %%i in ('git -C "%PROJECT_DIR%" rev-parse --short^=7 HEAD 2^>nul') do set "SHA=%%i"

set "NAME=KCDC-%VER%-%SHA%"
set "DIST=%DIST_ROOT%\%NAME%"
set "ZIP=%DIST_ROOT%\%NAME%.zip"

echo [*] Packaging %NAME%

REM ---------------------------------------------------------------------------
REM 1. Build through builds\build.bat - the only supported way to drive the
REM    canonical 64-bit tree. KCDCLauncher links KCDCClient, but both are named
REM    so a failure points at the right target.
REM ---------------------------------------------------------------------------
if "%DO_BUILD%"=="0" goto skip_build

REM Each target goes through :build rather than a for body: a failing build has
REM to abort the packaging, and errorlevel read inside a parenthesised block is
REM unreliable.
call :build KCDCClient
if errorlevel 1 exit /b 1
call :build KCDCLauncher
if errorlevel 1 exit /b 1
call :build cef_subprocess
if errorlevel 1 exit /b 1

:skip_build

REM ---------------------------------------------------------------------------
REM 2. The runtime set. REQUIRED is fatal when absent; OPTIONAL covers the CEF
REM    extras that only some configurations stage.
REM ---------------------------------------------------------------------------
set "REQUIRED=KCDCClient.dll KCDCLauncher.exe FrameworkLoaderData.dll fw_steam_api64.dll"
set "REQUIRED=%REQUIRED% cef_subprocess.exe libcef.dll chrome_elf.dll libEGL.dll libGLESv2.dll"
set "REQUIRED=%REQUIRED% icudtl.dat v8_context_snapshot.bin resources.pak chrome_100_percent.pak chrome_200_percent.pak"
set "REQUIRED=%REQUIRED% libnode.dll discord_game_sdk.dll d3dcompiler_47.dll"

REM MafiaHubUpdater.exe is optional here and required in a release: only a
REM patcher-enabled build (KCDC_PATCHER_ENABLED) ever runs it.
set "OPTIONAL=MafiaHubUpdater.exe crashpad_handler.exe crashpad_wer.dll dxcompiler.dll dxil.dll"
set "OPTIONAL=%OPTIONAL% vk_swiftshader.dll vk_swiftshader_icd.json vulkan-1.dll"
set "OPTIONAL=%OPTIONAL% libcrypto-3-x64.dll libssl-3-x64.dll"

if not exist "%PROJECT_DIR%\files\kcdc\ui\index.html" (
    echo [ERROR] The multiplayer UI is not built: %PROJECT_DIR%\files\kcdc\ui\index.html
    echo         Run "npm install" then "npm run build" in %PROJECT_DIR%\ui and rebuild the client.
    exit /b 1
)

set "MISSING="
for %%f in (%REQUIRED%) do if not exist "%BIN%\%%f" set "MISSING=!MISSING! %%f"
if not exist "%BIN%\locales\en-US.pak" set "MISSING=!MISSING! locales\en-US.pak"
if not "!MISSING!"=="" (
    echo [ERROR] Missing from %BIN%:!MISSING!
    echo         Build first, or drop --skip-build.
    exit /b 1
)
for %%f in (LICENSE.txt NOTICE.txt) do if not exist "%PROJECT_DIR%\%%f" (
    echo [ERROR] Missing project notice: %%f
    exit /b 1
)
if not exist "%BIN%\licenses\Framework-LICENSE.txt" (
    echo [ERROR] Missing bundled Framework license. Reconfigure the build first.
    exit /b 1
)

REM ---------------------------------------------------------------------------
REM 3. Assemble the folder.
REM ---------------------------------------------------------------------------
echo [*] Assembling %DIST%
if exist "%DIST%" rmdir /S /Q "%DIST%"
mkdir "%DIST%" 2>nul
if not exist "%DIST%" (
    echo [ERROR] Could not create %DIST%
    exit /b 1
)

for %%f in (%REQUIRED%) do (
    copy /Y "%BIN%\%%f" "%DIST%\" >nul
    if errorlevel 1 (
        echo [ERROR] Failed to copy %%f
        exit /b 1
    )
)
for %%f in (%OPTIONAL%) do if exist "%BIN%\%%f" copy /Y "%BIN%\%%f" "%DIST%\" >nul
for %%f in (LICENSE.txt NOTICE.txt) do (
    copy /Y "%PROJECT_DIR%\%%f" "%DIST%\" >nul
    if errorlevel 1 (
        echo [ERROR] Failed to copy %%f
        exit /b 1
    )
)
mkdir "%DIST%\licenses" 2>nul
copy /Y "%BIN%\licenses\Framework-LICENSE.txt" "%DIST%\licenses\" >nul
if errorlevel 1 (
    echo [ERROR] Failed to copy the bundled Framework license.
    exit /b 1
)

robocopy "%BIN%\locales" "%DIST%\locales" /E /NFL /NDL /NJH /NJS /NP >nul
if errorlevel 8 (
    echo [ERROR] robocopy locales failed ^(code %errorlevel%^).
    exit /b 1
)

REM The mod's mounted data. The project tree is authoritative - it is where the
REM Preact build writes files\kcdc\ui - and bin\files\kcdc is overlaid on top
REM for anything staged there but untracked, such as servers.tsv.
robocopy "%PROJECT_DIR%\files" "%DIST%\files" /E /NFL /NDL /NJH /NJS /NP /XF .gitkeep >nul
if errorlevel 8 (
    echo [ERROR] robocopy files failed ^(code %errorlevel%^).
    exit /b 1
)
if exist "%BIN%\files\kcdc" (
    robocopy "%BIN%\files\kcdc" "%DIST%\files\kcdc" /E /NFL /NDL /NJH /NJS /NP >nul
    if errorlevel 8 (
        echo [ERROR] robocopy staged files failed ^(code %errorlevel%^).
        exit /b 1
    )
)
REM Fonts come only from the source tree, including their license. Do not
REM bring back stale fonts from an older build when packaging with --skip-build.
if exist "%DIST%\files\kcdc\fonts" rmdir /S /Q "%DIST%\files\kcdc\fonts"
if exist "%DIST%\files\kcdc\fonts" (
    echo [ERROR] Could not clear the staged fonts directory.
    exit /b 1
)
robocopy "%PROJECT_DIR%\files\kcdc\fonts" "%DIST%\files\kcdc\fonts" /E /NFL /NDL /NJH /NJS /NP >nul
if errorlevel 8 (
    echo [ERROR] robocopy fonts failed ^(code %errorlevel%^).
    exit /b 1
)
if not exist "%DIST%\files\kcdc\servers.tsv" echo [!] No files\kcdc\servers.tsv - the in-game server browser will start empty.

if "%DO_SYMBOLS%"=="1" (
    echo [*] Including symbols...
    for %%p in (KCDCClient.pdb KCDCLauncher.pdb) do if exist "%BIN%\%%p" copy /Y "%BIN%\%%p" "%DIST%\" >nul
)

REM ---------------------------------------------------------------------------
REM 4. Player-facing note.
REM ---------------------------------------------------------------------------
(
    echo KCDC %VER% ^(%SHA%^)
    echo.
    echo Kingdom Come: Deliverance II must be installed through Steam, and the
    echo Steam client must be running. The launcher asks Steam for the install
    echo directory, so nothing here has to be copied into the game folder and the
    echo game's own files are never modified.
    echo.
    echo   1. Unpack this folder anywhere you like.
    echo   2. Start Steam.
    echo   3. Run KCDCLauncher.exe.
    echo.
    echo On the first run the launcher writes KCDC_launcher.json next to itself
    echo with the resolved game path. Delete that file to have it detected again.
    echo.
    echo Servers listed in the in-game browser come from files\kcdc\servers.tsv,
    echo one per line, tab separated: name, host, port and an optional password.
    echo The browser reloads it every time you enter the main menu.
    echo.
    echo Logs are written to logs\ next to the launcher.
    echo.
    echo Needs the Microsoft Visual C++ 2015-2022 x64 redistributable, which the
    echo game itself already installs.
) > "%DIST%\README.txt"

REM ---------------------------------------------------------------------------
REM 5. Zip it. Compressing the folder itself keeps the KCDC-<VER>-<SHA>\ top
REM    directory inside the archive.
REM ---------------------------------------------------------------------------
if "%DO_ZIP%"=="1" (
    echo [*] Creating archive: %ZIP%
    if exist "%ZIP%" del /Q "%ZIP%"
    powershell -NoProfile -ExecutionPolicy Bypass -Command "Compress-Archive -Path '%DIST%' -DestinationPath '%ZIP%' -Force"
    if errorlevel 1 (
        echo [ERROR] Archive creation failed.
        exit /b 1
    )
)

echo.
echo ============================================================================
echo  Done.
echo    Folder : %DIST%
if "%DO_ZIP%"=="1" echo    Archive: %ZIP%
echo.
echo  Run KCDCLauncher.exe with Steam running. No files are copied into the game
echo  install; the launcher maps KingdomCome.exe into its own process.
echo ============================================================================
exit /b 0

REM ---------------------------------------------------------------------------
:build
echo [*] Building %~1 ^(64-bit^)...
call "%PROJECT_DIR%\builds\build.bat" %~1 64
set "BUILD_RC=!errorlevel!"
if not "!BUILD_RC!"=="0" echo [ERROR] %~1 build failed ^(exit code !BUILD_RC!^).
exit /b !BUILD_RC!

REM ---------------------------------------------------------------------------
:usage
echo.
echo Usage: package_client.bat [--skip-build] [--no-zip] [--with-symbols] [--out ^<dir^>]
echo.
echo   --skip-build     package whatever is already in builds\build-64
echo   --no-zip         assemble the folder only, do not create the .zip
echo   --with-symbols   include KCDCClient.pdb / KCDCLauncher.pdb
echo   --out ^<dir^>      write the distribution under ^<dir^> ^(default builds\dist^)
exit /b 1
