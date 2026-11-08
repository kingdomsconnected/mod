# Chest contents

Every container holds an in-memory inventory the server keeps for it. A
spawned container, a level-placed one and a virtual stash all start empty;
the server never rolls the game's loot. Nothing survives a restart: a script
that wants contents back saves them from the events and restores them.

## Methods

```ts
chest.getInventory(): { revision: number; items: InventoryRow[] } | null;
chest.addItem({ item, amount, metadata?, revision? }): InventoryResult;
chest.removeItem({ units, revision? }): InventoryResult;
chest.setInventory({ items, revision? }): InventoryResult; // at most 128 rows
chest.itemCount;   // rows held
chest.isOpen;      // a player holds its view; physical chests animate the lid
chest.holderId;    // that player's network id, or 0
chest.isVirtual;   // stock with no world chest
chest.open(player): boolean; // virtual stock only; see Virtual stashes
```

For physical containers, every client knows whether the stock holds anything,
so the game's own prompt tells an empty chest from a full one. Only the holder
receives the rows; virtual stock streams only to its holder while open.

The request and result shapes are the ones `Inventory.add`, `remove` and
`set` take for players (see [Inventory scripting API](inventory-scripting-api.md)).
A linked child -- a level container that opens
another one's contents -- reads and changes its master's inventory.

## Virtual stashes

`Stash.createVirtual(virtualWorld?)` creates stock without spawning a chest.
Call `stash.open(player)` from server code to request the native stash screen,
with both depositing and withdrawing available. Scripts choose their own
trigger. The default gamemode's `/stash virtual` command is a usage example;
it reopens the caller's same stash, moving it into their current world with
`setVirtualWorld`, until they disconnect, the resource stops or `/stash clear`
destroys it.

```ts
const storage = Stash.createVirtual(player.virtualWorld);
storage.addItem({ item: "bread", amount: 5 });
storage.open(player); // returns whether the server accepted the request
// Keep the handle and call open(player) again to reopen its contents.
// storage.destroy() discards the stock, including anything inside.
```

The request goes through `stashInteract(player, stash, "open")` and grants
exclusive access before showing any rows. A physical stash, an unavailable
player (including one whose first inventory projection has not been
acknowledged), a locked or busy stash, a world mismatch or a script refusal
returns false. A player who already holds the stash is sent the screen again
without asking `stashInteract` a second time. True means
the client was asked to open; if its screen or inventory cannot become ready,
it releases the view. `stashOpen` records the server's grant for a virtual
stash. Closing, death, changing world, disconnecting and an expired view
release access; walking around does not. The stock survives closing the UI.
Opening another virtual stash releases the player's previous container view.

Virtual stock supports the same inventory methods, row limit and events.
The optional world must be a uint32 number; invalid arguments throw before
creating stock. Omitting it, or passing `undefined`, selects the global world.
`Stash.all()` and `Stash.getById()` include it. `guid`, `levelGuid` and `name`
are empty; `find()` and `nearest()` address physical containers. Position and
rotation do not affect virtual access. Contents live in server memory until
destroyed or restarted; use a script-owned storage key when saving virtual
stock, since it has no world GUID. `destroyAll()` includes virtual stashes.

Client and server must be updated together: virtual construction adds a field
and virtual opening uses its own RPC.

## Events

```ts
Events.on("stashSpawn", (stash) => {});
Events.on("stashInteract", (player, stash, action) => {}); // open, unlock, lockpick; return false to refuse
Events.on("stashOpen", (stash, player) => {});
Events.on("stashClose", (stash, player, reason) => {}); // closed, lostAccess, timeout, disconnected, destroyed
Events.on("stashInventoryChanged", (stash, player, change) => {});
Events.on("stashDestroy", (stash) => {});
```

- `stashInteract` is asked before access is granted; for physical
  containers it is described in [Level containers](level-containers.md).
- `stashInventoryChanged` carries the core `InventoryChange`. `player` moved
  the units (`deposit`, `withdraw`, or `drop` for `player.dropInventory`, and
  their own inventory raises `playerInventoryChanged` too), or is null for a
  script change (`add`, `remove`, `set`) and for `destroy`, when a spawned
  container or a virtual stash went with something in it.
