# Server-spawned NPCs, as built

The NPC system, its components, scripting API and known limits.

Two repositories are involved, deliberately:

- **Framework** — `DelegationManager`, the election of which client simulates
  which server-owned entity. Reusable: nothing in it knows what an NPC is.
- **KCDC** — the NPC feature built on it.


## 1. The framework half: delegated simulation

`code/framework/src/networking/replication/delegation.{h,cpp}`

An entity opts in by overriding one method:

```cpp
const DelegationPolicy *GetDelegationPolicy() const override { return &kPolicy; }
```

and the manager does the rest: every 500 ms it ranks the clients near each
delegated entity and makes the nearest one its **owner**. That is the whole
trick — the elected client becomes the owner of an entity the replication layer
already knows how to own, so the feature inherits the entire authority model
rather than growing a parallel one: updates travel upstream and are relayed,
server overrides arrive through `ForceState`, updates from a revoked simulator
are dropped by the state epoch, and pose ordering resets on the handover.

| Piece                                 | What it is                                                        |
| ------------------------------------- | ----------------------------------------------------------------- |
| `DelegationPolicy`                    | acquire/release ranges, weight, handover grace, world rule        |
| `DelegationCandidate`                 | one client: where its avatar is, its world, its load and budget   |
| `DelegationManager::Elect(...)`       | **the rule, as a pure function.** No manager, no entity, no clock |
| `DelegationManager::Update()`         | the pass: collect candidates, charge loads, elect, apply          |
| `Pin` / `Unpin`                       | force one client to run it, whatever the distances say            |
| `SetClientCapacity`                   | per-client simulation budget, global or per peer                  |
| `AddSimulatorChangedHandler`          | handle-based subscription, so two features can both use it        |
| `NetworkEntity::CanReleaseSimulation` | an entity mid-action defers a handover, up to `graceMs`           |
| `NetworkEntity::OnSimulatorChanged`   | per-entity hook, raised inside the handover                       |
| `NetworkEntity::IsSimulating()`       | "this client is the elected simulator"                            |

Two asymmetries in `Elect` are the whole reason it does not flap, and they are
what the tests pin down:

1. The incumbent is measured against `releaseRange` while a challenger must be
   inside `acquireRange`.
2. The incumbent's distance is discounted by `kStickyDiscount` (0.72, the
   interest grid's own constant), so a challenger has to be ~15% nearer to take
   an entity off it.

An incumbent is never dropped for being over budget: a capacity lowered at
runtime stops new work arriving rather than stuttering what is already running.
With no candidate at all the entity goes **dormant** — server-owned, still
replicated, simulated by nobody. That is a state, not a failure.

Tests: `code/tests/modules/delegation_ut.h`, 16 cases over the rule —
hysteresis, stickiness, budgets and weights, virtual worlds, ground-plane
measurement, a disconnected incumbent. `FrameworkTests` runs 430 cases green.

Wiring: `ReplicationManager` owns the manager (`Delegation()`), `NetworkPeer::
Update` runs a pass after the interest rebuild and before the state-bag flush,
`DestroyEntity` drops its bookkeeping, and `OnClosedConnection` hands back what
a departing peer was simulating before its viewer is destroyed and re-elects
after.


## 2. The KCDC half: NPCs

### Shared (`code/shared/`)

| File                        | What                                                                                     |
| --------------------------- | ---------------------------------------------------------------------------------------- |
| `features/npc/npc_types.h`  | `NpcFlags`, `NpcIntentKind`, `NpcSpeed`, `NpcLocomotion`, `NpcIntentStatus`, `NpcIntent` |
| `features/npc/npc_entity.h` | `NpcEntity`: the replicated entity, its policy, its channels                             |
| `features/npc/npc_rpc.h`    | `NpcInteract`, `NpcDamage` (client→server), `NpcAction` (server→clients)                 |
| `game/npc_roles.{h,cpp}`    | ten named roles, each a real soul out of the game's tables                               |

`NpcEntity`'s channel layout is the design's load-bearing detail:

- **Construction snapshot** — `soulGuid`, `levelGuid`, `actorClass`,
  `spawnEpoch`. Identity, so a late joiner builds the same body.
- **Server-authored block** — name, appearance, equipment, faction, flags,
  locomotion mode, health ledger, intent, revision, simulator id. It rides
  **both** the field channel (for observers) and `SerializeForcedState` (for the
  simulator, whom the field channel is withheld from).
- **Simulator report** — `intentStatus`, `statusRevision`, `busy`. Measurements,
  never written by the server.
- **Transform** — quantized pose plus the Mannequin tags beside it, fenced by
  `poseEpoch`.

The rule that makes the echo safe: **every server write is followed by
`ForceState()`**, which bumps the state epoch, so the simulator's in-flight echo
of the previous value is dropped instead of reverting the write. `Npcs::` is the
only place that writes, and the only place that has to remember.

### Server (`code/server/src/features/npc/`)

- `npc_verbs.{h,cpp}` — `Npcs`: spawn, adopt, destroy, identity, ledger,
  intents, one-shots, pinning, lookup. Also the RPC handlers and the per-tick
  `Update`, which feeds each order what the entity reports and publishes what
  the order wants simulated.
