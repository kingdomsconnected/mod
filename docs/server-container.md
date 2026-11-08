# Public server container

The release workflow publishes a Linux amd64 image to
`ghcr.io/kingdomsconnected/kcdc-server:<version>` (no leading `v`), after the
release itself has been published. A final release also moves `latest` to
the same image; a prerelease never does. There are no `stable` or `testing`
image tags, and MafiaHub Services channel promotion stays manual. Nothing here bumps versions.

The image contains the stripped native server, `crashpad_handler`, `libnode`,
the startup wrapper, the generated Pterodactyl egg and the default gamemode.
The default gamemode includes its TypeScript source. The image contains no
native project source or server configuration.

## One-time GHCR setup

The workflow uses its own `GITHUB_TOKEN` with job-scoped `packages: write`.
Allow GitHub Actions to create packages in the organisation. No registry PAT
or private checkout credential is ever passed to Docker.

GHCR creates new packages as private. After the first push, an
organisation/package administrator must open the **kcdc-server package
settings**, change **package visibility to Public**, and ensure **Manage
Actions access** grants the `kingdomsconnected/mod` repository write access.

The first run fails its final anonymous-access check until visibility is
public. After changing it, rerun the failed image job while its one-day Linux
artifact is still available. CI verifies an unauthenticated manifest request
after every push; an authenticated push succeeding does not prove the package
is public.

## Run

Copy `compose.yaml` to the host and create a `.env` beside it:

```dotenv
KCDC_VERSION=REPLACE_WITH_A_RELEASED_VERSION
```

Then:

```sh
docker compose pull
docker compose up -d
docker compose logs -f server
```

Or run it directly:

```sh
docker run -d --name kcdc --init --restart unless-stopped \
  -p 27015:27015/udp -p 27015:27015/tcp \
  -v kcdc-data:/home/container 'ghcr.io/kingdomsconnected/kcdc-server:<version>'
```

### Override startup arguments

Native server arguments go after the image name:

```sh
docker run -d --name kcdc --init --restart unless-stopped -p 28015:28015/udp -p 28015:28015/tcp -v kcdc-data:/home/container 'ghcr.io/kingdomsconnected/kcdc-server:<version>' --port 28015
```

For Compose, edit the service's `command` and its `ports` entries together:

```yaml
    command: ["--port", "28015"]
    ports:
      - "28015:28015/udp"
      - "28015:28015/tcp"
```

`command` passes arguments through the image's entrypoint, which prepares the
data directories and then executes the server.

| Argument   | Default                            | Purpose                                     |
| ---------- | ---------------------------------- | ------------------------------------------- |
| `--port`   | `27015`                            | Game session (UDP) and HTTP endpoints (TCP) |
| `--host`   | `0.0.0.0`                          | Bind address                                |
| `--config` | `server.json` in `/home/container` | Server configuration file                   |

`KCDC_VERSION` selects the release; the optional `KCDC_IMAGE` overrides the
registry and name, which defaults to `ghcr.io/kingdomsconnected/kcdc-server`.
Those two variables choose the image and nothing else — server settings come
from CLI arguments or `server.json`.

The game transport is UDP and the HTTP endpoints are TCP, both on the one
port number (Framework 40.2 and later). Docker `EXPOSE` documents the
defaults; Compose and `-p` are what publish them. Open the same port for
both protocols in the host or cloud firewall, and keep the advertised game
port equal to the published one, as Compose does, so clients reach the port
they are told about.

The server writes `/home/container/server.json` on first start with every key
this build understands, and honours it unless a CLI flag overrides it. KCDC's
own keys sit under `mod`; `level` selects the level clients load and must be
`kutnohorsko`, `trosecko` or `klaster`. See the
[server configuration](server-configuration.md) for the other keys. The session
holds 512 players — a larger `maxplayers` is clamped back down at boot, since
the cap is compiled in.

### Persistent data and resources

The server runs as UID/GID `10001:10001`. A named volume persists
configuration, logs, the resource package key and cache, crash data and
resources across upgrades; a bind-mounted directory must be writable by that
UID. The default gamemode is seeded into `/home/container/resources` the first
time it is not there, so a fresh volume answers commands; put your own gamemode
beside it, replace it, or mount a directory there:

```yaml
    volumes:
      - kcdc-data:/home/container
      - ./resources:/home/container/resources
```

The seeding happens once per resource, on startup, and only when the directory
is missing -- an edited or deleted `kcdc-gamemode` is never restored or
overwritten. Empty `resources/` of everything and players still connect, to a
session that answers no commands.

For Fly.io, `fly deploy -c deploy/fly.toml --image ghcr.io/kingdomsconnected/kcdc-server:<version>`
with the committed [`fly.toml`](../deploy/fly.toml), whose process command resolves
Fly's UDP bind address and passes it through `--host`. **UDP requires a
dedicated IPv4** (`fly ips allocate-v4`).

## Image contents and validation

The container build never compiles or clones source. A fresh context is
assembled from exactly `KCDCServer`, `crashpad_handler` and numerically
versioned `libnode.so*` files taken from the release artifact, plus the public
startup wrapper, the generated egg, license notices and the Docker packaging
files. The default gamemode is staged separately from the same release
artifact. Other resources, source maps, Git history, server configuration,
tokens, static libraries and debug files are excluded. A runtime symlink
cannot escape the input bundle, and a deny-by-default `.dockerignore` is the
second boundary.

The server must already be stripped — staging rejects DWARF and a private
symbol table, which is why the Linux build job strips its bundle and verifies
the result. Vendor binaries are stripped in the context. The tested server
bytes are never rewritten, so a final release that reuses a prerelease
artifact ships what testers ran; the Ubuntu runtime layers are rebuilt, so
this is binary promotion rather than whole-image digest promotion. Published
native binaries still carry their dynamic symbols and compiled strings.

Buildx uses an explicit local context with provenance, SBOM, Git metadata and
build-record uploads disabled, and no remote cache is exported. Before
pushing, CI checks non-root execution, library resolution, HTTP startup, the
UDP listener, custom ports and graceful shutdown, then pushes that exact
tested image.

To prepare an image locally on Linux with binutils installed, start from an
existing Linux release artifact:

```sh
python3 scripts/stage_server_image.py /path/to/server-linux /tmp/kcdc-image
docker buildx build --platform linux/amd64 --provenance=false --sbom=false \
  --load -t kcdc-server:local /tmp/kcdc-image
```

The destination must not already exist. Building from the checkout is
unsupported and fails without an explicitly staged runtime.
