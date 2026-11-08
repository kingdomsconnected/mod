# Previews and placement: `PropGhost` and `PropPlacer`

A resource can put the game's own geometry into the world as a preview -- a
translucent copy that is *in* the world without being *part* of it -- and can
run a placement mode where that preview follows the crosshair, colours itself
for whether the spot is accepted, and is put down with a click.

The map editor's placement brush uses the same ghost, driven by the editor's
own camera; see [map-editor.md](map-editor.md).

---

## Two layers

**`PropGhost`** is one preview and nothing else: create it, move it, tint it,
drop it. It has no rules, no input and no loop, which is what makes it useful
on its own -- a turntable in a build menu, an outline of where the server says
a zone is, a debug mesh you can see through.

**`PropPlacer`** is the mode built on it. Hand it a spec and it runs the whole
loop natively: ray the crosshair, move the ghost, evaluate the rules, recolour
on a flip, and report what the player did. The resource writes no tick at all.

---

## A preview on its own

```ts
const ghost = PropGhost.create({
  model: "objects/manmade/structures/defensive/watchtowers/watchtower_a.cgf",
  position: somewhere,
  tint: new Color(0.25, 0.95, 0.4, 0.45),   // a < 1 makes it translucent
});

ghost.setPose(position, rotation, 1.0);
ghost.setTint(new Color(1, 0.2, 0.15, 0.45));
ghost.setTint(null);          // back to the materials the mesh shipped with
ghost.getBounds();            // { min, max, centre, size } in world space
ghost.destroy();
```

A blueprint of several meshes is the same call with parallel arrays, which
line up by index:

```ts
PropGhost.create({
  models:    ["…/scaffolding_floor_only_a.cgf", "…/scaffolding_main_beam_c.cgf"],
  offsets:   [new Vector3(0, 0, 0), new Vector3(0, 0, 2.2)],
  rotations: [identity, identity],
  position,
});
```

The whole ghost turns and scales as one; the offsets are in its own frame.

**Only paths the shared prop catalog carries are accepted**, and a path outside
it throws rather than drawing nothing. That is deliberate: a preview of
something the server could never be asked to spawn afterwards is a preview of a
dead end.

**It is not a prop.** Nobody else can see it, nothing about it is replicated,
it has no collision, and a level load destroys every one. `ghost.valid` is what
says whether it is still standing.

---

## Placement

```ts
const started = PropPlacer.begin({
  model: "objects/…/watchtower_a.cgf",
  validTint:   new Color(0.25, 0.95, 0.4, 0.45),
  invalidTint: new Color(1, 0.2, 0.15, 0.45),
  range: 40,
  faceAwayFromPlayer: true,
  keepOpen: true,
  rules: {
    maxSlope: 35,
    minDistance: 2,
    maxDistance: 35,
    clearance: 1.5,
    requireSurface: true,
  },
});

PropPlacer.on("confirm", (pose) => Events.emitServer("build", pose));
PropPlacer.on("rejected", () => Hud.showInfoText("Not there."));
PropPlacer.on("cancel", () => {});
PropPlacer.on("end", () => {});
```

Left click places, right click ends the session, and the mouse wheel turns the
preview. `PropPlacer.rotate(degrees)` does the same from script, and
`PropPlacer.getPose()` reads the aim without waiting for an event.

### The rules are a hint, not a decision

Everything in `rules` exists to colour the preview at *one* client. It is not
authority and it must not be treated as any:

> Two players aiming at the same patch of ground both see green.

What actually gets built is the server's call, made again from the pose the
client confirmed. The shape that works:

```
client   PropPlacer.begin(…) → on("confirm") → Events.emitServer(…)
server   Events.onClient(…) → re-measure the pose → Prop.spawn(…)
```

`kcdc-gamemode`'s `/build` is that, end to end, in about two hundred lines --
`src/client/build.ts` and `src/server/build.ts`.

### A rule the engine cannot answer

`rules` covers what the engine can settle from the ray it has already traced:
the slope, the surface name, the distance, and what else is standing nearby.
Anything past that -- what the player can afford, what the server has already
refused -- goes through the latch instead:

```ts
PropPlacer.setVeto(true);    // every spot refused until cleared
```

That costs the placement loop nothing, which is the point. There is deliberately
no per-frame callback into script: `move` is throttled, and fires on a validity
flip or once the aim has travelled far enough to be worth saying so.

### What the mode takes, and what it leaves alone