- `npc_order.{h,cpp}` — `NpcOrder` and `NpcOrders`: each NPC's standing order
  and patrol, and the queue its route plans wait in. It reads the simulator's
  report, decides and raises `npcIntentDone` once per order, steps patrols,
  and **dead-reckons a dormant NPC along its route** so a patrol nobody is
  watching still advances.
- `npc_navigation.{h,cpp}` — `NpcNavigation`: one move's goal, its pathfinding
  mode and the corners the server planned for it. See
  [navigation](navigation.md).
- `npc_scripting.{h,cpp}` — the `Npc` handle and its events.

### Client (`code/client/src/features/npc/`)

- `npc.{h,cpp}` — the replica, in one of two roles.
- `npc_service.{h,cpp}` — the tick, the one-shot inbox, the interaction key.

**Observer role**: the body is a puppet. Pose and locomotion come off the wire
through the same override a remote player's body is drawn with, interpolated
through the same snapshot buffer.

**Simulator role**: runs the intent and reports. Two locomotion modes:

- `Kinematic` (default) — the simulator advances the pose itself, grounds it
  with the engine's own `ResolveGround`, publishes it, and animates it through
  the locomotion override. Predictable, and every part of it is already proven
  by the bodies this mod draws.
- `Native` — hands a move target and a desired speed to
  `C_ActorMovementController::RequestMovement` and lets the actor walk there.
  Real footfalls, real turns, real gait tags read back off the body, and no
  knowledge of what is in the way.

### Shared code

- `client/src/common/actor_dressing.{h,cpp}` — the packed-item-class
  format and the dressing itself, used by `Human`, `Npc` and `Horse`. Armor is
  never equipped directly: the soul would take it back off on its next outfit
  application. The list becomes a clothing preset in the game's own database
  and is pushed onto the soul's preset stack, so the engine keeps it on. An
  empty list on an NPC means "its own clothes". `Human::ApplyOutfit` is a few
  lines of its own bookkeeping over it.
- `Social::EmoteService::QueueEmoteOnActor` — an unmanaged one-shot emote, for a
  body nothing is tracking. The managed path and this one share one action
  builder.


## 3. The scripting API

Generated contract: `scripting-api/generated/server-api.d.ts`. The default
gamemode's `/npc` command (`resources/kcdc-gamemode/src/server/commands/npc.ts`)
is its first consumer and type-checks against it.

```ts
const guard = Npc.create({ soul: "guard", position, rotation, name: "Sir Hanush's guard", nametag: true });
guard.patrol([gate, courtyard, stables], { loop: true, speed: "walk", waitSeconds: 8 });

Events.on("npcIntentDone", (npc, status) => { if (status === "blocked") npc.hold(); });
Events.on("npcInteract", (npc, player) => npc.say(`Well met, ${player.nickname}.`));
```

Statics: `create`, `adopt`, `all`, `getById`, `removeAll`, `roles`.
Properties: `soul`, `actorClass`, `levelGuid`, `name`, `faction`, `health`,
`maxHealth`, `alive`, `invulnerable`, `frozen`, `interactable`, `nametag`,
`lootable`, `locomotion`, `intent`, `status`, `appearance`, `simulator`.
Methods: `moveTo`, `patrol`, `follow`, `flee`, `lookAt`, `hold`, `teleport`,
`playAnimation`, `say`, `wear`, `setOutfit`, `setAppearance`, `damage`, `kill`,
`revive`, `pin`, `remove`, and the nametag family `Player` has --
`setNametagVisible`, `setNametagHealthVisible`, `setNametagText`,
`setNametagColor` with their `is`/`get` readers.
Events: `npcSpawn`, `npcDestroy`, `npcIntentDone`, `npcDamage`, `npcDeath`,
`npcRevive`, `npcInteract`, `npcSimulatorChange`.


## 4. Known limits

These are deliberate and documented rather than overlooked.

1. **Neither locomotion mode paths around anything.** Kinematic movement
   grounds itself and stops when it stops making progress (`Blocked` after
   three seconds); native movement hands the target to the engine's movement
   controller, which steers straight at it. Routes come from the server's
   navigation mesh, which hands either mode one corner at a time -- see
   [navigation](navigation.md). Neither mode uses the engine's own navigation
   service.
2. **Kinematic bodies hold one gait.** Nothing drives the MoveSpeed tag group in
   that mode, so the blend coordinate comes from the published velocity while
   the fragment stays whatever the body holds. Native mode reads the engine's
   own tags back and has real gaits.
3. **A female appearance needs a female body.** An NPC's soul is chosen by the
   server; moving a shared soul's archetype would change every other body
   spawned from it, so a mismatch is refused and logged rather than forced.
4. **Combat is damage, not fighting.** `NpcDamage` carries an attacker's own
   resolved hit and the server arbitrates the ledger; nothing yet makes an NPC
   swing back. That is M6 in the plan and wants the brain.
5. **Interaction is a key and an event.** `npcInteract` fires with the NPC and
   the player, distance-checked on the server. What a gamemode does with it —
   dialogue, a shop — is the gamemode's.
6. **Nametags** ride the framework's `NametagState`, carried in the
   server-authored block so the simulator sees a change through the forced
   state. The `ShowNametag` flag decides whether a tag is drawn at all and is
   the switch `setNametagVisible` turns; the state decides the health bar, the
   colour and any text standing in for `name`. The renderer is the one remote
   players are drawn by.
