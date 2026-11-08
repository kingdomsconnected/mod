# Level containers

The level's own chests, boxes and barrels are replicated containers the
server addresses, locks and gates, the same way it handles the level's doors.
A tester wanted a key item to open both a house door and the chest inside it.
Doors already had `Door.locked`, `lockedByServer` and `doorInteract`. The
containers the level placed had nothing: `Stash` only covered containers a
script spawned.

## One catalog for level and spawned containers

The server keeps one catalog, keyed by the EntityGuid every client finds a
container under:

- **A level container** has the level's own GUID. Its entry comes from the
  compiled catalog below.
- **A spawned container** has `0xF5A5` in its high bits and its `stashId`
  below them (`StashEntity::SpawnedGuid`, the GUID the client spawns the
  native entity with). `Stash.spawn` appends an entry at runtime, and
  `destroy` or `destroyAll` removes it. The entry records:
  - the spawn position;
  - its forward axis as the use axis;
  - the default 2.5 m reach;
  - no key, lockpickable, starts unlocked;
  - the name `spawned_stash_<stashId>`;
  - the virtual world it was spawned in, the only one it lives in.

So `find`, `nearest`, `name`, `useDirection`, the gate's reach check and
every lock property treat both kinds the same. `stash.guid` is the catalog
key for either kind; `levelGuid` stays empty for a spawned one.

## What a script gets

- `Stash.find(guid, virtualWorld?)` finds a container by its EntityGuid,
  level or spawned. `Stash.nearest(position, maxDistance?, virtualWorld?)`
  finds the one nearest a point. `nearest` answers at once from the
  catalog, without asking a client.
- `stash.levelGuid` and `stash.name` identify the container. Both are empty
  for one a script spawned.
- `stash.locked` can be read and set. A lock set from a script marks the
  container `lockedByServer`, and the generated home or shop key does not
  open it.
- `stash.lockpickable` can be read and set.
- `stash.keyItem` can be read and set. Reading it gives the level's key until
  a script assigns a class. From then on, every client asks for the assigned
  class instead, so a player holding it is offered "unlock and open".
- `stashInteract: [player, stash, action]` fires with `action` set to `open`,
  `unlock` or `lockpick`. It is asked before anything happens, and returning
  false refuses the action.
- `stash.lockpickable = false` removes the lockpick prompt on every client:
  the prompt is registered disabled and hidden.
- `stash.useDirection` gives the horizontal direction the game's use check
  wants a body to face the container from. Stand at
  `position - useDirection * distance` to be offered it.
- `stash.destroy()` and `Stash.destroyAll()` leave the level's containers
  alone.

The gamemode's `/stash near|lock|unlock|pick|key|goto` commands exercise
all of this. `goto` stands the caller in front of the nearest locked level
container, the nearest unlocked one, or one named by GUID.

## What else changed on the wire

- `StashEntity` carries `levelGuid`, `contentsNetworkId`, `locked`,
  `lockedByServer`, `lockpickable` and `keyItemClass`.
- The lease (`lockOwnerNetworkId`) no longer drives the native lock. Before
  this change, a stash another player had open offered everyone else the
  lockpick. The gate now refuses a second opener instead.
- A client applies the lock on edges, not every frame. A key turned here
  unlocks natively a round trip before the server agrees, and forcing the
  replica's value would lock it again in between.
- Eight containers on kutnohorsko open a linked master's inventory through a
  `masterStash` link (`C_Stash::GetMasterInventoryWuid`). Each is its own
  replica with its own lock, but its contents and lease are the master's.
  The client routes their open and close to the master's replica.

## Level data

`tools/generate_stash_catalog.py` mines every `Stash` placement into
`code/shared/generated/stash_catalog.cpp`:

| level       | containers | start locked | own key | not lockpickable | linked |
| ----------- | ---------: | -----------: | ------: | ---------------: | -----: |
| kutnohorsko | 2,729      | 1,162        | 31      | 19               | 8      |
| trosecko    | 918        | 373          | 17      | 18               | 0      |
| klaster     | 206        | 69           | 3       | 0                | 0      |

Another 25 `masterStash` links point at nothing, and the engine ignores them,
so the catalog does too.

All of a level's containers exist from boot in the global world, and are
built on first use in any other virtual world. They stream at 50 m: the
densest 50 m of Kuttenberg holds 144 of them, and 100 m would hold 412. The
type's interest budget is 192.

The Stash type is held to each container's own range
(`ReplicationManager::SetInterestUsesEntityRange`). By default the interest
grid streams an entity at the larger of its range and the viewer's, which is
250 m for a player. At that range the same spot holds 1,043 containers, and
the budget would cut them by rank, so containers at the edge would stream in
and out on every step. With the entity's range authoritative, a level
container streams in at 50 m and out past 75 m (the 25 m stream-out margin),
and a spawned one at 200 m.

Those types are also filed in a grid of their own, which is queried only as
far as their longest range. Every player's main query reaches 425 m, because
a gate's 400 m range plus the margin sets it, but it no longer passes over
the 2,729 chests. Measured with the real Kuttenberg positions, 64 players
among the chests, and one rebuild of every player's set (Debug build):

| setup                               | per rebuild | held per player | stashes per player |
| ----------------------------------- | ----------: | --------------: | -----------------: |
| no level stashes                    | 7.0 ms      | 172             | 0                  |
| stashes, viewer range widens theirs | 19.2 ms     | 296             | 124                |
| stashes, own range, one grid        | 15.9 ms     | 216             | 44                 |
| stashes, own range, own grid        | 10.3 ms     | 216             | 44                 |

The remaining cost over no stashes at all is the 44 containers each player
actually holds.

## Verified in game with two clients

The Suchdol chest `01c63302c59259b5` was authored locked, and both clients
were offered only the lockpick.

Confirmed:

- A server `unlock` changed the prompt to open on both screens, and a server
  `lock` changed it back.
- An open was held, granted and replayed into the transfer screen.
- A second player's open while the chest was held was refused with no
  screen.
- A script-assigned key (an apple) raised "unlock and open". The unlock was
  reported and the transfer screen opened.
- A lockpick was granted and the minigame opened.
- A `stashInteract` handler returning false refused one player's open, while
  another player's open on the same chest was granted.

The client records a lockpick grant before replaying it. The server accepts
an open only against an open grant, which a reported unlock produces.
A successful pick has not been validated with this ordering; the test
character's thievery was too low for the lock.

## Costs

- **Streaming.** Each client holds only the containers within 50 m, or
  200 m for spawned ones, and never more than 192. The server re-files all
  of them in the interest grid on each rebuild, every 100 ms, and ranks only
  those in range.
- **Binding.** Each client binds every streamed replica, level or spawned,
  by looking up its GUID in the entity system's hash every tick. A container
  the engine streamed out or removed is noticed on the next tick, and a
  spawned one is built again.
- **Contents.** A client fetches a container's contents only once its own
  body is within 10 m. The prompt's "empty" hint is right by the time the
  player reaches the chest, and a town full of streamed-in chests does not
  each ask the server.
- **Hook look-ups.** The hooks search a flat list of the containers bound
  this tick, rebuilt without allocating. `AddLuaAction` only records the
  prompt kind for prompts on a replicated container, although every prompt
  in the game passes through it.
- **Request rate.** The server answers at most one request per connection
  every 100 ms. Anything faster is refused before it reaches
  `stashInteract`.

## Not covered

- `StashCorpse`, `Nest`, `CartStash` and `DestroStash` have their own
  scripts and prompts.
- The tutorial chest relocks itself in Lua on close
  (`Properties.Script.bTutorial`), and the server does not see that happen.
