# Hosting KCDC with Pterodactyl

## How the pieces fit together

Pterodactyl Panel is the web interface for creating servers, viewing their
consoles, editing files and managing users. Wings is the service installed on
a Linux node that runs those servers in Docker. A nest groups related games;
an egg is an importable JSON recipe describing one game's image, startup
command, settings, readiness message, installer and stop behaviour.

The egg describes how to run the server. The public image,
`ghcr.io/kingdomsconnected/kcdc-server:<version>`, carries the stripped native
runtime, and the same image works with Docker, Compose and Pterodactyl. There
is no separate Pterodactyl tag or registry.

The image follows Pterodactyl's conventions: a user named `container`, `HOME`
and the working directory at `/home/container`, and support for Wings'
`STARTUP` template. Wings chooses the numeric runtime UID/GID and mounts that
server's files into the home directory. Everything KCDC writes stays there,
so the rest of the image can be read-only.

## Get the egg

Each release embeds its matching `egg-kcdc.json` in the public image. Extract
it without starting the server:

```sh
IMAGE='ghcr.io/kingdomsconnected/kcdc-server:<version>'
docker pull "$IMAGE"
CONTAINER=$(docker create "$IMAGE")
docker cp "$CONTAINER:/usr/share/kcdc/egg-kcdc.json" ./egg-kcdc.json
docker rm -v "$CONTAINER"
```

Extraction needs Docker but no GitHub login. The image and its egg are
versioned together, so an imported egg
points at the image it was extracted from.

The release workflow also attaches `egg-kcdc.json` to the
[GitHub release](https://github.com/KingdomsConnected/mod/releases). Use the
egg from the same release as the server image.

## Import and create a server

These steps assume a working Pterodactyl 1.x Panel and Wings installation on
a Linux amd64 node.

1. In the Panel's administrator area, go to **Nests** and create a **KCDC**
   nest if needed. Use **Import Egg**, select that nest and upload
   `egg-kcdc.json`.
2. In **Nodes → your node → Allocation**, add one available port — for
   example `27015`. The game session uses it over UDP and the HTTP
   endpoints over TCP.
3. Create a server on the KCDC nest and egg. Set its primary allocation to
   that port and select the versioned KCDC image from the egg. The port
   comes from Wings' built-in `SERVER_PORT`; the egg has no port variable.
4. Choose resource limits. Two CPU cores, 2 GiB RAM and 5 GiB disk is a
   starting budget to measure against, not production sizing. The native
   server needs no database, though a gamemode might.
5. Start it. The console should show `KCDC Server successfully started`,
   which the egg uses to mark the server running. The default gamemode is
   seeded into `resources/` on that first start, so the server answers
   commands straight away.

The installer only creates data directories. It downloads no source, needs no
Steam credentials, compiles nothing and never overwrites configuration on
reinstall.

The game transport is UDP on the primary allocation and HTTP is TCP on the
same allocation. Wings publishes assigned allocations — open the matching
host or cloud firewall port for both protocols too. Bind the process to `0.0.0.0` inside its
container, not to the host's public allocation IP.

## Configuration and files

The egg starts:

```text
/opt/kcdc/KCDCServer --host 0.0.0.0 --port {{SERVER_PORT}} --config server.json
```

Pterodactyl supplies the placeholder through an environment variable. The
entrypoint resolves it once and replaces itself with the server, so stop
signals reach the server process. The egg's Stop setting is `^C`, which Wings
sends as SIGINT; an ordinary Docker stop sends SIGTERM. Both enter the native
shutdown path.

Edit `server.json` through the file manager or SFTP after the first start.
The egg's explicit bind address and port override the matching JSON
fields. Everything else stays in the file, including the server token and the
`mod` block — `mod.level` picks the level clients load, one of
`kutnohorsko`, `trosecko` or `klaster`, and an unknown value fails at boot
rather than at the first connect. Keep secrets in the JSON file rather than
in a publicly shared egg.

Upload gamemodes into `resources/`, beside the seeded `kcdc-gamemode`. Edit or
delete that one freely: it is only ever written when it is not there, so your
version survives a restart and an image upgrade. Logs, crash data and the
`.packages/` resource cache live beside them. Back up the whole server directory,
including hidden files. The runtime binaries live outside the writable
directory and are upgraded by changing the image tag.

The administrator controls the startup command.

## Updates

Stop the server, back up its files, import or update the egg from the
intended release, select that exact image tag for the existing server, and
start it again. Updating an egg does not change the image every existing
server has selected. No reinstall is needed to replace native binaries —
they are part of the image.

## Release packaging

The template, renderer, Dockerfile and release automation live in this
repository. The release pipeline generates the egg for the exact image tag,
embeds it in the image, tests it, pushes the image to GHCR and attaches the
JSON to the GitHub release. The template leaves `meta.update_url` unset.

Operators import the generated JSON into their own Panel. There is no
required central egg registry.

## Validation

The release job tests an ordinary Docker start and a Wings-style run with a
read-only root filesystem, a writable home volume, a numeric UID different
from the image default, dropped capabilities and a writable `/tmp`. It checks
HTTP readiness, the UDP listener, SIGINT and SIGTERM shutdown, and
package-key persistence across a restart before publishing. Unit tests check
the rendered egg's PTDL_v2 structure and its image and variable metadata.

Those checks do not cover an actual Panel and Wings installation. Test the
egg on your deployment before making the server available to players.

The implementation follows Pterodactyl's [custom image
guide](https://pterodactyl.io/community/config/eggs/creating_a_custom_image.html),
[custom egg
guide](https://pterodactyl.io/community/config/eggs/creating_a_custom_egg.html),
[Panel egg
importer](https://github.com/pterodactyl/panel/blob/develop/app/Services/Eggs/Sharing/EggImporterService.php)
and [Wings container
restrictions](https://github.com/pterodactyl/wings/blob/develop/environment/docker/container.go).
