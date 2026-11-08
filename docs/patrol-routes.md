# NPC patrol routes

Create patrol routes in World Builder's **Patrols** tab. Each route has an ID
for scripts and a display name for the editor. Use IDs such as
`town.guard-loop` to avoid collisions between world exports.

Aim the camera at the ground and click **New route**. Its first waypoint is
placed at the camera ray hit; when aiming horizontally, the editor probes the
ground five metres ahead. The new route defaults to **Ping pong**.
Click **Add points in world** and click the ground to append waypoints.
Escape finishes placement. Select a numbered waypoint and drag its position
arrows, or edit its coordinates in the panel. Hold **Shift** while translating
to snap to the ground, with any axis or plane handle. Double-click a route
line to insert a waypoint. **Move earlier** and **Move later** change
the order. Delete removes the selected waypoint; deleting the last waypoint
removes the route. **Delete route** removes the whole route.

Choose **Once**, **Loop**, or **Ping pong**, with a default speed, arrival
radius and wait at each waypoint. Individual waypoints can override those
defaults. Ping pong visits each endpoint once before turning back.

## Preview

Choose a **Preview NPC** and click **Preview**. A temporary NPC walks the
route using the same path planner and waypoint progression as scripted
patrols. The green line shows its current planned path; the panel shows its
target waypoint and whether it is moving or waiting. Preview reads the
level's `recast.pak` from the game installation. If it cannot load a mesh or
spawn the NPC, the panel explains why.

**Stop preview**, a route edit, closing World Builder, opening another
project, or leaving the level removes the NPC. A completed once route also
removes it. Preview is local to this client and is never saved or exported.
Preview uses the base navigation mesh. Humanoid preview NPCs unlock and open
local doors on approach, then wait for the swing before crossing. Doors remain
unlocked and open in the local level after preview. Server NPCs still respect
locks, so a deployed route may need its doors unlocked by the server script.
Servers with enabled navigation overlays can also choose a different path.

## Save and deploy

Save the editable `.project.json` to continue later. Export a `.world.json`
and list it in the server's `mod.world_resources` to make its routes available
to scripts. Routes do not spawn or assign NPCs automatically. Blueprints and
**Copy API calls** include selected routes; appending a blueprint gives any
colliding route IDs a numeric suffix.

The updated reader accepts version 1 projects and world exports without
routes. New files use format version 2 and need the updated reader.
Duplicate route IDs across exports or scripted definitions fail loading;
partially loaded export contents are rolled back.

Use **Copy API call** in the Patrols tab to copy the selected route as a
`PatrolRoute.create(...)` call for a server script.

## Server scripting

Existing point-array patrols still work:

```ts
npc.patrol([gate, market], { loop: true, speed: "walk", waitSeconds: 3 });
```

Create a named route in a script, or obtain one from a world export:

```ts
const route = PatrolRoute.create({
    id: "town.guard-loop",
    name: "Town guard patrol",
    mode: "pingPong",
    speed: "walk",
    waitSeconds: 2,
    points: [
        { position: gate, waitSeconds: 5 },
        { position: market, speed: "jog", radius: 0.75 },
    ],
});

npc.patrol(route);
npc.patrol("town.guard-loop", { speed: "jog" });
npc.hold();
```

`PatrolRoute.getById(id)` returns a handle or `null`; `PatrolRoute.all()` lists
definitions. `route.toJSON()` returns an independent copy. `route.update()`
replaces the supplied fields atomically; its ID cannot change. `destroy()`
removes the definition. Handles to removed definitions remain invalid even
if a new route uses the same ID. Script-created routes remain registered
until destroyed or the server shuts down.

A patrol copies its definition when started. Updating or deleting that
definition leaves the running patrol alone; call `npc.patrol()` again to
restart with the edited route. Waypoint overrides take precedence over call
options, which take precedence over route defaults. `mode` and the legacy
`loop` option are mutually exclusive. `pathfinding` retains its existing
`auto`, `game`, and `server` behavior.

`npc.patrolState` is `null` when no patrol is active. Otherwise it gives
`routeId`, `waypointIndex`, `lap`, `direction` (1 or -1), and `phase`
(`moving` or `waiting`). Indices and laps start at zero; anonymous point-array
patrols have a null `routeId`.

`patrolWaypoint(actor, info)` reports a waypoint outcome: `reached`, `blocked`
or `failed`. `patrolFinished(actor, info)` reports `completed`, `failed` or
`cancelled`. Both include the patrol state. These events supplement the
existing `npcIntentDone`; intermediate path corners do not count as
waypoints. An unreachable waypoint is skipped after a backoff; a full run of
failed legs ends the patrol. Removing an NPC uses the existing `npcDestroy`
event rather than emitting an event with a dead actor handle.

```ts
const validation = Navigation.validatePatrol(route, { actor: npc });
if (validation.available && validation.complete) npc.patrol(route);
```

Validation checks every leg, including loop closure and both directions of a
ping-pong route. Each leg reports `fromIndex`, `toIndex`, `complete`, and
`path`. A partial path is incomplete. Without a mesh, `available` and
`complete` are false. Supplying an actor selects its door policy: unlocked
doors in the actor's virtual world for humanoid NPCs, no doorways for horses
or animal classes. It does not move
the actor or check the approach from its current position to waypoint zero.
The route format and progression are independent of actor type; actual
animal spawning and horse patrol verbs belong to the animal-navigation PR.

World exports load after scripts start. Check readiness before reading them:

```ts
function onWorldReady() {
    const routes = WorldResource.find("town")?.patrolRoutes ?? [];
    console.log(routes.map(route => route.id));
}
if (WorldResource.ready) onWorldReady();
else Events.once("worldResourcesReady", onWorldReady);
```

The sample gamemode provides `/patrol list`, `/patrol start <route-id>`,
`/patrol validate <route-id>`, `/patrol status`, and `/patrol stop`. Start,
status and stop act on the nearby NPC. `/npc patrol` still runs its original
two-point demonstration.

## Crossing doors

Humanoid NPCs automatically open unlocked doors on server-planned `moveTo()`
and patrol routes. This works with both named routes and existing point arrays.
No extra script call is needed. The default `pathfinding: "auto"` uses this
behavior when the server has a navigation mesh; `"server"` always requires one.
`"game"` uses direct steering first and can fall back to a server route when blocked.

The server changes the door in the NPC's virtual world, so nearby players see
its native opening animation. NPCs pause on approach, then continue toward the
same waypoint. They never unlock a door. A door locked after planning blocks
the current leg. Horses and animals cannot open doors and avoid doorways.
Doors stay open after crossing.

Preview uses the same route progression, unlocking and opening local doors
through the existing native door service. It waits for that door's animation to finish.
Server navigation allows a 1.5-second pause for replication and the swing before
continuing the approach; it does not receive the native animation clock.
The door animates, but the NPC does not play a hand-opening interaction.
