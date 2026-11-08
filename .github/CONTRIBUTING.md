# Contributing to the project

There are many ways to get involved in Kingdoms Connected:

## Report bugs

If you stumble upon an issue, report it on the
[Issues page](https://github.com/KingdomsConnected/mod/issues). Include the
KCDC version, game version, steps to reproduce it, and what you expected to
happen. Relevant logs and screenshots help. Remove passwords, tokens and
personal information before attaching them. If you know how to solve it,
open a pull request with the fix.

## Suggest new features

If you have an idea that fits the project, open an issue and describe what
players or server creators could do with it. Discuss larger changes with
the maintainers before starting work, especially new dependencies or changes
to shared Framework functionality.

## Code

We're always looking for contributors. Fixes, features, documentation and
examples are all useful contributions. Send KCDC changes to this repository;
changes to shared functionality belong in the
[MafiaHub Framework](https://github.com/MafiaHub/Framework).

KCDC is source available under [LICENSE.txt](../LICENSE.txt). Forks, source
builds and modifications are for developing and testing upstream
contributions. Community servers must use official binaries, including when
the source used for a local build is unchanged. Do not deploy source-built
or modified clients or servers for a community or separate platform.
Submit the change through a pull request for an official release.

Custom or alternative launchers require written permission. You may develop
and test changes to the official launcher for an upstream contribution. Configuring
official binaries and customizing resources and game modes through their
supported scripting APIs remain supported. Read the license for the
controlling terms.

Follow the [build instructions](../docs/building.md) from the KCDC
repository root. KCDC downloads the pinned Framework SDK automatically;
it does not need to be placed inside a Framework checkout. Use
`FW_SOURCE_DIR` when a change needs both repositories built from source.

For native changes, build the affected targets and run the Windows test
suite and style check:

```bat
builds\build.bat RunKCDCTests 64
python scripts/check_braces.py
```

Add native tests to the existing `KCDCTests` target. Gameplay changes also
need in-game testing with a matching client and server. Describe what you
tested in the pull request, including anything you could not test.

For the default game mode, install its dependencies and run its tests:

```sh
npm --prefix resources/kcdc-gamemode ci
npm --prefix resources/kcdc-gamemode test
```

For scripting API changes, update the binding documentation and regenerate
the metadata and declarations using the
[scripting API guide](../scripting-api/README.md). Update the relevant
[guides](../docs/README.md) with changes to behavior, controls or configuration.

## Version changes

We follow the Framework's versioning convention to keep networked peers and
scripts compatible:

- PATCH for changes that do not affect peer synchronization or scripting.
- MINOR for changes to the scripting layer.
- MAJOR for changes to netcode, shared ECS modules or synchronization between
  peers. Both the client and server must update together.

Note the version impact in your pull request. Versions are raised by hand
in [`VERSION`](../VERSION); the matching `v<version>` tag starts a release.
Changing [`cmake/FrameworkPin.cmake`](../cmake/FrameworkPin.cmake) also needs
testing on both peers, and a Framework MAJOR change requires both to update.

## Code submission policy

Please follow these rules when contributing:

- Follow the Framework's
  [coding style](https://github.com/MafiaHub/Framework/blob/develop/.github/CODING_STYLE.md)
  and KCDC's [coding instructions](../AGENTS.md). Use this repository's
  [`.clang-format`](../.clang-format) for changed C++ code. KCDC's instructions
  take precedence where the two differ.
- Submit only material you have the right to contribute under this
  repository's license. Retain any required third-party notices.
- Discuss new external dependencies with the code owners before adding them.
- Split changes into separate, atomic commits and keep unrelated work out of
  the pull request.
- Rebase your changes on this repository's default branch and target that
  branch in the pull request.
- Use `Module: Brief commit description` for commit subjects. The module can
  name a file, system or process. Wrap commit messages at 72 characters.
- Use clear English and explain the reason for changes that need more than
  the commit subject. A longer description is optional for simple changes.