While a session is up the mode adds its own action map *and* turns on an
action filter over the game's attack and block actions. That is the whole of
what it takes: left click stops swinging and right click stops raising a
guard, while **mouse look and movement carry on as normal**, because those are
`rotatepitch`, `rotateyaw` and the movement actions and the filter never
touches them.

An exclusive action map would also take mouse look. The action filter blocks
only the named actions before dispatch, so the player can still aim the
preview and move around it.

Ending the session turns the filter off again.

One session at a time, and it belongs to the resource that started it: a
resource that stops takes its session and its ghosts with it. `end` fires
whenever the session stops, for any reason -- a right click, a level load, the
mod losing the controls -- and is always the last event, so it is the right
place to tell the server the client has left the mode.

---

## `Camera`

Placement needs to know where the player is looking, so the view is exposed
too:

```ts
const pose = Camera.getPose();           // position, forward, right, up, fov, aspectRatio
const ray  = Camera.screenRay({ range: 40 });
const hit  = World.raycast(ray.origin, ray.target, { mode: "ground" });
```

`screenRay` takes a point in normalized device coordinates -- `(0, 0)` is the
centre of the screen, which is where the crosshair is -- and is what turns
"what is the player looking at" into the two points `World.raycast` takes.

The basis is read off the camera's own matrix rather than rebuilt from a yaw
and a pitch, so it carries roll. It is the *game's* camera, whichever one the
game currently has; `NoClip` is what takes it over.

### From the world back to the screen

The opposite direction -- where a world point lands on screen, to pin a Web
view or a label to it -- is `Hud.worldToScreen`:

```ts
Hud.worldToScreen(point: Vector3): { x: number; y: number; onScreen: boolean } | null
```

It projects through the renderer's own `ProjectToScreen`, against whichever
camera the frame is drawn with (`ISystem::GetViewCamera`), whichever one the
game currently has. `x` and `y` are in the pixels Web views are placed in,
which follow the game window's client area, so the answer can be handed to
a Web view's position as it is.

- A point past an edge of the frame keeps its coordinates, running below 0
  or past the width or height, with `onScreen` false -- enough to clamp an
  arrow to the edge pointing at it.
- A point behind the camera answers null: the projection mirrors through
  the eye there, so it has no place on the frame at all.
- It also answers null when there is no view to place the point in (no
  Web viewport yet) or the projection is not finite.
- A missing or non-finite `point` throws.

The answer is for the frame being drawn; a point that should follow a moving
body is projected again every frame.

### Trace results

`World.raycast` and `World.raycastAll` on the client answer with
`WorldRayHit` objects directly. The server's `World.raycast(player, ...)` and
`World.raycastAll(player, ...)` borrow that player's client to run the same
trace and settle a promise with a `WorldTraceResult`: `answered` (false when
the client has no world loaded, is in another level, did not answer within
five seconds, or left), `reason` (empty when it answered), `hit` (the nearest
hit or null) and `hits` (everything met, nearest first). The answer covers
only what that client has streamed in, and it is a client's word.

Every hit carries:

| Field         | What it holds                                                                                               |
| ------------- | ----------------------------------------------------------------------------------------------------------- |
| `position`    | Where the ray met it, in world-space metres.                                                                |
| `normal`      | The surface normal there, as a unit vector.                                                                 |
| `distance`    | How far along the ray, in metres.                                                                           |
| `surface`     | The surface type, as `Libs/MaterialEffects/SurfaceTypes.xml` spells it.                                     |
| `terrain`     | Whether the ground itself was hit; the same as `kind` being `terrain`.                                      |
| `kind`        | `terrain`, `entity`, `vegetation`, `brush` or `static`.                                                     |
| `model`       | The mesh of a vegetation instance or brush, by its load path; otherwise null.                               |
| `category`    | `tree`, `bush`, `plant` or `rock`, from `model`'s folder; otherwise null.                                   |
| `material`    | The (sub-)material drawn where the ray met it, by its path; otherwise null.                                 |
| `entityGuid`  | The level's sixteen-hex-digit GUID; null for terrain, static geometry and anything the session spawned.     |
| `entityName`  | The name the level gives it, often empty; null when nothing named was hit.                                  |
| `entityClass` | The engine class -- `AnimDoor`, `NPC_NAI`, `GeomEntity`; null when nothing with a class was hit.            |
| `entityId`    | Client only. This machine's own handle, not portable; null for terrain and static geometry.                 |
| `player`      | Client only. The player whose body it was, the local one included when `ignoreSelf` is off; otherwise null. |

