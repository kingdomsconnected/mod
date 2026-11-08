# Siege engines

A server script can build a trebuchet or a cannon, load it, and fire it at a
point, or let players work it themselves. Every player nearby sees the engine
work, sees the projectile fly, and sees where it lands. The blast hurts the
players, NPCs and other engines it lands on.

```ts
const trebuchet = SiegeEngine.spawn("trebuchet", position, rotation);
trebuchet.speed = 4;                  // a crew four times the game's pace
trebuchet.load();                     // siegeEngineReady fires when it can shoot
Events.on("siegeEngineReady", (engine) => engine.fire(target, gunner));
Events.on("siegeImpact", (engine, position, attacker) => { /* ... */ });
```

The default gamemode's `/siege` command builds, loads and fires them; see
[The `/siege` command](#the-siege-command).

## Working an engine

An engine with `usable` set can be worked by players.

**Taking its place.** A player within 2 m of an engine nobody operates gets
the game's own "Press X to Y" hint on the use key. A press raises
`siegeEngineUse(player, engine, "operate")`; unless a handler returns false,
the player becomes the engine's `operator`. Nobody else can take the place
until they leave it.

**In the place**, the keys the player already has bound become the engine's,
shown as the game's own hints:

| Key          | What it does                            |
| ------------ | --------------------------------------- |
| block (held) | lays the engine where the player looks  |
| torch        | loads it (`siegeEngineUse` with `load`) |
| attack       | fires it where it is laid (`fire`)      |
| use          | steps away (`leave`)                    |

While the player works the engine, the game's own answers to those keys --
the swing, the guard, the torch and the use prompt -- are filtered away
(`kcdc_siege_block`), and come back when they leave. The look is traced into
the world while the aim key is held and sent to the server as `SiegeAim` a
few times a second, only when it has moved. The server lays the engine on it
if it can reach it, and otherwise on the nearest point along the same line
it can reach, so a look at the far hills lays it as far as it throws.

The Fire hint says how far the shot would go. With `setAimGuide(true)` --
it is off by default -- a translucent red beam also runs from the engine along the
shot's arc to where it would come down, with a ball there.
From a cannon it is all but straight, like a beam of light; from a trebuchet
it is the curve the stone takes. It is built from the game's own primitive
meshes as preview brushes (the prop placer's ghosts), and traced through this
client's world from the same launch every client solves, so a wall in the way
is where it ends. Firing before laying the engine anywhere throws it along
the player's facing at `range`.

The operator lets go of the place by pressing use, by walking more than
16 m from the engine, by leaving the session, or when the engine is wrecked.

**Crew.** A trebuchet has two winches. With `crewRequired` set, its arm is
winched down only while that many of them are manned: with fewer hands the
winding holds where it is, and goes on when they come back. A player
standing at a free winch is offered it on the use key (`winch`). Taking it
puts them at the drum facing it, and while the arm is being winched down
they play the game's own pull (`Trebuchet` fragment, `trebuchetPullA` or
`trebuchetPullB`), held in place. A crewman who walks more than 4 m from
the winch has let go of it.

`setOperator(player)` puts somebody in the place without asking.

## Health, wrecks and ammunition

Engines are unharmable by default. Give one a `maxHealth` and every blast
within a shot's `damageRadius` of it -- any engine's shot, its own included
-- takes its share of that shot's `damage`, raising `siegeEngineDamage`.
At 0 the engine is wrecked (`siegeEngineWreck`): a cannon is shown as the
game's own `cannon_broken.cgf`, and a trebuchet, which has no broken mesh,
leans and settles half a metre into the ground. Both throw up the Nebakov
cinematic's dust and debris. A wreck cannot be loaded, laid or fired, its
operator and crew are let go, and a stone still in its sling goes down with
it. `repair()` or a new `maxHealth` makes it whole and idle again.

`ammo` counts the shots left; each `load` takes one, and an engine with none
refuses to load. -1, the default, never runs out.

## What the game has

KCD2 has no siege gameplay to hook into. The trebuchet and the cannon exist
only as cutscene props. The game ships no catapult and no ballista, so this
feature offers neither.

- **The trebuchet** is a skinned character,
  `objects/characters/assets/trebuchet/trebuchet.chr`. Its DBA ships four
  clips:
  - `quest_trebuchet_default`: the arm down.
  - `quest_trebuchet_pull_back`: the arm winched down to cocked (65 s).
  - `quest_trebuchet_load`: the crew laying the stone (40 s).
  - `quest_trebuchet_fire`: the arm let go (45 s).

  Their anim events play the creaks. The stone is not part of the model. A
  cutscene flies a separate `trebuchet_stone.cgf` along the path of the
  `projectile` bone. That bone's parent is the root, so its track is in
  model space.
- **The cannon** is a static mesh,
  `objects/manmade/task_specific_props/combat/cannon.cgf`, with a separate
  ball, `cannon_ammo.cgf`. Its barrel lies along the mesh's -Y, so it is
  placed turned 180 degrees (the catalog's `modelYawDeg`), and the ball
  leaves from the muzzle, 0.94 m ahead of the pivot and 0.54 m up.
- **Crew animations** exist as the Mannequin fragment `Trebuchet` with the
  tags `trebuchetLoad`, `PullA`, `PullB` and `Fire` (including a player
  version of Fire). They are already in the animation catalog, so
  `player.playAnimation` and the NPC equivalent can play them.
- **Sounds.** `c_trebuchet_stone_shoot` and `c_trebuchet_stone_impact` are
  declared with no FMOD event, so they play nothing. The impact uses the
  cutscene's `q_M50_trebuchet_impact`, and the cannon uses
  `q_M44b_cannon_shot`. Both were made for a single set piece: their
  triggers set `distance_culling` to 65535 m, and the FMOD events have no
  distance falloff, so as shipped they play at full volume at any range.
  The client gives them the range the catalog sets (300 m for the shot,
  150 m for the impact) as the audio system loads its control files, and
  fades them out toward it, following the listener as it moves, so the
  cull lands where the sound is already silent. Both FMOD events are 3D,
  so the game pans them from where they were fired.

`tools/generate_siege_catalog.py` mines the clip lengths, the release and
the sling path into `code/shared/generated/siege_catalog.cpp`. It finds the
release by reading the `projectile` track: the first key after the sling's
fastest swing where the stone's direction turns less than a degree. That is
3.5 s into the fire clip, 18.6 m up, at 45.8 degrees above forward. The
crew's `Detach` event on `trebuchetLoad` gives the moment the stone is laid
in the sling.

## How it is built

**The engine** is a replicated `SiegeEngineEntity`. It carries the asset,
the step it is in (`Idle`, `Drawing`, `Loading`, `Ready`, `Firing`,
`Wrecked`), the server clock that step started on -- or, for a winding held
for want of crew, how far it had got -- a speed multiplier, its aim, its
health and ammunition, who operates it and who mans its winches, and its
latest shot. The motion is never streamed. Each client builds the model as
a static `BasicEntity`, then plays the step's clip from the server clock in
TrackView-exclusive mode, exactly as
the gate module does (`Door::Gate`). The stone in the
sling is a second entity, held on the mined sling path until the release.

**The shot** is solved on the server when `fire` is called:

1. The engine turns to face the target. A trebuchet's speed is solved for
   the distance at its fixed release angle. A cannon fires at a fixed
   80 m/s, so its elevation is solved, taking the lower arc; its barrel is
   raised about the back of its bed, which carries the muzzle up with it, so
   the solve goes round a few times until the muzzle settles.
2. The shot -- its origin, its velocity and the server clock it is let go at
   -- is written onto the engine at once. Every client that has the engine
   launches the stone itself at that clock, so it leaves the sling exactly
   as the clip lets it go, with no round trip. A client that streams the
   engine in while the stone is in the air launches it where it has got
   to; one that comes in after it landed shows nothing.
3. At the release time, `SiegeShot` carries the same shot to every player
   within 400 m of the release point or the target, and always to the
   player asked to report it. A client already flying the stone only takes
   from it whether it is the reporter.
4. Every client draws the stone along the same analytic arc
   (`siege_ballistics.h`) from its own clock. Each frame it traces the step
   since the last frame against what a player would collide with. Nothing
   is simulated, so every machine shows the same flight.
5. The server has no geometry to trace against. So one client is asked to
   report where its stone stopped (`SiegeImpactReport`): the attacker when
   they stand within 400 m of the target, else the player nearest the
   target within 400 m. With nobody there, nobody reports. The server
   accepts the report
   only within 8 m of the solved arc. An unreported shot lands on its target
   once its flight time plus 4 s has run out.
6. `SiegeImpact` goes to every player within 400 m of the release point or
   of where the stone came down. Each one lands the stone if it is
   still flying, plays the impact effect and sound once, and takes its own
   player's share of the blast through `I_CombatSoul::TakeDamage`, with the
   shooter's soul as the attacker. The game's own damage hook then reports
   it to the server like any blow, so the server raises `playerDamage`. NPC
   health is the server's ledger, so `Npcs::Damage` takes the NPCs' share
   on the server.

Damage is full at the point of impact and falls to a quarter at
`damageRadius`. The same share is taken off any engine with a `maxHealth`
in reach, measured to the nearer of its base and a point 3 m above it.

**What everyone sees and hears.** A cannon's barrel eases to the elevation
its aim solves to at 12 degrees a second, the fuse sparks and fizzes at its
touch hole as a shot starts, and the gun kicks back 35 cm and is run out
again over a second and a half. A ball passing within 15 m of the listener
is heard going by (`q_M44b_cannon_ball_whiz_by`). The local camera shakes
near an impact and beside a firing gun, fading with the square of the
distance (`IView::SetViewShake`, as `CPlayer::CameraShake` calls it; the
game's own `actor:CameraShake` is a no-op in KCD2, and `wh_c_CameraShake`
still gates the whole of it). The big sounds are heard as late as sound
takes to get there, at 343 m/s. Engines and stones are drawn a hundred times
as far as their size would earn them (`IRenderNode::SetViewDistRatio(255)`),
or a stone would vanish a second into its flight. Both values default from the catalog (trebuchet 150 within
6 m, cannon 120 within 4 m), and a script sets them per engine.

## Scripting API

Server-side, in `scripting-api/generated/server-api.d.ts`.

**Building and finding engines**

| Call                                                          | What it does                                                                                                                                                                 |
| ------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `SiegeEngine.spawn(kind, position, rotation?, virtualWorld?)` | builds an unloaded `trebuchet` or `cannon` at `position`; `rotation` is a Quaternion or Euler degrees, `virtualWorld` defaults to the global one. Throws for any other kind. |
| `SiegeEngine.all(virtualWorld?)`                              | every engine, or those in one world                                                                                                                                          |
| `SiegeEngine.getById(id)`                                     | the engine with that network id, or `null`                                                                                                                                   |
| `SiegeEngine.destroyAll(virtualWorld?)`                       | removes them, raising `siegeEngineDestroy` for each; returns how many                                                                                                        |

**Properties**

| Property                             | Meaning                                                                                                                                              |
| ------------------------------------ | ---------------------------------------------------------------------------------------------------------------------------------------------------- |
| `kind`                               | `trebuchet` or `cannon`                                                                                                                              |
| `cycle`                              | `idle`, `drawing` (the arm winched down), `loading` (the stone laid), `ready` or `firing`. Named `cycle` because every entity already has a `state`. |
| `loaded`                             | whether `cycle` is `ready`                                                                                                                           |
| `speed`                              | the cycle's pace, clamped to 0.1-20; 1 is the game's own. A change mid-step carries on from where the engine has got to.                             |
| `damage`                             | health taken at the point of impact, 0-10000; a player has 100. 0 leaves hits to `siegeImpact`.                                                      |
| `damageRadius`                       | how far from the impact anything is hurt, 0-100 m                                                                                                    |
| `usable`                             | whether players may [work the engine](#working-an-engine); off by default                                                                            |
| `range`                              | how far a shot goes along the operator's facing when they fire before laying it, kept between `minRange` and `maxRange`                              |
| `maxHealth`, `health`                | what it can take and what is left; `maxHealth` 0, the default, is unharmable                                                                         |
| `wrecked`                            | whether its health ran out                                                                                                                           |
| `ammo`                               | shots left to load, -1 for no limit                                                                                                                  |
| `operator`                           | the player in its place, or null                                                                                                                     |
| `crew`, `crewPlaces`, `crewRequired` | who mans each winch, how many it has, and how many must be manned for it to wind                                                                     |
| `minRange`, `maxRange`               | the closest and furthest ground distance the engine can be aimed at (read-only)                                                                      |
| `loadDuration`                       | how long `load` takes at the engine's speed, in seconds (read-only)                                                                                  |

| Kind      | `minRange` | `maxRange` | default `range` | default `damage` | default `damageRadius` |
| --------- | ---------- | ---------- | --------------- | ---------------- | ---------------------- |
| trebuchet | 40 m       | 350 m      | 150 m           | 150              | 6 m                    |
| cannon    | 10 m       | 600 m      | 120 m           | 120              | 4 m                    |

**Methods**

- `load()` starts the cycle of an idle engine. `siegeEngineReady` fires
  when it can shoot.
- `fire(target, attacker?)` turns a loaded engine to face `target` and
  shoots. `attacker` is a Player or its id, credited in `playerDamage`,
  `npcDamage` and `siegeImpact`.
- `aim(target)` turns the engine to face a point without shooting.
- `canHit(target)` returns an empty string when the engine can reach the
  point, otherwise the reason.
- `setAimGuide(enabled)` shows or hides the operator's red beam to where a
  shot lands; `getAimGuide()` reads it. Off by default.
- `repair()` makes a damaged or wrecked engine whole and idle.
- `setOperator(player | null)` puts a player in its place, or empties it.
- `destroy()` removes the engine after raising `siegeEngineDestroy`. A
  stone already in the air still lands.

`load`, `fire`, `aim` and `canHit` throw once the engine is gone. `load`,
`fire` and `aim` return a `SiegeResult`:

- `accepted`: whether the engine did it.
- `seconds`: for `load`, the whole load; for `fire`, until the stone is let
  go. 0 when nothing happened.
- `reason`: empty when it did, otherwise a phrase that reads after the
  engine's name:
  - `load`: `is already loaded`, `is already being loaded`,
    `is still firing`, `is wrecked`, `has nothing left to throw`.
  - `fire`: `is not loaded`, `is still loading`, `is already firing`, `is wrecked`,
    `was given a target that is not a point`, or `cannot fire: ` followed
    by the `canHit` reason.
  - `aim`: `is firing`, `is wrecked`, `was given a target that is not a point`.
  - `canHit`: `the target is closer than the engine can throw`,
    `the target is further than the engine can throw` (also any flight
    longer than 30 s), `the target is too high for the engine's arc`.

**Events**

| Event                | Arguments                                                             | When                                                                                         |
| -------------------- | --------------------------------------------------------------------- | -------------------------------------------------------------------------------------------- |
| `siegeEngineSpawn`   | `engine`                                                              | right after an engine is built                                                               |
| `siegeEngineDestroy` | `engine`                                                              | while an engine is removed; the handle still resolves                                        |
| `siegeEngineReady`   | `engine`                                                              | a load finished and the engine can fire                                                      |
| `siegeEngineUse`     | `player, engine, "operate" \| "winch" \| "leave" \| "load" \| "fire"` | a player asked something of the engine; return `false` to refuse (`leave` cannot be refused) |
| `siegeEngineDamage`  | `engine, amount, attacker \| null`                                    | a blast took `amount` off an engine with a `maxHealth`                                       |
| `siegeEngineWreck`   | `engine, attacker \| null`                                            | its health ran out                                                                           |
| `siegeFire`          | `engine, attacker \| null, target`                                    | the stone is let go                                                                          |
| `siegeImpact`        | `engine \| null, position, attacker \| null`                          | the stone came down; `engine` is `null` if it was destroyed meanwhile                        |

## The `/siege` command

The default gamemode's `/siege` builds engines 14 m in front of the
caller, facing where they look, and makes them `usable`. Without an id, a
command works the nearest engine within 40 m, preferring one within 200 m
that can do what was asked.

| Command                           | What it does                                                             |
| --------------------------------- | ------------------------------------------------------------------------ |
| `/siege trebuchet\|cannon`        | build one                                                                |
| `/siege load [id]`                | load an idle engine                                                      |
| `/siege fire [distance] [id]`     | fire along the caller's facing, `range` metres when no distance is given |
| `/siege fire at <player id> [id]` | fire at a player                                                         |
| `/siege speed <multiplier> [id]`  | set the crew's pace, 0.1 to 20                                           |
| `/siege health <hit points> [id]` | set `maxHealth`; 0 makes it unharmable                                   |
| `/siege ammo <shots> [id]`        | set `ammo`; -1 for no limit                                              |
| `/siege crew <winches> [id]`      | set `crewRequired`                                                       |
| `/siege repair [id]`              | make it whole again                                                      |
| `/siege guide on\|off [id]`       | show or hide the operator's aim beam                                     |
| `/siege list`                     | the engines in this world, newest first (up to 20)                       |
| `/siege remove <id>`              | remove one                                                               |
| `/siege clear`                    | remove every engine in this world                                        |

## Limits

- Walls do not break. KCD2's castle brushes are not breakable, and no
  explosion shapes are registered.
- The crew places are declared in `tools/generate_siege_catalog.py`, not
  mined: the skeleton has no winch bones, and the pull fragments align by a
  procedural layer that needs a target the mod does not give them. NPC crew
  is a script's to place and animate.
- A player who arrives near the target after a shot was launched, without
  the engine streamed in, does not see that stone.
- A stone that meets nothing within 30 s has left the world the client has
  streamed in. It disappears without an impact, and the server lands the
  shot on its own. `fire` refuses a target whose flight would take longer
  than 30 s as too far.
