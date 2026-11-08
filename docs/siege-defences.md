# Siege defences: ladders and stone piles

Alongside the engines in [siege-engines.md](siege-engines.md), a server
script can storm and defend a wall with the game's own siege kit:

- ladders the attackers climb and the defenders push off the wall
- piles of hurling stones the defenders heave over the battlements

Everything a player sees and does here is the game's own: the climb, the
halberd push, the lift and the heave, the stone's flight. The server keeps the
state and decides who may do what.

```ts
const ladder = SiegeLadder.spawn("siege", footOfWall, facingTheWall);
const pile = StonePile.spawn(onTheWalk, facingOutward);
pile.stones = 20;
Events.on("siegeLadderFall", (ladder, pusher) => { /* ... */ });
Events.on("stoneImpact", (pile, position, attacker) => { /* ... */ });
```

The default gamemode's `/siege` command places both; see
[The `/siege` command](#the-siege-command).

## Ladders

A `SiegeLadder` is the game's `Ladder` entity, with the properties of a
battle-ladder prefab: `Prefabs/Ladder/battleLadder_10000.xml` for `siege`
and `battleLadder_350.xml` for `short`. `tall` and `low` are the 5.75 m and
2.25 m village ladders, set up the same way.
`tools/generate_siege_defence_catalog.py` mines them into
`code/shared/generated/siege_defence_catalog.cpp`.

| Kind    | Climb  | Push tag |
| ------- | ------ | -------- |
| `siege` | 9.75 m | `10`     |
| `tall`  | 5.5 m  | `575`    |
| `short` | 3.25 m | `350`    |
| `low`   | 2 m    | `225`    |

The ladder stands at its foot (`position`), leaning 12.15 degrees towards
its forward, which is the wall. It is never scaled, because a scaled `Ladder`
hands its scale to whoever climbs it. Pick the kind whose climb reaches the
walk. `bTopOnPalisade` is set, so the game's own step off at the top carries
the climber over the palisade.

Each client fits the climb to the wall the ladder stands against when its
player first comes within 40 m. The climb ends at the highest rung where
nothing is over the climber and the step off lands on open walk. That means
a level floor as wide and as deep as the step off carries the body, open sky
above it, a clear way across and the wall still under its rim. A roof partway
up a wall, or the narrow edge of one, is not a walk. A
ladder taller than its wall therefore stops at the wall's top. One leant
against a house cannot carry anyone over the eaves or through a window. If
there is nowhere to step off, the top is blocked and the climb only goes
back down. The climb is not offered until it is fitted.

Each client spawns the ladder with a GUID derived from its network id. A
remote player's replicated climb finds the ladder by that GUID, as it would
a ladder placed in the level.

**Push.** A defender on the walk at a standing ladder's top is offered the
game's `@push_ladder` prompt, held on the use key. While it is offered, the
ladder's climb is not. A push plays `GuardHlbrdPushAway_Player` with the
ladder's tag on the pusher. The ladder stands through the swing until the
blade meets it, 2.65 s in. It then falls away from the wall and lands 4.5 s
in, when `battleLadder.xml`'s `player_pushLadder` plays its crash
(`q_M11_gate_crash_open2` with the `q_conquest_gatecrush` dust). Anyone on
the ladder as it starts to fall is thrown off by
`C_ActorStateExpansion::DropFromLadder` and takes the game's own fall.

**Raise.** At a fallen ladder's foot, the use key raises it. The raiser plays
`LayLadder`/`layFromIdle`, and the ladder swings back up over 5.7 s.

Both are requests the server checks for reach (`SiegeLadderUse`), and each
raises `siegeLadderUse` first, which a handler may refuse. The fall and the
raise are never streamed: the state and the server clock it began on are
replicated, and every client tilts its ladder on the same curve.

## Stone piles

A `StonePile` is the game's battlement pile,
`Prefabs/hurlingStone/hurlingStonePile_onesided_walls_left_a.xml`. Each
client spawns these parts:

- nine `GeomEntity` stones
- two `StoneThrowingNode`s, a short path running back from the stand point
- an entity for the pick point
- the `StoneThrowingPile` itself

They are linked the way the prefab links them (`path`, `pickPoint`, `stone`,
through `IEntity::AddEntityLink`). The pile then rebuilds itself from those
links through its own reset.

From then on it is the game's `C_StoneThrowing` minigame:

- the pile's prompt (`@ui_start_stone_throwing`)
- the grab, after which the thrower holds the stone
- the throw, on the use key (the game's `throw` action), never on its own

The pile is a `hole` pile, not the prefab's `walls`. A `walls` pile keeps the
minigame on its non-interactive action map and throws as soon as the stone
is in the hands (`RefreshInteractionAction`, `ShouldAutoThrowAfterGrab`).
While the stone is held, the game keeps the thrower on the path, facing back
along it, so a path running back from the stand point has them face the way
the pile throws.

The throw is a 300 kg rigid stone, pushed once along the thrower's facing by
`wh_pl_StoneThrowingLaunchStrength`, pitched by
`wh_pl_StoneThrowingLaunchXAngle`. A positive angle pitches it down: the
game's 85 degrees drives it down at the thrower's feet. The client sets -40
degrees and 3300, which arcs the stone up and about 11 m away.

The prompt is the game's. `C_StoneThrowingPile::CanUse` offers it within
0.3-1.8 m of the pile, to someone standing inside a cone about the pile's
own +Y. The pile entity is turned so that cone opens behind the stand point:
walk up to the heap facing the way it throws and aim at it. The invisible
trigger is larger than the prefab's, so it is easy to aim at on open ground.

`position` is where the thrower stands and `rotation`'s forward is the way
they throw.

**The count.** `stones` is the server's. A client shows at most nine, and
none when the pile is not `usable`, so the game only offers the prompt while
the server allows a throw. The game takes a stone off the heap itself at
every grab. The count is written back only when it disagrees with the grabs
the client saw, and never mid-grab.

**A throw on the network.** The thrower's client captures the grab, throw,
departure and stone release.

The local player's grab is reported (`StonePileUse`). The server takes one
off the count and relays it to everyone near (`StonePileUsed`). Each of them
runs the same `StartWithPile` on the thrower's body, which then holds the
stone. The throw key is reported and relayed the same way (`Throw`), and each
client runs `RequestThrow` on the body. The game plays the grab, the throw
and the stone exactly as it did for the thrower, along the body's facing.

Replaying a throw on another player's body does not take over the local
player's input or camera.

**What a stone hurts.** The game's stone throwing deals no damage. The
thrower's client follows its own stone and traces it frame by frame. The
first body or surface it meets, or where it comes to rest, is reported
(`StoneImpactReport`). The server accepts one report per grab, within 15 s
and 30 m of the pile. It hurts NPCs within `damageRadius` there, and tells
the players near it to take their share (`StoneImpact`) through the game's
own damage, as a siege engine's blast is taken. The thrower is never among
them.

## Scripting API

Server-side, in `scripting-api/generated/server-api.d.ts`. Both are entity
handles: `position`, `rotation`, `virtualWorld` and the `state` bag come from
the framework's `Entity`.

### `SiegeLadder`

| Call                                                          | What it does                                                                                                                                                                            |
| ------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `SiegeLadder.spawn(kind, position, rotation?, virtualWorld?)` | stands a `siege`, `tall`, `short` or `low` ladder with its foot at `position`; forward of `rotation` (a Quaternion or Euler degrees) is the way to the wall. Throws for any other kind. |
| `SiegeLadder.describe(kind)`                                  | `{ kind, model, height, lean }` for a kind, or `null`: what a placement preview needs to show it as it will stand                                                                       |
| `SiegeLadder.all(virtualWorld?)`                              | every ladder, or those in one world                                                                                                                                                     |
| `SiegeLadder.getById(id)`                                     | the ladder with that network id, or `null`                                                                                                                                              |
| `SiegeLadder.destroyAll(virtualWorld?)`                       | removes them, raising `siegeLadderDestroy` for each; returns how many                                                                                                                   |

| Property    | Meaning                                                                                                                                  |
| ----------- | ---------------------------------------------------------------------------------------------------------------------------------------- |
| `kind`      | `siege`, `tall`, `short` or `low`                                                                                                        |
| `pose`      | `standing`, `pushed` (the swing, then the fall), `down`, `raising` or `broken`. Named `pose` because every entity already has a `state`. |
| `height`    | how far up the rungs go, in metres (read-only)                                                                                           |
| `climbable` | whether the game's climb is offered: it stands and nobody has started to push it (read-only)                                             |
| `usable`    | whether players are offered the push and the raise; on by default. The climb is offered whenever it stands.                              |
| `pusher`    | the player who last pushed it, or null                                                                                                   |

- `push(player?)` pushes a standing ladder off its wall. `player` plays the
  halberd push and is credited in `siegeLadderFall`.
- `raise(player?)` lays a fallen ladder back against its wall; `player`
  plays the lift.
- `break()` leaves it lying in pieces, throwing off anyone on it; nothing
  raises it until `repair()`.
- `repair()` stands a fallen or broken ladder back against its wall at once.
- `destroy()` removes it after raising `siegeLadderDestroy`.

`push` and `raise` return a `SiegeResult`, as an engine's verbs do. `reason`
is `is not standing against a wall` for a push, and `is broken` or
`is not lying on the ground` for a raise; `seconds` is how long the fall or
the raise takes.

### `StonePile`

| Call                                                  | What it does                                                                                                     |
| ----------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------- |
| `StonePile.spawn(position, rotation?, virtualWorld?)` | places a battlement pile where the thrower stands; forward of `rotation` is the way the stones go, over the wall |
| `StonePile.all(virtualWorld?)`                        | every pile, or those in one world                                                                                |
| `StonePile.getById(id)`                               | the pile with that network id, or `null`                                                                         |
| `StonePile.destroyAll(virtualWorld?)`                 | removes them, raising `stonePileDestroy` for each; returns how many                                              |

| Property       | Meaning                                                                                                                                 |
| -------------- | --------------------------------------------------------------------------------------------------------------------------------------- |
| `stones`       | stones left; each throw takes one, and players see at most nine. -1, the default, never runs out.                                       |
| `usable`       | whether players are offered the game's prompt at the pile; on by default                                                                |
| `damage`       | health taken where a stone comes down, 0-1000; 60 by default, and a player has 100. A quarter of it reaches the edge of `damageRadius`. |
| `damageRadius` | how far from that point anyone is hurt, 0-10 m; 1.5 by default                                                                          |

- `destroy()` removes the pile after raising `stonePileDestroy`. A stone
  already thrown still lands.

### Events

| Event                | Arguments                           | When                                                                                                                                              |
| -------------------- | ----------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------- |
| `siegeLadderSpawn`   | `ladder`                            | right after a ladder is placed                                                                                                                    |
| `siegeLadderDestroy` | `ladder`                            | while a ladder is removed; the handle still resolves                                                                                              |
| `siegeLadderUse`     | `player, ladder, "push" \| "raise"` | a player asked from the prompt at its top or foot; return `false` to refuse                                                                       |
| `siegeLadderFall`    | `ladder, pusher \| null`            | a pushed ladder hit the ground; whoever was on it has already fallen                                                                              |
| `siegeLadderRaise`   | `ladder`                            | a raised ladder leans on its wall again                                                                                                           |
| `stonePileSpawn`     | `pile`                              | right after a pile is placed                                                                                                                      |
| `stonePileDestroy`   | `pile`                              | while a pile is removed; the handle still resolves                                                                                                |
| `stonePileThrow`     | `player, pile`                      | a player took a stone and threw it. It cannot be refused: make the pile not `usable` to stop the next one.                                        |
| `stoneImpact`        | `pile \| null, position, attacker`  | where a thrown stone first met something, within 30 m of its pile. Nearby players have been told to take their share, and NPCs have taken theirs. |

## The `/siege` command

- `/siege ladder [siege|tall|short|low]` opens the placement preview
  ([`PropPlacer`](prop-placer.md)) with the ladder's own mesh, leaning as it
  will stand and facing away from you; aim at the foot of the wall and click.
  Right-click cancels.
- `/siege ladder push|raise|break|repair [id]` acts on the nearest ladder
  within 25 m, or the one with that id, with you as the pusher or raiser.
- `/siege stones [count]` puts a pile 1.2 m in front of you, throwing the way you
  face; `count` defaults to -1, no limit.
- `/siege clear` removes the engines, the ladders and the piles in your
  world.

## Not done

KCD2 has no breakable fences or palisades. No level layer places a
`BreakableObject` or `DestroyableObject`, and walls cannot be broken (see
[siege-engines.md](siege-engines.md)).
