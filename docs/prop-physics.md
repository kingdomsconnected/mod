# Rigid props and weapon hits

Server scripts can push, stop and reset a prop that falls and rolls, and hear
when a player's weapon strikes the world. Together they are enough to fell a
tree: count the blows on it, stand a stump where it was, and let a copy of the
tree topple over.

Everything here runs in a **server** script, except `weaponHit`, which a
client script receives for the local player.

## Rigid props

A prop spawned with `"rigid"` physics, or switched to it with
`prop.physics = "rigid"`, is a physics body: it falls, collides and can be
pushed by players. These calls move that body:

| Call                                                  | What it does                                                                                           |
| ----------------------------------------------------- | ------------------------------------------------------------------------------------------------------ |
| `prop.applyImpulse(impulse, point?, angularImpulse?)` | Pushes it. `impulse` is in kg m/s; landing at `point`, off the centre of mass, it also turns the body. |
| `prop.setVelocity(velocity, angularVelocity?)`        | Sets how fast it moves and spins, whatever its mass.                                                   |
| `prop.wake()`                                         | Starts a resting body simulating, so it falls if nothing holds it up.                                  |
| `prop.sleep()`                                        | Stops it where it is, in mid-air if need be, until something hits it.                                  |
| `prop.resetPhysics()`                                 | Stops it and puts it back where it was spawned.                                                        |

The first four return `false` for a prop that is not `rigid` or has gone, for
a value that is not finite or is out of range (an impulse past 10,000,000, a
velocity past 200 m/s or a spin past 100 rad/s), and when no player is within
60 m to simulate the prop. `resetPhysics()` returns `false` only for a prop
that is not `rigid` or has gone.

### Mass

A body's mass comes from its collision mesh unless `prop.mass` sets it, in
kilograms. The mass is given to the body as it is built, so assigning
`mass` rebuilds the prop on every client; set it right after `Prop.spawn`,
before pushing. `0`, the default, takes the mesh's.

Some meshes carry no mass at all. A tree's collision is one: the level only
ever stands a tree still, and so do most walls, gates and buildings. A body
with no mass takes no push and feels no gravity, so every client builds such
a prop as a `static` one instead: it stands where it was placed, solid, and
pushes do nothing. Each client logs a warning naming the model:

```
Prop '<model path>' has no mass to fall or be pushed with, so it stands as a static prop; give it a mass to make it rigid
```

Maps made before 1.7.2 may stand walls and gates as `rigid`; they keep
standing. 1.7.2 itself refused such a prop, so it disappeared, in World
Builder and on servers alike.

To make it fall and roll, give it a mass:

```ts
const trunk = Prop.spawn(tree.name, cut, tree.rotation, tree.scale, "rigid");
trunk.mass = 500;
```

The client log prints each rigid prop's mass as it is built, and whether a
script set it: `Prop 12 ('tilia_cordata_d') stands as a rigid body of 500.0
kg, as its script set`. How hard to push depends on that mass;
`setVelocity` moves a body the same way whatever it weighs.

### How every player sees the same thing

The server does not simulate physics, so one client does it for everyone: the
client of the nearest player within 60 m. It keeps the prop until that player
is 90 m away, and then the next nearest player's client takes over. Every
push goes to that client, which runs the body and streams where it is. Every
other player sees the prop where that stream says, and their own copy cannot
move on its own. A player who arrives while the prop is falling sees it
falling.

When the body stops, the simulating client says so, and the server raises
`propRest`:

```ts
Events.on("propRest", (prop, position, rotation) => {
    // Every player already sees it at `position`.
    prop.physics = "static"; // keep it where it fell
});
```

`propRest` is raised every time the body stops, whatever moved it: a push, a
fall, or a player walking into it. `sleep()` raises it as soon as the body
stops. `resetPhysics()` does not.

With nobody within 60 m, a rigid prop stays where it last rested.

Limits:

- Only the simulating player pushes the body by walking into it. To another
  player it is solid but does not move, until the simulator's push reaches
  them.
- When the simulator changes while the body is moving, the new one starts it
  from where it was, at rest, so a fall can stall for a moment.
- Where the body stops is the simulating client's word. The server checks only
  that it lies within 100 m of where the prop last rested or was spawned; a
  prop reported farther is put back there and raises nothing.

## Weapon hits

`playerWeaponHit` is raised on the server when a player's swing meets
something solid that is not a body: a tree, a wall, a rock, a prop. Its `hit`
is a `WorldRayHit`, the same object `World.raycast` answers with:

```ts
Events.on("playerWeaponHit", (player, hit) => {
    if (hit.category === "tree" && hit.levelObject) {
        // hit.levelObject is what LevelEdit.apply takes.
    }
});
```

- `position` is where the weapon met it, and `normal` points out of the
  surface, against the swing.
- `distance` is how far that point is from the player's feet.
- `kind`, `model`, `category`, `material` and `levelObject` name what was
  hit, as for a trace. `surface` is the surface type, such as `mat_wood`.

It is raised once per object per attack, at the first contact, the way the
game itself counts contacts: a blade resting against a wall is not reported
again until the next swing. Fists and kicks are not reported. Neither is a
blow on a player, an NPC or an animal, which `playerHit` and the damage events
cover.

The report comes from that player's client. The server checks only that the
point lies within 8 m of the player and that the client is in the server's
level, and takes at most 16 a second from one player.

On the client of the player who swung, the same hit is dispatched as
`weaponHit: [hit: WorldRayHit]` first, for local effects such as a sound or
splinters. There `hit.entityId` and `hit.player` are filled in as for a
client trace.

## Felling a tree

Three blows on a tree swap it for its species' stump and drop a rigid copy
of it, tipped over away from the woodcutter:

```ts
// The game ships stumps for these; a birch or an oak has none.
const STUMPS: Record<string, string> = {
    tilia_cordata: "objects/natural/vegetation/trees/stumps/stump_tilia_cordata_a.cgf",
    fagus_sylvatica: "objects/natural/vegetation/trees/stumps/stump_fagus_sylvatica_a.cgf",
    pinus_sylvestris: "objects/natural/vegetation/trees/stumps/stump_pinus_sylvestris_a.cgf",
};
const blows = new Map<string, number>();

Events.on("playerWeaponHit", (player, hit) => {
    const tree = hit.levelObject;
    const species = tree && Object.keys(STUMPS).find((name) => tree.name.includes(name));
    if (hit.category !== "tree" || !tree || !species) return;
    const key = `${tree.name}@${tree.at.x},${tree.at.y},${tree.at.z}`;
    const count = (blows.get(key) ?? 0) + 1;
    blows.set(key, count);
    if (count < 3) return;

    LevelEdit.apply({ ...tree, model: STUMPS[species] });
    const cut = new Vector3(tree.at.x, tree.at.y, tree.at.z + 0.45);
    const trunk = Prop.spawn(tree.name, cut, tree.rotation, tree.scale, "rigid");
    trunk.mass = 500; // a tree's collision carries none

    // A slow turn about the base, away from the woodcutter; gravity does the rest.
    const away = new Vector3(tree.at.x - player.position.x, tree.at.y - player.position.y, 0);
    const length = Math.hypot(away.x, away.y) || 1;
    const spin = 0.3;
    trunk.setVelocity(new Vector3(away.x / length * spin * 5, away.y / length * spin * 5, 0), new Vector3(-away.y / length * spin, away.x / length * spin, 0));
});
```

The copy starts on top of the cut, so it does not start inside the stump's
collision; a stump taller than the cut needs a higher start, which a downward
`World.raycast` onto the stump finds. The stump and the felled tree are kept
in memory only: apply them again when the server starts if they should last.
