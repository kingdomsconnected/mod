# Alchemy server API

Players brew at the game's own alchemy tables; the server rules on every
step and computes the result (see
[authoritative-alchemy.md](authoritative-alchemy.md)). What a resource
gets is the `Crafting` binding and six events. Everything is in memory
for the player's connection: sessions end on disconnect, and recipe
knowledge starts empty every session unless a script restores it.

The synchronous `craftingRefunding(player, proposal, refund)` event lets a
resource request a full refund before an unfinished batch settles. See
[crafting refund policy](crafting-refunds.md) for reasons, callback lifetime,
and which reasons a client can claim. The default gamemode installs no
refund policy.

## Methods

```ts
Crafting.isAvailable("alchemy");               // true; smithing too, see smithing-api.md
Crafting.stations("alchemy", virtualWorld);    // CraftStationInfo[] players there can use
Crafting.recipes(player, "alchemy");           // CraftRecipeInfo[] enabled by required_dlc
Crafting.registerRecipe(definition);           // adds a recipe; see custom-recipes.md
Crafting.session(player);                      // CraftSessionInfo | null
Crafting.occupancy(stationId, virtualWorld);   // CraftStationOccupancy | null
Crafting.cancel(player);                       // boolean
Crafting.knowledge(player);                    // Record<string, number>
Crafting.setKnowledge(player, recipeId, mask); // boolean
```

- `stations` lists the tables players in one virtual world can use, the
  global one when `virtualWorld` is omitted: the level's own, minus any a
  level edit took out of that world, and every table `SmartObject.spawn` or
  a world resource placed there or in the global world. `placed` is true for
  those; their `layer` is empty. Pass `undefined` as the kind to list both
  kinds for a world. See [World objects](world-objects.md#stations).
- `session` is a copy of the player's session: `id`, `station`,
  `virtualWorld`, `phase` (`entering` until the opening animation finishes,
  `brewing`, `finishing` while a finished batch waits to settle,
  `betweenBatches` when the player kept the table after a batch),
  `sequence`, `resources` on the table and `expiresInMs`.
- `occupancy` says who holds one table in one virtual world: `playerId`,
  `entryId` (the visit), `sessionId`, `phase` and `expiresInMs`. Null for
  an unknown or non-alchemy station.
- `cancel` ends the player's session as leaving the table would --
  wholly unmilled bowl, mortar and herb groups go back to the inventory,
  the rest was spent, unless a `craftingRefunding` handler calls
  `refund()`, which runs synchronously inside this call -- and releases
  the table. The player's game closes
  the table. `craftingEnded` follows on the next tick. False when there was
  nothing to end.
- `knowledge` maps a recipe id (the decimal `key.id` of `Crafting.recipes`)
  to a mask of its 29 authored steps; `536870911` is the whole recipe. A
  recipe not listed is unknown.
- `setKnowledge` sets one recipe's mask and sends the change to the
  player's recipe book; `0` forgets the recipe. False for an unknown recipe,
  a mask above `536870911`, or a player who is not connected.

Skills and perks are not configured here: the alchemy skill and the perks
that matter to brewing come from the player's own progression (see the
`Progression` binding), their permanent buffs from what their game reports.

## Events

```ts
Events.on("craftingStarting", (player, proposal) => {
    // { station, virtualWorld, continuationOf }
    return player.virtualWorld === 0; // literal false refuses
});
Events.on("craftingStarted", (player, event) => {});   // { session, station, virtualWorld, continuationOf }
Events.on("craftingCompleting", (player, proposal) => {
    // { session, station, outcome, recipe, grade, product, amount, quality, xp }
    return true; // literal false: the batch ends failed without a product
});
Events.on("craftingCompleted", (player, event) => {}); // { session, station, virtualWorld, outcome, recipe, grade, quality, xp, outputs, knowledge }
Events.on("craftingRefunding", (player, proposal, refund) => {
    // `interrupted` is reported by the player's own client; see crafting-refunds.md before refunding on it.
    if (proposal.reason === "interrupted") refund();
});
Events.on("craftingEnded", (player, event) => {});     // { session, station, virtualWorld, outcome, reason, refunded }
```

- `craftingStarting` and `craftingCompleting` are vetoes: every handler runs,
  and one returning literal `false` refuses. An async handler cannot refuse.
  Their payloads are frozen.
- `craftingStarted` fires when a fresh entry's opening animation finished,
  or at once for the next batch at a kept table.
- `craftingCompleted` fires when a result was granted -- a recipe or the
  failed potion -- and is followed by `craftingEnded`. `outputs` are the
  inventory rows that received the product; do not grant it again. `xp` is
  the base alchemy XP ordered; the player's own multipliers apply on top.
- `craftingEnded` fires for every batch that is over. `outcome` is
  `success`, `failed` or `cancelled`; `reason` is empty for a completed
  batch, otherwise `craftingCompletingRejected`, `cancelled`, `interrupted`,
  `clientError`, `disconnected`, `timeout` or `contextInvalidated` (walked
  away, died, changed world). `refunded` are the inventory rows ingredients
  went back to. A table kept between batches is not another end.
- `craftingRefunding` runs before an unfinished batch returns ingredients.
  Only a synchronous `refund()` call requests a full refund; return values
  are ignored. See [crafting refund policy](crafting-refunds.md).
- `craftingStarted`, `craftingCompleted` and `craftingEnded` are raised after
  the `playerInventoryChanged` they caused, so handlers read the updated rows.
- A disconnecting player's session ends before `playerDisconnect`, so the
  refunds are in the inventory a script saves there, and
  `Crafting.knowledge` still answers there.

## Keeping knowledge across sessions

```ts
const saved = new Map<string, Record<string, number>>();

Events.on("playerInventoryReady", (player) => {
    for (const [recipe, mask] of Object.entries(saved.get(player.nickname) ?? {})) Crafting.setKnowledge(player, recipe, mask);
});
Events.on("craftingCompleted", (player, event) => saved.set(player.nickname, event.knowledge));
Events.on("playerDisconnect", (player) => saved.set(player.nickname, Crafting.knowledge(player)));
```

## Testing a table

`/crafttest alchemy` in the default gamemode teleports the player to a
table and grants one batch of every enabled recipe's ingredients;
`/crafttest check` confirms the client sees the station.
