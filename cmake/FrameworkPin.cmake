# MafiaHub/Framework dependency pin.
#
# KCDC links a prebuilt Framework SDK (cmake/FrameworkBootstrap.cmake), so "which Framework" is part of
# the product, not an environment detail. This file is that record: the release whose archives are
# downloaded at configure time, and the SHA-256 of each archive KCDC consumes. A tag is a mutable ref,
# so the version alone would pin nothing; the hashes are what fix the content, and
# file(DOWNLOAD EXPECTED_HASH) fails the configure if an archive is ever repointed or tampered with.
#
# Plain set(), not CACHE entries: a cached value survives in an existing build tree, so bumping the
# pin here and reconfiguring incrementally would silently keep the old release.
#
# Raising this pin is a release-affecting change: rebuild and re-test the client and the server
# together before tagging. If the new Framework crosses a MAJOR boundary its network build token
# changes (NetworkPeer::BuildToken compares major components), so every peer must update together.
#
# The release notes of every Framework release carry this block ready to paste; or:
# shasum -a 256 Framework-<ver>-*. -DFW_SDK_ROOT or -DFW_SOURCE_DIR (see builds/build.bat) override it.
#
# Before the SDK the pin was the Framework commit in FRAMEWORK_PIN and KCDC built nested in the
# Framework tree; the last commit pin was the one that took the Node 26.12.1 libnode (Framework 37).

# Framework 40.0.0: 38 moved the RPC identifier salt to runtime (InstanceOptions::rpcIdentifierSalt),
# 39 fixed the SDK package config on Linux, 40 ships the scripting-api tooling in the SDK; 40.1 moves voice
# relaying and the client codec off the main thread and lets a client decline a delegated entity; 40.2 serves
# client assets over HTTP on the game port and stops attaching the RakNet statistics plugin; 40.3 reports the
# voice input level only while transmitting and recreates Epic's missing Manifests folder in the launcher.
#
# Framework 41.0.0: the asset download queues on a busy server instead of failing, resumes a cut transfer
# with a fresh retry budget, assembles files from content-addressed chunks, owns the stall watch and the
# failure reason, and can defer lanes past the join and admit downloads beside the player slots. MAJOR:
# AssetPakInfo gained a serialized field, so client and server move together.
set(FRAMEWORK_PIN_VERSION "41.0.0")

# One archive per OS; the Windows one carries the x86 and x64 libraries (KCDC uses x64 only).
set(FRAMEWORK_PIN_SHA256_windows "157032c663e86faadf35f88171142561ffc3a36a06bb355e23d2cd108e8ebb97")
set(FRAMEWORK_PIN_SHA256_linux "643bb845cbff6fc295be8cac950d08621929d1fac4335171466902db4e59cb10")
