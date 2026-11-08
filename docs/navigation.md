# Server-side navigation

The server can load KCD2's navigation mesh to query paths, reachability and
floor heights, and plan NPC movement. The mesh includes buildings, bridges
and stairs. It is optional game data that must be supplied separately.

## Setting up the mesh

Copy the navigation data from your own KCD2 installation into the server's
`files/` directory. This is the recommended location for a deployed server.
The server does not need the rest of the game installed.

1. Open the game's installation directory through Steam or GOG Galaxy.
2. Find `Data/Levels/<level>/recast.pak`. Use the level named by `mod.level`:
   `kutnohorsko`, `trosecko` or `klaster`.
3. Copy that file to `files/<level>/recast.pak` in the server bundle. Keep
   the filename `recast.pak`. For example:

```text
server/
  KCDCServer.exe
  server.json
  files/
    kutnohorsko/
      recast.pak
```

Run the server from the bundle directory. It searches `files/` recursively,
so no `mod.navmesh` setting is needed. `files/recast.pak` also works for a
server using one level. When keeping several levels, use their level
subdirectories so the server can choose the right copy. Restart the server
after replacing a pak, including after a game update.

### Using a game installation during development

Set `mod.navmesh` if you want to read directly from a game installation or
keep the file elsewhere. This location is checked before the `files/` tree:

```json
"mod": { "level": "kutnohorsko", "navmesh": "C:/Games/KingdomComeDeliverance2" }
```

For a native Linux server, a Steam installation might look like this:

```json
"mod": {
  "level": "kutnohorsko",
  "navmesh": "/home/user/.local/share/Steam/steamapps/common/KingdomComeDeliverance2"
}
```

A Windows server running under Wine uses the Windows path to that directory.
For example:

```text
Z:/home/user/.local/share/Steam/steamapps/common/KingdomComeDeliverance2
```

These are explicit configuration examples; the server does not search game
installation paths automatically.

The configured value accepts a pak file, a directory containing `recast.pak`,
or one containing `<level>/recast.pak`, `Levels/<level>/recast.pak` or
`Data/Levels/<level>/recast.pak`. Relative paths start at the server's working
directory.

### Search order and missing data

The server tries the configured location first. It then searches `files/`
for `recast.pak` copies in three groups, each in path order:

1. copies under a directory named for the level, such as
   `files/kutnohorsko/recast.pak`;
2. copies nested under directories that name no level;
3. a bare `files/recast.pak`, last.

Names are matched without regard to case. Only a directory named exactly
after a level counts as that level's: `kutnohorsko_old` names no level and
falls in the second group. Copies under another level's directory are
skipped. An unreadable or invalid candidate is logged, and the server tries
the next one.

