# Animals and horse navigation

Server scripts can create mammal NPCs through `Npc.create`, move them with the
existing NPC orders, and order wolves or wild dogs to attack. Horses keep their
separate `Horse` API and now accept navigation orders too. Both client and server
must use this build because horse replication carries new navigation fields.

The client suppresses the game's animal NPC population. The native
`AnimalSpawner` scheduler stays disabled, and the entity-system population sink
also refuses directly placed or native-scripted mammals, horses and dogs before
they are created. Server-scripted `Npc.create`, `Horse.spawn` and `Dog` bodies
pass through the existing `ActorSpawner` replica path. Animal spawnpoint rows
remain available as data for server scripts; they do not enable native spawning.

Default chicken flocks are also suppressed: `Chickens`, `ChickensBrownLight`
and `ChickensWhite` are refused before their scripts create any Boids. Chickens
still have no server spawning API in this branch. Other ambient wildlife and
inventory preview dummies are unaffected.

`builds\build.bat RunKCDCTests 64` from the Framework root checks
spawn admission against every catalogued animal and authored spawnpoint class,
plus chicken variants, horses, dogs, replica names and unrelated entity classes.

## Spawn an animal

```ts
const sheep = Npc.create({
    soul: "animal_sheep_ewe",
    position: spawnPosition,
    nametag: false,
    interactable: false,
});
sheep.moveTo(destination, { speed: "walk", pathfinding: "server" });

const wolf = Npc.create({ soul: "animal_wolf_kutnohorsko", position: wolfPosition });
wolf.attack(player);
// Cancel pursuit and native combat:
wolf.hold();
```

`Npc.animals()` returns `{ name, soul, actorClass }` rows. A name or soul GUID
selects its matching native class automatically. An explicit conflicting `class`
is refused. Animals use their native models; human outfits, worn items and
appearance overrides are refused. Wolves and wild dogs accept melee attack
orders. Other catalogued animals and animal bow attacks are refused.
Predators use native ordinary bites, including native collision and damage.
Paired takedown animations are not supported by this attack path.

The catalog contains 167 souls for sheep, cattle, pigs, boar, hares, red deer,
roe deer, wolves and wild dogs. It includes quest variants; choose a generic
`animal_*` entry for ordinary populations. Horses use `Horse.spawn`, companion
dogs use `Dog`, and chickens need a separate flock implementation. Chickens are
Boids entities, so they are not included in this actor catalog.

## Flee

```ts
deer.flee(hunter); // a player, an NPC, a network ID or a place
```

An animal told to flee runs the game's own escape on its simulator.
It can take up to five seconds to begin, after which the order fails.
A threat with an NPC record is fled from as itself, anything else from where
it stands when the order arrives. Observers draw the pose and gait the
simulator reports.

`radius` only decides when `npcIntentDone` reports `reached`; once clear,
the animal goes on until another order replaces the flee. Another order,
a new simulator, freezing, death or a failed handover stops the move.

## Find authored spawnpoints

`Npc.animalSpawnpoints()` returns the running level's authored `AnimalSpawner`
placements. Pass a level name to inspect it, or `"*"` for every level. This build
contains 337 Trosky and 711 Kuttenberg placements.

Each row exposes `guid`, `level`, `name`, `layer`, `soul`, `actorClass`,
`position`, `count`, `respawnDays`, `spawnInFlock` and `spawnAreaGuid`. Entity GUIDs
are hexadecimal strings, preserving all 64 bits. The source includes mission
objects and layer files, deduplicated by GUID. Quest-layer markers are catalogued
as data; their presence does not mean that a single-player quest enabled them.

`position` is an authored marker, not a guaranteed ground position. Project it
with `Navigation.closestPoint` before spawning and handle a null result. A linked
spawn area's GUID is exposed for scripts that want to consult the area catalog.
The sample uses a small navigation radius around the marker, not the original
area's entire shape. `count`, `respawnDays` and `spawnInFlock` describe the game's
configuration; they do not start native spawning or impose a multiplayer policy.

## Sample population and respawns

The default game mode automatically populates the level's authored animal sites
in world 0 once the [server navigation mesh](navigation.md) is ready. Without a
mesh, it creates no animals. In
[`animals.ts`](../resources/kcdc-gamemode/src/server/animals.ts),
`animalSpawnpoints: null` selects all populated sites. Set it to a list of GUIDs
to select sites, or `[]` to disable automatic populations.

Use `/animals list` to find nearby sites and `/animals start <spawnpointGuid>` to
enable one in your virtual world. `/animals stop` clears enabled sites and
removes only animals owned by this sample. Automatic sites stay cleared until
the resource restarts; individual sites can still be enabled with the command.

The sample allows up to four animals per site and 256 spawned bodies in total,
including retained corpses. Pending replacements do not reserve global capacity.
Living animals are removed when every player in their world is at least 350
metres from both their site and their current position, freeing capacity for
other sites. They can spawn again when players return. The sample creates at
most two animals per second and performs at most four navigation queries per
tick. Animals roam within 20 metres
of their home. Wolves and wild dogs acquire active players within 18 metres and
give up pursuit outside a 45 metre home leash.

A death starts one replacement timer of five minutes plus up to one minute of
jitter. Repeated death events do not reset it. A body remains for at least 90
seconds and is removed only when players in that world are at least 60 metres
away. Its replacement waits for both the timer and corpse removal. Unexpected
entity removal also starts a replacement delay.

Initial spawns and replacements require a ready player within 350 metres, with
all players in that world at least 60 metres from both the marker and chosen
spawn position. Visibility includes the global virtual world, as it does in
replication. This is distance gating, not a line-of-sight test. Failed placement
retries after 15 seconds. Resource shutdown clears the timer and tracked animals.
These timers are sample rules in real milliseconds; they are not persisted across
resource or server restarts and do not use the catalog's single-player day count.

## Navigate an unridden horse

```ts
horse.moveTo(destination, { speed: "walk", radius: 1.5 });
horse.patrol([first, second], { speed: "jog", loop: true, waitSeconds: 3 });
horse.hold();
Events.on("horseIntentDone", (horse, status) => {
    console.log(`${horse.id}: ${status}`);
});
```

`moveTo` and `patrol` return false without a loaded mesh, for a dead or mounted
horse, or invalid input. `navigationStatus` reports `idle`, `running`, `reached`,
`blocked` or `failed`. Mounting cancels the route; dismounting does not restart it.
Call `hold()` before teleporting a horse or moving it to another virtual world.

Horse orders reuse `NpcOrder`, its planning queue and the existing navmesh. They
exclude door links. With no simulator the server advances the route; when a
client owns the horse it drives the native movement controller and reports
progress against the server order's revision. Riders retain control while mounted.
The horse turns toward each waypoint at up to 120 degrees per second and waits
to move forward while the target is behind it. The server also updates its facing
when advancing a route without a client simulator.
The mesh was built for game NPCs, so narrow passages can still block a horse;
handle `blocked` and `failed` instead of retrying every frame.

## Horse stats

A horse has no stat fields of its own: speed, stamina, courage and carrying
capacity are its soul's derived stats, and the game changes them with buffs.
`horse.addBuff` / `removeBuff` / `hasBuff` / `clearBuffs` / `buffs` put the
server's effects on every copy of the horse; `docs/custom-buffs.md` has the
modifiers (`runSpeed`, `moveSpeed`, `stamina`, `courage`, `carryCapacity`)
and an example. Gear changes stats too: horseshoes and saddles carry their
own buffs.
