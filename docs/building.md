# Building KCDC

Build KCDC from this repository's root. The build downloads the Framework
SDK release recorded in [`cmake/FrameworkPin.cmake`](../cmake/FrameworkPin.cmake)
and verifies its archive hash. Set `FW_SOURCE_DIR` to a Framework checkout
to build it from source, or `FW_SDK_ROOT` to a locally installed SDK.

Source builds are for developing and testing upstream contributions. Use
official binaries for community servers; see [the license](../LICENSE.txt).

## Windows

On Windows, install Visual Studio 2022 or its Build Tools with the C++ tools
and Windows SDK, CMake 3.24 or newer, Ninja, Node.js 22 with npm, and Python 3.
The client and launcher are Windows x64 targets. A game installation is
needed to run the client.

```bat
git clone https://github.com/KingdomsConnected/mod.git
cd mod
npm --prefix resources/kcdc-gamemode ci
```

Build through the script. It loads the matching Visual Studio environment
and configures the 64-bit Debug tree at `builds\build-64`:

```bat
builds\build.bat KCDCClient 64
builds\build.bat KCDCLauncher 64
builds\build.bat KCDCServer 64
builds\build.bat RunKCDCTests 64
```

The optional third argument selects `Debug`, `Release` or `RelWithDebInfo`.
Use a client and server built from the same checkout for testing. Official
releases use a different RPC salt, so a local build cannot join them by
default.

The in-game menu is a Preact application under [`ui/`](../ui/README.md). Its
built assets are committed to `files/kcdc/ui`, so a native-only build still
packages a working menu; when `node_modules` is present, `KCDCClient` rebuilds
the frontend before staging assets.

```powershell
cd ui
npm ci
npm run build
```

Run `scripts\package_client.bat` to package a local client build into
`builds\dist` as a folder and a `.zip`. Pass `--skip-build` to package an
existing build, `--no-zip` for the folder alone, or `--with-symbols` to include
the PDBs.

## Linux server

Linux builds produce the dedicated server. The client, launcher and native
test target currently require Windows. The release workflow uses Ubuntu
24.04 with CMake, Ninja, a C++ compiler, the curl development package,
Autoconf, Automake and Libtool. Node.js 22 with npm builds the default game
mode.

From the repository root:

```sh
npm --prefix resources/kcdc-gamemode ci
cmake -S . -B build-server-linux -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build-server-linux --target KCDCServer
```

The server and its runtime dependencies are staged in `build-server-linux/bin`.
To use a Framework checkout or local SDK on Linux, pass
`-DFW_SOURCE_DIR=/path/to/Framework` or `-DFW_SDK_ROOT=/path/to/sdk` when
configuring.