If no usable copy is found, startup continues and `Navigation.ready` is
false. Geometry queries return `null` or `false`, and NPC moves fall back as
described under [Choosing the NPC pathfinder](#choosing-the-npc-pathfinder).
The startup log names the loaded file, or warns that no mesh was found and
gives the absolute path it searched and where to put the pak. The game data
must be supplied separately; it is not part of the server bundle.

| Level          | Tiles    | Polygons    | Build    | Memory    |
| -------------- | -------- | ----------- | -------- | --------- |
| klaster        | 264      | 12 k        | 0.01 s   | ~5 MB     |
| trosecko       | 26 009   | 1.04 M      | 0.25 s   | ~160 MB   |
| kutnohorsko    | 54 620   | 1.50 M      | 0.4 s    | ~250 MB   |

Build is the mesh alone, measured natively; the Windows server under wine
reports about 1.2 s for kutnohorsko including reading the pak.

## Scripting

`Navigation` is a server global. Points are world-space metres, Z up. A point
argument is a `Vector3` or a `Partial<Vector3>`; omitted components default to
zero. Without a mesh, geometry queries return `null` or `false`, overlay lists
are empty, and overlay changes return `false`.

```ts
if (Navigation.ready) {
    const spawn = Navigation.closestPoint(guessed) ?? guessed;
    const path = Navigation.findPath(guard.position, gate, { doors: "unlocked" });
    if (path && path.complete) { /* path.points, path.length */ }
}
```

| Member                                         | Answers                                                                              |
| ---------------------------------------------- | ------------------------------------------------------------------------------------ |
| `ready`, `overlays`, `activeOverlays`          | Whether a mesh loaded; the level's overlays, sorted; the enabled ones, oldest first. |
| `closestPoint(point, opts?)`                   | The nearest walkable point, or null.                                                 |
| `floorAt(point, opts?)`                        | The floor height there, bridges and upper storeys included, or null.                 |
| `findPath(from, to, opts?)`                    | `{ points, complete, length }`, or null when an end is off the mesh.                 |
| `canReach(from, to, opts?)`                    | Whether a complete path exists.                                                      |
| `raycast(from, to, opts?)`                     | `{ hit, point }`: how far a straight walk gets.                                      |
| `randomPoint(center, radius, opts?)`           | A reachable point within `radius` of `center`, or null.                              |
| `randomPoint()`                                | A point anywhere on the level.                                                       |
| `enableOverlay(name)` / `disableOverlay(name)` | Enable or disable a named overlay.                                                   |

### Query options

Every query takes an optional options object:

```ts
{ searchRadius?: number; searchHeight?: number; doors?: "unlocked" | "open" | "any" | "none" }
```

`searchRadius` (default 2 m, at most 64) and `searchHeight` (default 4 m, at
most 128) limit how far a point may be from the mesh, horizontally and
vertically. Keep `searchHeight` under a storey's height, or a point on one
floor can snap to the floor above.

`doors` chooses which doors a query walks through:

| Policy               | Crosses                                        |
| -------------------- | ---------------------------------------------- |
| `unlocked` (default) | Every door that is not locked, open or closed. |
| `open`               | Only doors standing open.                      |
| `any`                | Every door, locked or not.                     |
| `none`               | No doorway at all.                             |

A door's state is read from the door in the global virtual world. Links that
are not doors are crossed under every policy.

An options value that is not an object, or a field that is present with the
wrong type or out of range, throws. So does a point that is not an object or
has a non-finite component.

### Random points

`randomPoint(center, radius, opts?)` draws a point reachable from `center` and
no further from it across the ground than `radius` (at most 512 m). Detour
draws from every polygon the circle touches, so a draw outside the circle is
thrown away and drawn again, a bounded number of times; the call returns null
when no draw lands inside. It also returns null when `center` is off the mesh.

Without a centre, the point can be anywhere on the level: each tile is chosen
with equal probability, then a point on it weighted by area. To pass options
without a centre, leave the first two arguments undefined:

```ts
const anywhere = Navigation.randomPoint(undefined, undefined, { doors: "open" });
```

### Overlays

An overlay is replacement navigation data the game ships for a quest state.
Enabling one replaces every layer of the tile columns it covers; where enabled
overlays overlap, the most recently enabled wins. Disabling one gives its
columns back to the overlay enabled before it, or to the base mesh.

Only each overlay's index is read at startup. Its tiles are read from the pak
and checked when it is enabled, so keep the pak in place while the server
runs. Of the base mesh, only the columns some overlay covers are kept in
memory, to be restored when the overlays over them are disabled.

`enableOverlay` returns false for a name the level does not ship, one already
enabled, or one whose tiles cannot be read or loaded; the mesh is then left as
it was, and the server log says why. `disableOverlay` returns false for a name
that is not enabled, or when a column could not be restored; the overlay is
disabled either way, and the log names the failure.

Overlays are chosen by name only. Nothing maps quest state to overlays
automatically; a gamemode enables the ones it wants.

### Cost

A path within a village costs a few tenths of a millisecond; one across a
whole level a few milliseconds. Plan when something changes, not every tick.

## Choosing the NPC pathfinder

The game's own movement steers a body straight at the point it is given and
finds no way around anything. Routes come from the server's mesh.

`npc.moveTo` and `npc.patrol` accept `pathfinding` in their options:

| Mode             | Streamed NPC (client simulator)                                                                               | Dormant NPC                                                               |
| ---------------- | ------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------- |
| `auto` (default) | With a mesh, the server routes. Without one, the game steers straight at the goal.                            | With a mesh, the server walks the route. Without one, a straight line.    |
| `game`           | The game steers straight at the goal. If it reports `blocked` and a mesh is loaded, the server tries a route. | The order waits for a client simulator. An active server route continues. |
| `server`         | The server routes.                                                                                            | The server walks the route.                                               |

```ts
if (Navigation.ready) {
    npc.moveTo(gate, { pathfinding: "server", speed: "walk" });
    npc.patrol([gate, market], { pathfinding: "server", loop: true });
}
```

The mode applies to the whole move or patrol and is replaced by a new order.
`server` throws if `Navigation.ready` is false, leaving the previous order
intact. An invalid mode also throws.

A server route is handed to the simulating client one corner at a time. Each
corner carries how much route lies beyond it, so the game does not slow the
body down as if the corner were the end of the walk, and the body keeps
walking through a corner it reaches before the next one arrives. A gentle bend
is handed over before the body gets to it; a sharp bend or a doorway is handed
over as the body arrives, so it does not cut into what the route bends around.
The client moves and animates the body using `npc.locomotion`.

A move arrives within its radius measured across the ground, and within 2 m
in height, so a body standing under a balcony has not reached a point on it. A dormant NPC is walked along the same route by the
server. Server routes only pass doors that stand open, as a query with
`doors: "open"` does.

Route plans are queued and run a few per tick, oldest first, each with a
capped search. An order waiting for its plan reads as `running`. A search that
runs out of budget, or a goal the mesh cannot reach, gives a partial route,
and a missing or partial route ends the move with `blocked`. A blocked server
route is not retried.

`follow` and `flee` have no `pathfinding` option: the game steers them
straight at, or straight away from, their target. When game movement reports
`blocked` on a `moveTo`, a patrol leg, a `follow` or a `flee`, a loaded mesh
lets the server try a route before the outcome is reported. Without a mesh,
the blocked result is reported as is. A follow that recovers this way routes
to where its target stood, then resumes following the moving target; scripts
still see `npc.intent` as `follow`. Flee recovery routes to a point past the
requested distance, on the ground at the NPC's own height. A new order cancels
any recovery route.

### Status and events

`npc.status` reads `running` from the moment a move is given -- while its
route is planned and while it walks the corners -- until the server accepts an
outcome. It then reads that outcome (`reached`, `blocked` or `failed`) until
the next order.

`npcIntentDone` fires once per order, when the server accepts its outcome.
Intermediate corners do not raise it, and a change of simulator never
announces a finished order again. A follow never finishes: it announces
`reached` when it catches up, at most once per intent revision -- again only
once its intent has been sent out anew, as on a change of simulator.

A patrol raises `npcIntentDone` once per leg and steps on that event. After a
leg that ends `blocked` or `failed`, it waits at least 2 seconds before the
next leg. When every leg of a lap has failed in a row, the patrol ends and its
last leg reports `failed`.

`patrol` accepts 1 to 256 waypoints, each with finite coordinates within the
world's limits, and returns false otherwise. `waitSeconds` must be a finite
number and is clamped to 0..3600.

A frozen NPC does not advance while dormant.

This feature changes NPC replication. Build and update the client and server
together.

## Why Detour is vendored

`vendors/recastnavigation` holds Detour 1.6.0, runtime only, built with
`DT_POLYREF64` and `DT_VIRTUAL_QUERYFILTER`. kutnohorsko needs the first:
54 620 tiles and up to 317 polygons in one leave a 32-bit reference 7 bits of
salt, where `dtNavMesh::init` demands 10. The second lets the query filter
consult door state on an off-mesh link, which a flag mask cannot express.

The vcpkg port is not used because its build options are fixed by
`vcpkg.json` and the triplets, which belong to the Framework repository
rather than to this project. Turning on upstream's
`RECASTNAVIGATION_DT_POLYREF64` there, even scoped to the port in a triplet,
would change Detour for every project built from it.

With 64-bit references a `dtLink` wants 8-byte alignment that Detour's
4-byte section layout does not give; the codec pads the vertex list so the
link section lands aligned.

## Limitations

- Overlays are chosen by name; nothing maps quest state to them
  automatically.
- Door state is read from the global virtual world only.
- Links that name an entity only through a secondary id, and kind 3 links,
  are not interpreted: every query crosses them.
- The doorway given to a door the mesh does not mark follows its leaf as
  measured from the linked doors. A door that is wider, or whose leaf does
  not follow its model's name, is gated over the wrong polygons.
- Mesh data stays on the server. Clients receive movement targets, never the
  mesh.
