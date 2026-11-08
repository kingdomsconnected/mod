@echo off
setlocal
if "%~1"=="" goto usage
set "KCDC_ARCH=%~2"
if not "%KCDC_ARCH%"=="32" if not "%KCDC_ARCH%"=="64" goto usage
set "KCDC_TARGET=x64"
set "KCDC_VCVARS=amd64"
set "KCDC_SUFFIX=-64"
if "%KCDC_ARCH%"=="32" (
    set "KCDC_TARGET=x86"
    set "KCDC_VCVARS=amd64_x86"
    set "KCDC_SUFFIX="
)
set "KCDC_CONFIG=%~3"
if not defined KCDC_CONFIG set "KCDC_CONFIG=Debug"
set "KCDC_BUILD=build%KCDC_SUFFIX%"
if "%KCDC_CONFIG%"=="Release" set "KCDC_BUILD=build-prod%KCDC_SUFFIX%"
if "%KCDC_CONFIG%"=="RelWithDebInfo" set "KCDC_BUILD=build-release%KCDC_SUFFIX%"
if not "%KCDC_CONFIG%"=="Debug" if not "%KCDC_CONFIG%"=="Release" if not "%KCDC_CONFIG%"=="RelWithDebInfo" goto usage
if not defined VSCMD_ARG_TGT_ARCH (
    call :setup_msvc
    if errorlevel 1 exit /b 1
)
if not "%VSCMD_ARG_TGT_ARCH%"=="%KCDC_TARGET%" (
    echo An %KCDC_TARGET% compiler environment is required; detected "%VSCMD_ARG_TGT_ARCH%".
    exit /b 1
)
pushd "%~dp0.."
chcp 65001 >nul
rem Where the Framework comes from (cmake/FrameworkBootstrap.cmake): the pinned SDK release by
rem default, FW_SDK_ROOT for a locally installed SDK, FW_SOURCE_DIR to build it from a checkout.
rem Both are cache entries, so switching modes clears the other one instead of letting a stale
rem value win in an existing build tree. The pinned archive is shared by the 32- and 64-bit trees
rem (it carries both architectures) unless FW_SDK_CACHE_DIR points elsewhere.
if not defined FW_SDK_CACHE_DIR set "FW_SDK_CACHE_DIR=%CD%/builds/framework-sdk"
set "KCDC_FRAMEWORK_ARGS=-UFW_SDK_ROOT -UFW_SOURCE_DIR"
if defined FW_SDK_ROOT set "KCDC_FRAMEWORK_ARGS=-DFW_SDK_ROOT=%FW_SDK_ROOT% -UFW_SOURCE_DIR"
if defined FW_SOURCE_DIR set "KCDC_FRAMEWORK_ARGS=-DFW_SOURCE_DIR=%FW_SOURCE_DIR% -UFW_SDK_ROOT"
cmake -S . -B "builds\%KCDC_BUILD%" -G Ninja -DCMAKE_BUILD_TYPE=%KCDC_CONFIG% -DBUILD_SHARED_LIBS=OFF -DCMAKE_MAKE_PROGRAM="%CD%\scripts\ninja_utf8.cmd" -DCMAKE_PROJECT_INCLUDE_BEFORE="%CD%\cmake\MSVCUtf8.cmake" %KCDC_FRAMEWORK_ARGS% %KCDC_CMAKE_ARGS%
if errorlevel 1 goto failed
cmake --build "builds\%KCDC_BUILD%" --target "%~1" --parallel %CMAKE_BUILD_PARALLEL_LEVEL%
if errorlevel 1 goto failed
popd
exit /b 0
:failed
popd
exit /b 1
:usage
echo Usage: builds\build.bat ^<target^> ^<32^|64^> [Debug^|Release^|RelWithDebInfo]
echo   FW_SDK_ROOT=^<dir^>    build against a locally installed Framework SDK
echo   FW_SOURCE_DIR=^<dir^>  build the Framework from a checkout as a subdirectory
exit /b 2
:setup_msvc
set "KCDC_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%KCDC_VSWHERE%" (
    echo Visual Studio Build Tools not found. Use an x64 developer prompt.
    exit /b 1
)
for /f "usebackq tokens=*" %%i in (`"%KCDC_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "KCDC_VS_ROOT=%%i"
if not defined KCDC_VS_ROOT exit /b 1
call "%KCDC_VS_ROOT%\VC\Auxiliary\Build\vcvarsall.bat" %KCDC_VCVARS%
exit /b %errorlevel%
