# Kingdoms Connected scripting API

This directory holds KCDC's authoritative scripting API metadata, the shared structural types the
declarations import, and the target reference configuration.

Each binding declares its name, JavaScript signature and documentation in the v8pp call that
installs it. v8pp records those calls into the `kcdc-server` or `kcdc-client` catalog while the
runtime registers its globals, so the registration code *is* the documentation source. There is no
second API registry and no string-lookup layer to keep in step.

## Build

Debug builds refresh the metadata JSON in `generated/` automatically:

- **`KCDCServer`** on boot, as soon as `Server::ModuleRegister` finishes registering the globals.
- **`KCDCClient`** when its scripting initialises, which is on connect rather than on launch.

The output path is baked in at configure time (`KCDC_SCRIPTING_API_METADATA_FILE`); there is no
environment variable, and release builds skip the write entirely. Commit refreshed metadata with the
binding change, so the documentation can be reproduced without launching the game.

After running either binary in a debug build, regenerate the declarations and the public contract
from the refreshed JSON:

```sh
node scripts/build_scripting_contract.mjs --out ./build/scripting-contract
```

Run it from the KCDC repository root, with Node.js and pnpm on the path: the wrapper runs the
generator through `pnpm dlx`. It validates the metadata, regenerates the declarations, and writes
only the allowlisted contract files. The `KCDCScriptingContract` CMake target runs the same wrapper
with `--skip-missing` and writes the bundle to `<build-directory>/scripting-contract/kcdc`; it is
only defined when configure finds both `node` and `pnpm`.

The generator version is pinned in `services-cli.version`, so a local run and CI use the same tool
without anyone exporting anything; bump that file to move it. `MAFIAHUB_SERVICES_CLI_VERSION`
overrides it for a one-off, and generator contributors can point `MAFIAHUB_SERVICES_CLI_ENTRYPOINT`
at a locally built entry point instead of the published package.

Pass `--skip-missing` to build only the targets whose metadata exists. The client's is written on
connect rather than on launch, so a checkout that has never run a debug client has none. A publish
never skips: half a contract is not a contract.

## npm package

Every release tag publishes the committed declarations as `@kingdomsconnected/types` at the
release version -- a prerelease under the `next` dist-tag, a final release under `latest` -- from
the `publish-types` job in `.github/workflows/release.yml`. This directory is the package:
`package.json` is its manifest, with one entry per side -- `@kingdomsconnected/types/server` and
`@kingdomsconnected/types/client`, which a tsconfig names in `types` -- and `files` naming
`shared.d.ts` and the two generated declaration files. `scripts/build_types_package.mjs` copies
those with the release version written in and `package/README.md` as the package's README.

The default gamemode links this directory as that package, so it compiles against exactly what a
release publishes. The copy a release bundle ships depends on the released version instead. An npm
version can never be published twice, so `publish-types` is the last job of a release and runs only
once every build, upload and image has succeeded: a release that fails earlier leaves the version
free to tag again. The `pack-types` job packs the tarball first and hands its integrity to the build
jobs, whose `scripts/stage_gamemode_types.mjs` writes the bundled gamemode's lockfile entry from it
without the registry; the publish at the end uploads that same tarball. Rerunning a release whose
version is already on npm takes the published tarball instead of packing a new one.

## Continuous integration

- `.github/workflows/scripting-contract.yml` regenerates the declarations and fails if the committed
  ones differ. It runs on pull requests and `master` pushes that touch this directory,
  `code/shared/scripting/`, `code/server/src/core/scripting/`, `code/client/src/core/scripting/`,
  the wrapper script or the workflow itself. The feature bindings under `code/*/src/features/` are
  not in that list, so a binding change reaches the check through the metadata in `generated/`
  committed with it. It passes `--skip-missing`, so one missing side does not hold up review of
  the other.
- `.github/workflows/publish-docs.yml` builds the whole contract on a `v*` tag, checks the tag
  against the `VERSION` file, and publishes the immutable bundle to the `docs-contract-testing`
  channel. It needs the `KCDC_SERVICES_API_URL` and `KCDC_SERVICES_MOD_ID` repository variables and
  the `KCDC_DOCS_UPLOAD_TOKEN` secret.

## What the export contains

The framework registers its own globals -- `Events`, `Chat` and `Voice` on both sides, `Web`, `Key`
and `Nametags` on the client, the value types, the timers -- into `framework-server` / `framework-client`, before
`ModuleRegister` is ever called. `ExportCatalog` in `code/shared/scripting/catalog.h` folds those in,
or the file would document only half the API. Where both sides define a symbol -- `Player` above
all -- the mod's definition wins whole rather than being blended. Merging the members produces an
interface extending two types that declare the same property differently, which TypeScript cannot
express.

`Events` is recorded per side, so the generated `EventBus` has exactly what that side's isolate
installs: `emitServer` on the client, `emitAllClients` and `onClient` on the server. `on`, `once`
and `off` also get an overload that types the handler from `EventMap`. Nothing the runtime has is
missing from the declarations, so a resource needs no declaration file of its own to fill gaps.

## Customize

- Edit the v8pp binding call in the owning feature's `code/server/src/features/<name>/*_scripting.cpp`
  or `code/client/src/features/<name>/*_scripting.cpp` for functions, properties, globals and their
  documentation. Regenerate rather than hand-editing anything in `generated/`.
- Declare a native event in the same feature binding, with `events.add_property` on the `EventMap`
  that `EnsureEventMap` in `code/shared/scripting/catalog.h` creates, and change it there when its
  payload changes. `EventMap` and the derived `EventName` type come from those declarations.
- Edit `shared.d.ts` only for structural types that are not runtime globals -- which in practice
  means the framework's own base handles and value types.
- Edit `reference.config.json` only for target declarations and binding-source locations.

Server and client declarations must be loaded separately: they intentionally expose different
versions of `Player`, one with the connection verbs and one without.

Raw V8 callbacks must spell their JavaScript signature out in their C++ metadata descriptor. Their
native callback type carries no parameter or return information for the exporter to read.
