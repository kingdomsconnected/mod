# World objects: doors, smart objects and items

Server scripts can furnish the world with working objects, and take the
level's own out of it:

- hang a door where the level has none, with its own lock;
- take one of the level's doors out, and put it back;
- place a bench, a bed, a dice table or a crafting station that players use
  the way they use the level's own;
- take one of the level's stations out, and put it back;
- lay named items on the ground;
- ship all of these in a World Builder world resource.

Everything on this page runs in a **server** script. The objects are kept in
memory only: nothing is saved, so a script that wants them back after a
restart creates them again when it starts. A world resource does that for
you on every start.

To swap a level object for another mesh, such as a tree for a stump, use
`LevelEdit.apply` with `model`; see
[The level's own](map-editor.md#the-levels-own) and the felled-tree example in
[Trace results](prop-placer.md#trace-results). To make the tree fall and count
the axe blows that fell it, see
[Rigid props and weapon hits](prop-physics.md).

## Before you start

**Virtual worlds.** Every `virtualWorld` argument on this page is optional.
Unless a function says otherwise, leaving it out means the global world,
`0xFFFFFFFF` (4294967295). Players join in the global world. `0` is a
numbered world like any other, not the global one.

- An object in the global world reaches every player.
- An object in a numbered world reaches players in that world, and players
  who are still in the global world, because a player in the global world
  sees every world.
- The server lets a player use what they can see: what is in their own
  world and in the global world, and, for a player in the global world, a
  door hung or a station placed in any world. The level's own doors are the
  exception: each world has its own copy, and a player works their own
  world's. Move players into a numbered world with
  `player.setVirtualWorld(world)` before relying on per-world objects.

**GUIDs.** Doors, stations and seats are named by a GUID written as sixteen
lowercase hex digits, such as `0008eb7ef7bb1032`. A level object's GUID is
the same on every machine and in every server run, so it is safe to store.
Find a level door's GUID with `door.guid`, `Door.queryNearest(player)`, or
the `levelObject.guid` of a world trace hit
([Trace results](prop-placer.md#trace-results)).

A door or smart object a script creates gets a GUID from a counter
(`f5a9...` for doors, `f5aa...` for smart object parts). It changes with
every server run and with the order things are created, so do not store
it. Give the object a `name` and look it up by that instead.

**Ownership.** An object a script creates belongs to the resource that
created it, and is destroyed when that resource stops. A removal made by a
script is a level edit, which also belongs to that resource: the object
comes back when the resource stops. Objects and removals in a world
resource belong to the server and last until it shuts down.

## Doors

### Listing doors

- `Door.all()` returns a `Door` for every door: the level's own, all of
  which exist in the global world from the start, and every door a script or
  world resource hung. In a numbered world, a level door appears once
  something there has looked it up. Doors taken out of a world are not
  listed for it.
- `Door.find(guid, virtualWorld?)` returns the door with that GUID, or
  `null`. A level door is found in any world it has not been taken out of.
  A hung door is found only in the world it hangs in.
- `door.spawned` is `true` for a door a script or world resource hung, and
  `false` for a level door.
- `door.model` is the leaf mesh path, such as
  `objects/characters/assets/doors/lvl1_door_a_left.cdf`.

The existing door API (`door.open`, `door.setOpen`, `door.locked`,
`door.lockedByServer`, the `doorInteract` event and the rest) works the same
on hung doors and level doors. A hung door belongs to no level area, so its
`door.areas` is empty. Its `door.name` is the name it was hung with, or
`spawned_door_<n>` when it was given none.

### Refusing a player's use

A player's own client moves a door the moment the player uses it, and tells
the server afterwards. So where a refusal is decided changes what the player
sees:

- **`door.usable = false`**, on the server. Every client that has the door
  streamed in takes away its open, close, unlock and lockpick prompts and
  refuses the use on the player's machine, so nothing moves. The server
  refuses a report against it too. It is per door, not per player, and it
  does not stop NPCs, `door.setOpen` or `door.locked`.
- **The client's `doorInteract`** `(door, action, keySide)`, raised on the
  player's machine before anything moves. Return `false` and the use does
  nothing. `door` describes the door as that machine sees it: `id`, `guid`,
  `name`, `open` and `locked`. This is where a per-player rule goes.
- **The server's `doorInteract`** `(player, door, action, keySide)`, raised
  when the client's report arrives. Return `false` and the door swings back
  on the player's machine about a round trip after it started moving; nobody
  else sees anything. Use it for what only the server knows, and to hold a
  rule against a modified client.

A key turned in the same use as the push reaches both events as `unlock`
and then `open`.

### Door models

`Door.models()` lists every leaf mesh the shipped levels hang doors from,
sorted by path. These are the only models `Door.spawn` accepts. Each row
has:

| Field        | Meaning                                                                           |
| ------------ | --------------------------------------------------------------------------------- |
| `path`       | The mesh path, `objects/characters/assets/doors/...cdf`.                          |
| `name`       | The file stem, such as `lvl1_door_a_left`.                                        |
| `hinge`      | `left` or `right`: the side the leaf is hinged on. The hinge is part of the mesh. |
| `placements` | How many doors the shipped levels hang from this mesh.                            |

`Door.spawn` takes either the path or the stem. The path is matched without
regard to case; the stem must match exactly.

### Hanging a door

`Door.spawn(definition, virtualWorld?)` hangs a working door where the
level has none and returns its `Door`. Every player in that virtual world
sees it, opens and closes it, and can unlock it with a key or pick its lock.
The server judges every use by the same rules as the level's own doors. The
door starts closed.

The definition is one `DoorDefinition`, the same object a world resource's
`doors` array stores:

| Field            | Type                                           | Default        | Meaning                                                                                                                             |
| ---------------- | ---------------------------------------------- | -------------- | ----------------------------------------------------------------------------------------------------------------------------------- |
| `model`          | string                                         | required       | A model from `Door.models()`, by path or stem.                                                                                      |
| `position`       | `Vector3` or `[x, y, z]`                       | required       | Where the leaf's pivot goes, on its hinge side, as the level's own doors stand. Each coordinate must be within 16384 of the origin. |
| `rotation`       | `Quaternion`, `{x, y, z, w}` or `[x, y, z, w]` | no rotation    | Which way the door faces.                                                                                                           |
| `name`           | string                                         | none           | Up to 64 bytes of UTF-8. `door.name` reads it back.                                                                                 |
| `locked`         | boolean                                        | `false`        | Whether it is hung locked.                                                                                                          |
| `lock`           | `'none'`, `'bolt'`, `'boltAndKey'`, `'key'`    | `'boltAndKey'` | What each side needs to get through while it is locked. See [Locks](#locks).                                                        |
| `lockpickable`   | boolean                                        | `true`         | Whether its keyhole can be picked.                                                                                                  |
| `lockDifficulty` | number, 0 to 1                                 | the game's own | How hard the lockpicking minigame is, in fifths from very easy to very hard.                                                        |
| `keyItem`        | string                                         | none           | The item class GUID of a key that opens it, in the form `player.giveItem` takes.                                                    |

Note that `new Quaternion(w, x, y, z)` takes `w` first, while the array form
is `[x, y, z, w]`.

`Door.spawn` throws an `Error` when:

- `definition` is not an object, or `position` is missing;
- `model` is not in `Door.models()`;
- `position` or `rotation` is not finite, or `rotation` is not a quaternion;
- `name` is longer than 64 bytes;
- `lock` is not one of the four names;
- `lockpickable` is not a boolean, or `lockDifficulty` is outside 0 to 1;
- `keyItem` is not an item class this build knows.

The example hangs a locked cell door. Look the door up again by its name,
because its GUID changes between runs.

```ts
// A rotation around the vertical (Z) axis, as [x, y, z, w].
function yaw(degrees: number): number[] {
    const half = (degrees * Math.PI) / 360;
    return [0, 0, Math.sin(half), Math.cos(half)];
}

const cell = Door.spawn({
    model: "lvl3_door_prison_left",
    position: [3101.2, 815.4, 57.3],
    rotation: yaw(90),
    name: "jail-cell-1",
    locked: true,
    lock: "key",
    lockpickable: true,
    lockDifficulty: 0.8,
    keyItem: jailKeyClassGuid, // an item class GUID your resource hands out
});

console.log(cell.spawned, cell.model); // true "objects/characters/assets/doors/lvl3_door_prison_left.cdf"

// Later, from anywhere in the script:
const found = Door.all().find((door) => door.spawned && door.name === "jail-cell-1");
```

To keep a hung door between runs, store `door.toJSON()` in your own save
data and pass it to `Door.spawn` when the server starts. It returns the
definition the door was hung with, with `locked` set to whether it is
locked now. It returns `null` for a level door.

`door.destroy()` takes a hung door down for good. Destroying it also drops
it from its world resource's `doors`.

A hung door is only the leaf. It has no frame or wall, so hang it in a
doorway or build one around it with props.

### Locks

`lock` says what each side of the door needs to get through it while it is
locked:

| `lock`       | Front      | Back                                                |
| ------------ | ---------- | --------------------------------------------------- |
| `none`       | Nothing.   | Nothing.                                            |
| `bolt`       | A bolt.    | Nothing.                                            |
| `boltAndKey` | A bolt.    | A keyhole. Most of the level's doors are like this. |
| `key`        | A keyhole. | A keyhole.                                          |

A locked `none` door opens only from a script, with `door.setOpen`.

The door's forward axis is its local +Y axis after `rotation`. The front is
the side that axis points away from, so the keyhole of a `boltAndKey` door
is on the side +Y points to.

A key in `keyItem` or a lockpick opens a door hung with `locked: true`, as
on the level's own doors. Setting `door.locked = true` is the server's lock,
which is separate: a key or a player's own unlock does not open it, and a
lockpick still does. A hung door does not open to the generated home or
shop key, and no home area locks it at night.

### Removing a level door

`Door.remove(guid, virtualWorld?)` takes one of the level's doors out of a
virtual world, collision included. Leaving out `virtualWorld` takes it out
of the global world, which takes it out of every world. `door.destroy()` on
a level door does the same in that handle's virtual world.

While a door is out of a world:

- every player in that world stops seeing it, and players still in the
  global world stop seeing it too;
- `Door.all()` does not list it there, and `Door.find` returns `null`;
- the server refuses anyone working it there;
- `Door.queryNearest` does not return it as found;
- server-spawned NPCs and navigation queries in that world treat the
  doorway as open.

`Door.remove` throws when `guid` is not a hex GUID string or is not a door
this level places. For a hung door it throws and tells you to use
`destroy()`. Removing a door that is already out of that world, or out of
the global world, does nothing more. Removing a door that has another level
edit in that world, such as a move or a model swap, replaces that edit.

Removal goes through a level edit, so it also shows in `LevelEdit.all()`,
and `LevelEdit.restoreAll()` puts the door back with every other edit. A
level edit that swaps the door's model also counts as taking it out.

```ts
// Take the door a player is standing at out of the player's world.
// For a player in the global world, that is every world.
const answer = await Door.queryNearest(player);
if (answer.found && answer.door && !answer.door.spawned) {
    answer.door.destroy();
}
```

### Listing and restoring removed doors

`Door.removed(virtualWorld?)` lists the level doors that are out, whether a
script, a world resource or a raw level edit took them out. Leaving out
`virtualWorld` lists every world. Passing a world lists only removals made
in that world, not removals from the global world. Each row has:

| Field          | Meaning                                                                   |
| -------------- | ------------------------------------------------------------------------- |
| `guid`         | The door's GUID, as `Door.restore` takes it.                              |
| `name`         | The entity name the level gives it.                                       |
| `position`     | Where the level hangs it, as a `Vector3`.                                 |
| `virtualWorld` | The world it is out of. Out of the global world means out of every world. |

`Door.restore(guid, virtualWorld?)` puts a level door back in that world
and returns its `Door`. It comes back closed, and locked if the level builds
it locked. It returns `null` when that world has no removal for the door. A
door taken out of the global world can only be restored in the global world.

```ts
for (const row of Door.removed()) {
    Door.restore(row.guid, row.virtualWorld);
}
```

## Smart objects

A smart object is one of the game's interactive prefabs: the solid
furniture together with the entities that the game's sitting, sleeping,
dice and crafting run on. Each client builds it under the same GUIDs, so players
see each other sit, sleep, gamble and craft at it as they do at the level's
own.

### Blueprints

`SmartObject.blueprints()` lists everything `SmartObject.spawn` can place,
sorted by name. Each row has `name`, `kind`, `seats` (how many bodies can
sit or lie on it at once) and `bare` (whether it can be placed without its
furniture; see [Bare placements](#bare-placements)). The list is compiled
into the build:

| Blueprint                                     | Kind       | Seats | Bare |
| --------------------------------------------- | ---------- | ----- | ---- |
| `Bed/bed_bench`                               | `bed`      | 1     | yes  |
| `Bed/bed_high`                                | `bed`      | 1     | yes  |
| `Bed/bed_high_double`                         | `bed`      | 2     | yes  |
| `Bed/bed_low`                                 | `bed`      | 1     | yes  |
| `Bed/bed_low_ver1`                            | `bed`      | 1     | yes  |
| `Bed/bed_low_ver2`                            | `bed`      | 1     | yes  |
| `Bench/bench_1place_low`                      | `seat`     | 1     | yes  |
| `Bench/bench_1place_low_table`                | `seat`     | 1     | yes  |
| `Bench/bench_2places_high`                    | `seat`     | 2     | yes  |
| `Bench/bench_2places_low`                     | `seat`     | 2     | yes  |
| `Bench/bench_2places_low_table`               | `seat`     | 2     | yes  |
| `Bench/bench_3places_low`                     | `seat`     | 3     | yes  |
| `Chair/chair_high`                            | `seat`     | 1     | no   |
| `Chair/chair_low`                             | `seat`     | 1     | no   |
| `profession/apothecary/alchemyTable_wip`      | `alchemy`  | 0     | yes  |
| `profession/blacksmith/smithy_workshop`       | `smithing` | 0     | yes  |
| `profession/blacksmith/smithy_workshop_small` | `smithing` | 0     | yes  |
| `profession/butcher/butcher_dryer`            | `drying`   | 0     | yes  |
| `profession/butcher/butcher_smokeHouse`       | `smoking`  | 0     | no   |
| `Table/table_dice`                            | `dice`     | 2     | yes  |
| `Tent/tent_1`                                 | `bed`      | 1     | yes  |

The name is the game's prefab path under `Prefabs/`. `SmartObject.spawn`
matches it without regard to case.

### Placing a smart object

`SmartObject.spawn(definition, virtualWorld?)` places a blueprint and
returns its `SmartObject`. The definition is one `SmartObjectDefinition`,
the same object a world resource's `smartObjects` array stores:

| Field       | Type                                           | Default     | Meaning                                                      |
| ----------- | ---------------------------------------------- | ----------- | ------------------------------------------------------------ |
| `blueprint` | string                                         | required    | A name from `SmartObject.blueprints()`.                      |
| `position`  | `Vector3` or `[x, y, z]`                       | required    | Where the prefab's origin goes, as the level's copies stand. |
| `rotation`  | `Quaternion`, `{x, y, z, w}` or `[x, y, z, w]` | no rotation | Which way it faces.                                          |
| `name`      | string                                         | none        | Up to 64 bytes of UTF-8. `smartObject.name` reads it back.   |
| `bare`      | boolean                                        | `false`     | Place it without its furniture. `smartObject.bare` reads it. |

It throws an `Error` when `definition` is not an object, the blueprint is
unknown, `position` is missing or not finite, `rotation` is not a finite
quaternion, `name` is longer than 64 bytes, `bare` is not a boolean, or
`bare` is `true` for a blueprint that cannot stand bare.

```ts
const dice = SmartObject.spawn({ blueprint: "Table/table_dice", position: [3102.0, 820.5, 57.3], rotation: yaw(0), name: "tavern-dice" });
const bench = SmartObject.spawn({ blueprint: "Bench/bench_2places_low", position: [3104.5, 820.5, 57.3], rotation: yaw(90), name: "tavern-bench" });
const alchemy = SmartObject.spawn({ blueprint: "profession/apothecary/alchemyTable_wip", position: [3108.0, 818.0, 57.3], rotation: yaw(180), name: "tavern-alchemy" });

console.log(dice.kind, dice.stationId, dice.seats);  // "dice" "f5aa..." ["f5aa...", "f5aa..."]
console.log(bench.kind, bench.stationId);            // "seat" ""
```

`yaw` is the helper from the door example.

A `SmartObject` has:

| Member      | Meaning                                                                                                                    |
| ----------- | -------------------------------------------------------------------------------------------------------------------------- |
| `blueprint` | The blueprint it was placed from.                                                                                          |
| `kind`      | `seat`, `bed`, `dice`, `alchemy`, `smithing`, `drying` or `smoking`.                                                       |
| `name`      | The name it was placed with; empty for none.                                                                               |
| `bare`      | `true` when it was placed without its furniture.                                                                           |
| `stationId` | The GUID of its station part, as `Dice.tables()` and `SmartObject.stations()` list it. Empty for a seat or a bed.          |
| `seats`     | The GUIDs of the places a body sits or lies on. A seated player's stance and `Dice.tables()`'s `seats` use the same GUIDs. |
| `destroy()` | Takes it down for every player. Anyone sitting on it or working at it is let go.                                           |
| `toJSON()`  | The definition to place it again, or `null` once it is gone.                                                               |

`SmartObject.all(virtualWorld?)` lists placed smart objects. Leaving out
`virtualWorld` lists every world. Passing a world lists only objects placed
in exactly that world. `SmartObject.getById(id)` looks one up by its network
entity ID. To find one by name, filter `SmartObject.all()`.

A smart object never moves. Assigning its `position` or `rotation`, or
calling `setVirtualWorld`, is put back on the next server tick, with a
warning in the server log. To move one, destroy it and place it again.

### Bare placements

A blueprint brings its own furniture: `Bench/bench_2places_low` is always
the same shabby bench. With `bare: true` the server places only what the
interaction runs on -- the places a body sits or lies, the station, and the
triggers that offer the prompt -- and clients build no furniture for it. Put
it where a prop of your own stands and players sit on that prop:

```ts
// A log bench from the mesh catalog, and two places to sit on it.
Prop.spawn("objects/manmade/common_furniture/benches/low/bench_log_a.cgf", { x: 3104.5, y: 820.5, z: 57.3 });
const seat = SmartObject.spawn({ blueprint: "Bench/bench_2places_low", position: [3104.5, 820.5, 57.3], bare: true, name: "log-bench" });

console.log(seat.bare, seat.seats.length);  // true 2
```

What to know:

- **The pose is the blueprint's.** The seat height, the spacing between two
  places and the side a body gets up on are the game's, tuned to the
  blueprint's own furniture. Choose the blueprint closest to your prop -- a
  low bench for a low wall, `Bed/bed_low` for a pallet -- and line the two up
  by eye. In the World Builder the pose anchors show where each body goes
  ([Pose anchors](map-editor.md#pose-anchors)).
- **Nothing ties the two together.** The prop and the bare smart object are
  two objects. Moving or destroying one leaves the other.
- **Nothing is solid or visible.** A bare smart object has no collision and
  draws nothing. Without a prop under it, a player sits on thin air.
- **Not every blueprint.** `Chair/chair_high` and `Chair/chair_low` seat a
  body on a chair entity the game moves with the sitter, and the smokehouse
  has no separate furniture. They refuse `bare: true`; the `bare` column
  above, and `SmartObject.blueprints()`, say which accept it. Where a
  blueprint mixes both, as the smithy does, the static meshes go and the
  parts that are entities of the prefab -- the forge, the bellows -- stay.
- **Stations too.** A bare dice table, alchemy table or drying rack is
  listed and judged like any placed station. Its dice, book and tools stand
  where the blueprint's own table would hold them, so the prop has to offer
  a top at that height.

`toJSON()` writes `bare` only when it is `true`, so a definition saved
before this option existed reads back unchanged.

### Stations

A station is the part of a smart object players work at: a dice table, an
alchemy table, a smithy, a drying rack or a smokehouse. Seats and beds have
none.

`SmartObject.stations(virtualWorld?)` lists every station players in one
virtual world can use: the level's own, minus any taken out of that world
or the global world, and every station placed in that world or the global
world. Leaving out `virtualWorld` means the global world. Each row has:

| Field         | Meaning                                                                 |
| ------------- | ----------------------------------------------------------------------- |
| `id`          | Its GUID. For a level station, this is what `SmartObject.remove` takes. |
| `kind`        | `dice`, `alchemy`, `smithing`, `drying` or `smoking`.                   |
| `position`    | Where it stands, as a `Vector3`.                                        |
| `placed`      | `true` for a station a script or world resource placed.                 |
| `smartObject` | The placed `SmartObject` it belongs to, or `null` for a level station.  |

`Dice.tables(virtualWorld?)` and `Crafting.stations(kind?, virtualWorld?)`
follow the same rule, include placed stations, and mark them with `placed`.
Leaving out `virtualWorld` means the global world. To pass a world to
`Crafting.stations` without a kind, write `Crafting.stations(undefined, 5)`.
A placed crafting station has an empty `layer`.

The listings write a station's `id` in two forms. `SmartObject.stations()`
and `Dice.tables()` use the GUID alone, such as `0148ef920dcb72aa`.
`Crafting.stations()` puts the level in front of it, as
`kutnohorsko:0148ef920dcb72aa`, and crafting sessions, crafting events and
`Crafting.occupancy` use that form too. `SmartObject.remove` and
`SmartObject.restore` take only the GUID, the part after the colon.

The server treats a placed station like one of the level's own: players can
start alchemy and smithing there, play dice at a placed table, and dry or
smoke food at a placed rack or smokehouse. Destroying a placed station ends
any alchemy session at it.

A level station taken out of a player's world refuses them. A new alchemy or
smithing session cannot start there, and drying or smoking there fails with
`notAtStation`. Other players no longer see someone seated at a removed dice
table or working at a removed crafting station. A crafting session already
running when the station is taken out is not ended; end it with
`Crafting.cancel(player)` if you need to.

### Removing a level station

`SmartObject.remove(id, virtualWorld?)` takes one of the level's stations out
of a virtual world. Leaving out `virtualWorld` takes it out of the global
world, and so of every world. The station is hidden on every client there,
listed nowhere, and refused by the server, as above.

Only the station entity goes. The furniture around it is the level's
scenery and stays. To take that out too, use `LevelEdit.apply` or the World
Builder ([The level's own](map-editor.md#the-levels-own)).

`SmartObject.remove` throws when `id` is not a hex GUID string or is not a
station this level places. Placed smart objects are taken down with
`destroy()` instead.

```ts
// Take every level alchemy table out of world 5.
for (const station of SmartObject.stations(5)) {
    if (station.kind === "alchemy" && !station.placed) {
        SmartObject.remove(station.id, 5);
    }
}
```

`SmartObject.removed(virtualWorld?)` lists the level stations that are out,
by any route. Leaving out `virtualWorld` lists every world; passing a world
lists only removals made in that world. Each row has `id`, `kind`,
`position` and `virtualWorld`.

`SmartObject.restore(id, virtualWorld?)` puts a level station back in that
world and returns `true`, or `false` when that world has no removal for it.
Leaving out `virtualWorld` means the global world. A station taken out of the
global world can only be restored in the global world.

Like door removals, station removals are level edits. They show in
`LevelEdit.all()` and `LevelEdit.restoreAll()` puts them back.

## Items

In multiplayer, the level does not create items of its own. The client
keeps the level's item slots from spawning anything, except the recipe
books and smithing tools that crafting stations need. There are no level
items to remove.

To put items in the world, lay them on the ground:

- from a script, with `GroundItem.spawn`;
- in a world resource, with its `items` array (see below).

`GroundItem.spawn(item, position?, rotation?, amount?, virtualWorld?, properties?)`
takes a `name` in `properties`. `groundItem.name` reads it back, and is
empty for none. A name over 64 bytes of UTF-8 throws. A `name` that is not a
string is ignored. The name stays on the server; clients do not receive it.

```ts
GroundItem.spawn("arrow_crude", { x: 3103.0, y: 818.5, z: 57.3 }, undefined, 12, undefined, { name: "armoury-arrows" });

const arrows = GroundItem.all().find((stack) => stack.name === "armoury-arrows");
```

See [Ground items](ground-item-physics.md) for pickup and the other
`GroundItem.spawn` rules.

## World resources

A World Builder world resource (`<name>.world.json`, listed in
`mod.world_resources`) can carry three more arrays: `doors`,
`smartObjects` and `items`. Each is optional. See
[Exporting to a server](map-editor.md#exporting-to-a-server) for the file
and the server configuration.

When the server starts, it hangs the doors, places the smart objects and
lays the items down, all in the global world. It checks the format of
every entry before anything spawns. A bad entry stops startup with an error that names
the file and the entry, such as `Invalid door at doors[0]: ...`. Each array
holds at most 100,000 entries.

Because the server loads the file on every start, the items come back after
a restart even if players picked them up.

### `doors`

Each entry is a `DoorDefinition`, the object `Door.spawn` takes and
`door.toJSON()` returns:

```json
"doors": [
  {
    "model": "lvl1_door_a_left",
    "position": [3101.2, 815.4, 57.3],
    "rotation": [0, 0, 0.7071, 0.7071],
    "name": "jail-cell-1",
    "locked": true,
    "lock": "key",
    "lockpickable": false
  }
]
```

### `smartObjects`

Each entry is a `SmartObjectDefinition`, the object `SmartObject.spawn`
takes and `smartObject.toJSON()` returns:

```json
"smartObjects": [
  { "blueprint": "Table/table_dice", "position": [3102.0, 820.5, 57.3], "rotation": [0, 0, 0, 1], "name": "tavern-dice" },
  { "blueprint": "Bench/bench_2places_low", "position": [3104.5, 820.5, 57.3], "rotation": [0, 0, 0.7071, 0.7071], "bare": true }
]
```

The second entry is a [bare placement](#bare-placements): the two places of
a bench without the bench, for a prop the same resource puts there in
`objects`. An entry whose blueprint cannot stand bare stops startup, like
any other bad entry.

### `items`

Each entry lays one stack, by the rules of `GroundItem.spawn`:

| Field       | Type                     | Default         | Meaning                                                             |
| ----------- | ------------------------ | --------------- | ------------------------------------------------------------------- |
| `item`      | string                   | required        | The item class, by GUID or by name, as `GroundItem.spawn` takes it. |
| `position`  | `[x, y, z]`              | required        | Where it lies. Put it on the ground: it is placed, not dropped.     |
| `rotation`  | `[x, y, z, w]`           | no rotation     | Its resting orientation.                                            |
| `amount`    | whole number, 1 to 10000 | 1               | Units in the stack. A non-stackable item must use 1.                |
| `name`      | string                   | none            | Up to 64 bytes of UTF-8; `groundItem.name` reads it.                |
| `quality`   | whole number, 0 to 255   | the class's own | The game's grade.                                                   |
| `health`    | number, 0 to 1           | the class's own | The item's health.                                                  |
| `condition` | number, 0 to 1           | the class's own | The item's condition.                                               |

```json
"items": [
  { "item": "arrow_crude", "amount": 12, "position": [3103.0, 818.5, 57.3], "rotation": [0, 0, 0, 1], "name": "armoury-arrows" }
]
```

An item that is not a class the server knows, or a stack no client could
build, stops startup.

### Removing level doors and stations in a world resource

A world resource takes the level's doors and stations out with an entity
entry in its `world` array, the same level edit `Door.remove` and
`SmartObject.remove` make. This entry takes a Kuttenberg door out of every
world:

```json
"world": [
  { "kind": "entity", "guid": "0008eb7ef7bb1032", "class": "AnimDoor", "at": [3104.918, 812.9604, 57.3158], "hidden": true }
]
```

The edit appears in the resource's `levelEdits`, and the door in
`Door.removed()`. A script that calls `Door.restore` or
`SmartObject.restore` on it drops the edit, and it disappears from
`levelEdits`.

### `WorldResource` properties

`WorldResource.find(name)` and `WorldResource.all()` return handles with
these read-only properties. Each array property returns a fresh array in
export order, without objects that have been destroyed, restored or picked
up. Changing the array does not change the resource.

| Property                          | Contents                                                                                      |
| --------------------------------- | --------------------------------------------------------------------------------------------- |
| `name`, `path`, `level`, `loaded` | See [Reading exports from server scripts](map-editor.md#reading-exports-from-server-scripts). |
| `props`                           | `Prop[]`                                                                                      |
| `effects`                         | `Vfx[]`                                                                                       |
| `lights`                          | `Light[]`                                                                                     |
| `levelEdits`                      | `LevelEdit[]`, including the resource's door and station removals.                            |
| `doors`                           | `Door[]`: the doors the resource hangs.                                                       |
| `smartObjects`                    | `SmartObject[]`: the seats, beds, tables and stations it places.                              |
| `items`                           | `GroundItem[]`: its stacks nobody has picked up yet, each with the `name` the export gave it. |
| `areas`                           | `Area[]`                                                                                      |
| `patrolRoutes`                    | `PatrolRoute[]`                                                                               |

Read them after `WorldResource.ready` is `true` or from the
`worldResourcesReady` event, as described in
[Reading exports from server scripts](map-editor.md#reading-exports-from-server-scripts).

## Limitations

- **Catalog only.** `Door.spawn` takes only the leaf meshes the shipped
  levels use, and `SmartObject.spawn` only the 21 blueprints above. Both
  lists are compiled into the client and server; adding one needs a new
  build of both.
- **No scale.** Door and smart object definitions have no scale.
- **Doors are leaves.** A hung door has no frame or wall.
- **Fixed once created.** To change a hung door's model, position or lock
  type, or to move a smart object, destroy it and create it again.
- **NPCs and hung doors.** Server-spawned NPCs find their way on the
  game's own navigation mesh. In that mesh, each of the level's doorways
  names the door that stands in it, and an NPC opens that door as it
  arrives, or goes around it while it is locked. A hung door is not in the
  mesh:
  - Hung where the level has no doorway, NPCs never open it. Where the
    ground is walkable, their paths cross the spot as if nothing stood
    there, and they walk into the closed door.
  - Hung in the doorway of a level door you removed, NPCs treat the
    doorway as open, because the mesh still names the removed door. They
    walk through the new door even while it is closed or locked.

  Players are not affected: they open, lock and pick hung doors as they do
  the level's own.
- **NPCs and smart objects.** NPCs do not sit on, sleep in or work at
  placed smart objects.
- **Keys.** A hung door opens only to its `keyItem` and a lockpick, never to
  the generated home or shop key.
- **Not every station can be removed.** `SmartObject.remove` covers dice
  tables, alchemy tables, smithies, drying racks and smokehouses. The
  level's grindstones and cooking fires cannot be taken out or placed.
- **The smithy has no shop.** The smithy blueprints build the forge and its
  workplace, and leave out the shop and the chest the game's smithies carry.
- **Global-world players.** Players in the global world see the objects and
  removals of every virtual world, and can use hung doors and placed
  stations from any of them. They work the global world's copy of each
  level door, so a level door or station removed only from another world
  still works for them.
- **Placed GUIDs are not stable.** They change between runs. Store names.
- **World resources use the global world.** Their doors, smart objects and
  items always go in the global world.
- **Nothing is saved.** Recreate scripted objects and removals on boot, or
  ship them in a world resource.