- A container being destroyed raises its close, then the removal of its
  contents, then `stashDestroy`, all while it can still be read.

## Moving items

One player views a container's stock at a time. The server grants an open
through `stashInteract` and the game's own rules, then sends the holder the
rows (`StashInventorySnapshot`). Each move is one request
(`StashInventoryTransfer`: the rows, direction, and both revisions), all or
nothing, answered with a fresh snapshot while the sender still holds an
authorized view. Requests from non-holders receive a close response without
rows, metadata or revisions. Every snapshot rechecks the player's world,
ability to act and, for physical containers, reach at the chest actually
opened. A physical prompt may receive rows before opening only with that
connection's unexpired open grant; this never authorizes a transfer. The
holder loses the container when they close it, walk out of a physical chest's
reach, die, change world or leave; their game renews the view while its screen is up,
so only a client that stopped answering loses it after two minutes. When
the server takes a view away it tells the holder, whose further transfers
are refused. A key or a lockpick shows nothing until the lock is beaten.
Take All asks for every stack in one request before the game closes the
screen.

The service/RPC privacy regression uses two local network clients against
the production server handlers. Run it from the Framework root with
`builds\build.bat RunKCDCTests 64`. It covers closed, held and
cross-world stock, expired grants, physical linked containers, unlocks,
lost access, stale revisions and successful withdrawals, as well as virtual
stock privacy, waiting for the holder's first inventory projection, and
revocation on a world change regardless of distance.

## Death drops

`player.dropInventory({ keepEquipped? })` spawns a container just in front of
the player's body, so a revived player does not stand on it, and moves their
items into it -- every unit, or the unworn ones with `keepEquipped` -- up to
the container's 128 rows. `stashSpawn` is raised once it holds the drop, then
the player's `playerInventoryChanged` with reason `drop` and the container's
`stashInventoryChanged`; from then on it is anyone's to open. What it takes
to loot it and when it goes are the script's. The default gamemode drops the
pockets on `playerDied`, keeps worn gear, and removes a drop once it is empty
or after thirty minutes (`resources/kcdc-gamemode/src/server/death-drop.ts`).

## Locks and lockpicking

A container offers the game's own key and lockpick while `locked` is set.
`lockpickable` decides whether the pick is offered at all, and
`lockDifficulty` sets how hard the minigame is: a number from 0 to 1, which
the game's prompt labels in fifths -- below 0.2 very easy, then easy,
average, hard, and from 0.8 very hard. `lockFanciness` picks the lock the
minigame shows, `'common'` or `'fancy'`. Both are `null` until a script sets
them, which leaves the level's own lock; assigning `null` gives it back.
`stashInteract` with action `lockpick` is asked before the minigame starts,
so it is where a script decides who may pick at all.

`Stash.spawn` builds AnimStash.lua's chest unless its options name another
model. Any `.cgf`, `.cga` or `.cdf` works, and the level's own cabinets are
the drawers players already know:

```ts
const drawer = Stash.spawn(position, rotation, 0, { model: "objects/manmade/common_furniture/cabinets/cabinet_fancy_d.cgf" });
drawer.locked = true;
drawer.lockDifficulty = 0.7; // hard
drawer.addItem({ item: "bread", amount: 5 });

Events.on("stashInteract", (player, stash, action) => action !== "lockpick" || isThief(player));
```

A model without the game's open and close clips stays shut while it is
looted.

## Keeping contents

```ts
const saved = new Map<string, unknown[]>();
const key = (stash: Stash) => `${stash.guid}:${stash.virtualWorld}`;

const restore = (stash: Stash) => {
    const items = saved.get(key(stash));
    if (items) stash.setInventory({ items });
};

// Spawned containers, and level ones built in other virtual worlds.
Events.on("stashSpawn", (stash) => { if (!stash.isVirtual) restore(stash); });
// The level's containers in the global world exist before any resource runs.
Events.on("resourceStart", () => Stash.all().filter((stash) => !stash.isVirtual).forEach(restore));
Events.on("stashInventoryChanged", (stash) => { if (!stash.isVirtual) saved.set(key(stash), stash.getInventory()?.items ?? []); });
```

A level container's `guid` is the level's own and is the same on every
machine; a spawned one's is minted when it spawns.
