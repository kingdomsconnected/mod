# Find and remove herbs

Update the client and server together for this API; herb synchronization now
identifies the virtual world and acknowledges reload requests.

For a spherical search, use the same position and radius convention as
`Gathering.respawn`. Coordinates and radius are in metres:

```ts
const position = new Vector3(1000, 1000, 100);
const herbs = Gathering.getHerbs(position, 12);
const removed = Gathering.removeHerbs(position, 12);
const restored = Gathering.respawn(position, 12);
```

`Gathering.getHerbs(position, radius, virtualWorld?)` and
`Gathering.removeHerbs(position, radius, virtualWorld?)` default to world `0`.
The radius is an inclusive 3D distance, from `0` to `100000`. Positions must
have finite coordinates within `-100000` to `100000`. Invalid arguments
throw before changing anything. Use an `Area` when you need a different shape.

Server scripts can list and remove the herbs inside an `Area`. This works for
scripted boxes, spheres and polygons, as well as the level's own areas and
area unions. Mushrooms are not included in these queries or removals.

```ts
const clearing = Area.create({
  id: "building-clearing",
  type: "sphere",
  position: new Vector3(1000, 1000, 100),
  radius: 12,
  virtualWorld: 0,
});

const herbs = clearing.getHerbs();
const hasHerbs = herbs.length > 0;
const removed = clearing.removeHerbs();

// Restore them when the clearing is no longer needed.
const restored = clearing.respawnHerbs();
```

`getHerbs(virtualWorld?)` returns the available authored plants whose source
positions fall inside the area's current geometry. Each `GatheringHerb` has a
`kind` (species ID), `item` (item class GUID) and `position` (`Vector3`). Picked
and removed plants are excluded. The server uses the compiled level catalog,
so no player needs to be nearby for the query to work. Plants added by custom
map assets are not part of that catalog.

`removeHerbs(virtualWorld?)` removes all catalogued plants inside the area,
including picked plants that were waiting to regrow. It returns the number
newly marked for removal; repeating the call returns zero. It gives no items
or experience and does not raise `gatheringHarvest` or `gatheringHarvested`.
Connected players receive the change, and players arriving later receive it
when they approach the area.

Removed plants stay removed until an explicit respawn or a server restart.
Changing `Gathering.setRespawnTime` or `Gathering.resetRespawnTime` does not
restart their timers. `respawnHerbs(virtualWorld?)` restores both picked and
removed plants inside the area's current geometry and returns the number
restored. `Gathering.respawn` and `Gathering.respawnAll` can also restore them.

Each call affects one virtual world. The default is the area's world, or
world `0` when the area belongs to every world. Pass a world ID to operate
there explicitly. For operations around a player, pass `player.virtualWorld`:
players start in the global bucket (`0xffffffff`) unless the resource changes
it. That ID selects only the global bucket; it does not affect every world.
An area assigned to a different concrete world returns an empty list or zero.
Invalid arguments, fractional or negative world IDs throw before changing
anything.

These methods use geometry even when `area.enabled` is false. Moving,
reshaping, disabling or destroying the area does not undo earlier removal.
A destroyed area's methods return an empty list or zero. To restore the
original clearing, call `respawnHerbs` before moving or destroying it, or use
the `Gathering` respawn methods.

Removal state is held in server memory. For a clearing that should survive
restarts, save its definition with `area.toJSON()`, recreate it with
`Area.create` when your resource starts, and call `removeHerbs` again.
Stopping a resource does not restore its removed plants automatically.