**`surface`** says what the thing is made of -- how footsteps and blows
sound -- not what it is. Among the game's 74: `mat_wood`, `mat_wood_soft`,
`mat_vegetation`, `mat_bushes`, `mat_rock`, `mat_rock_unwalk`,
`mat_rock_horse_ignore`, `mat_stairs_stone`, `mat_gravel`, `mat_soil`,
`mat_mud`, `mat_grass`, `mat_road`, `mat_metal`, `mat_glass`, `mat_plaster`,
`mat_thatch`, `mat_fabric`, `mat_flesh`, `mat_water`. A plank wall is
`mat_wood` as much as a trunk is, so a tree is told by `category`. The field
is empty only before the material tables are up.

**`kind`** is what the ray stopped on:

- `terrain` -- the ground. Nothing lies behind it, so a trace stops there.
- `entity` -- anything with an `entityClass`: doors, `rigid` props, NPCs,
  players. `model`, `category` and `material` are null.
- `vegetation` -- an instance the level paints on: trees, bushes, plants,
  and boulders painted the same way.
- `brush` -- a static mesh placed one by one: walls, houses, rocks, cliffs,
  and the `static` and `none` props a server spawns.
- `static` -- static geometry that is neither, or whose source the engine
  does not record. `model`, `category` and `material` are null.

**`category`** is the game's own filing of natural objects, read from
`model`'s path: `tree` for `objects/natural/vegetation/trees/` (living, dead,
fallen and stumps alike -- `model` tells them apart), `bush` for
`objects/natural/vegetation/bushes/`, `plant` for the rest of
`objects/natural/vegetation/` (grass, ferns, mushrooms, branches) and `rock`
for `objects/natural/rocks/` and `objects/natural/stones/`. Anything else,
man-made included, is null.

**`levelObject`** names an object the level placed in the form
`LevelEdit.apply` finds it again by, on every machine:

- `kind`: `brush` for a static mesh or a vegetation instance (a wall, a
  rock, a tree), `entity` for anything the level gives a GUID (a door, a
  chest).
- `name`: a brush's mesh path, the same as `model`, or an entity's name.
- `guid` and `class`: an entity's level GUID and engine class. A brush has
  neither.
- `at`: where the level put the object. This is its pivot, not the point the
  ray met, and it stays the same while a level edit has moved the object,
  because it is the key.
- `rotation` and `scale`: how the level turned and sized the object.

`levelObject` is null for terrain, for static geometry the engine records no
source for, and for anything the session spawned: props, players and NPCs.

A felled tree is the common use. Trace from the player, then take the tree
out and stand a stump in its place, collision included, in one call:

```ts
const result = await World.raycast(player, from, to, { mode: "anything" });
const tree = result.hit?.levelObject;
if (tree && result.hit?.category === "tree") {
    LevelEdit.apply({ ...tree, model: "objects/natural/vegetation/trees/stumps/stump_fagus_a.cgf" });
}
```

`LevelEdit.apply` reads a rotation and scale only with a `position`, so
spreading `levelObject` into a definition moves nothing. To let the tree fall,
spawn a `rigid` prop of the tree's own `model` at `at` with the object's
`rotation` and `scale`, push it with `applyImpulse`, and swap or hide the
original as above. [Rigid props and weapon hits](prop-physics.md) shows the
whole felling, counting the axe blows with `playerWeaponHit`. A tree's
`model` and `at` are the same on every client on the same level, so they are
also what a server keeps a felled tree or a mined rock by.

**A level brush** has a collider that `C_PhysicalizationManager` built
with no render node behind it, so the collider alone cannot name its mesh.
The client keeps the pairing it made when it built those colliders
(`LevelColliderModule`), and a hit on one reports `kind: "brush"`, the mesh's file path as `model` and its
material, exactly as a brush with its own render node does. A collider
found in neither place reports `static`.

`model` and `material` are capped at 256 characters, what the wire carries
for a path.

---

## Cost

A ghost is a `CBrush` render node per mesh -- no entity, no script table, no
physics proxy -- and moving one is a single engine call. The budget is 64 live
ghosts per client and 32 meshes per ghost; past either, `create` answers null
rather than letting a leaking resource take the session down.

The translucency is real alpha blending rather than a swapped material: the
ghost's materials are private copies, and an opacity under 1 is what moves them
into the renderer's transparent pass. A tint the ghost already carries is
dropped rather than re-applied, so `setTint` is safe to call every frame.
