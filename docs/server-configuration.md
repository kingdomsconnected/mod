# Server configuration

Use an official server release on Windows or Linux. The server listens on
port 27015 by default, using UDP for game traffic and TCP for HTTP endpoints.
Open the selected port for both protocols.

The server creates `server.json` beside the executable on its first start.
Settings such as `host`, `port`, `maxplayers`, `server-token` and `password`
sit at the top level. KCDC settings sit under `mod`.

Set `server-token` to list the server in the browser. Set `password` to
require a password when joining; an empty value allows anyone to join.
These settings also accept the `--server-token` and `--password` command-line
flags. The server supports up to 512 players and clamps higher `maxplayers`
values to that limit.

```json
{
  "host": "0.0.0.0",
  "port": 27015,
  "maxplayers": 512,
  "password": "",
  "mod": {
    "name": "",
    "level": "kutnohorsko",
    "navmesh": "",
    "required_dlc": [],
    "scoreboard": true,
    "reveal_map": true,
    "world_resources": []
  }
}
```

| Setting           | Default         | Description                                                                |
| ----------------- | --------------- | -------------------------------------------------------------------------- |
| `name`            | `""`            | Server name shown in Discord presence.                                     |
| `level`           | `"kutnohorsko"` | Level to load: `kutnohorsko`, `trosecko` or `klaster`.                     |
| `navmesh`         | `""`            | File or directory checked before `files/` for the level's navigation mesh. |
| `required_dlc`    | `[]`            | DLCs clients must own, using names such as `ForgeTycoon`.                  |
| `scoreboard`      | `true`          | Whether Tab shows the player list.                                         |
| `reveal_map`      | `true`          | Whether to reveal locations and points of interest on the map.             |
| `world_resources` | `[]`            | World Builder exports loaded at startup, as paths or `{name, path}`.       |

Use `mod.level` to choose the level. The Framework's top-level `map` setting
does not select a KCDC level. The DLC requirement is checked by clients;
the server does not enforce it.

For NPC pathfinding, copy `Data/Levels/<level>/recast.pak` from your game
installation into the server's `files/<level>/` directory, or set
`mod.navmesh` to an existing location. The server can run without a mesh.
See [Server-side navigation](navigation.md) for setup and pathfinding modes.

See the [World Builder guide](map-editor.md) for exports and player access,
the [server container guide](server-container.md) for Docker, Compose and
Fly.io, or the [Pterodactyl guide](pterodactyl.md) for Panel and Wings.
