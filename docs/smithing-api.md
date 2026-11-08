# Smithing server API

Players smith at the game's own smitheries. The server takes the materials,
settles failures and grants the product (see
[authoritative-smithing.md](authoritative-smithing.md)). Scripts see it
through the same `Crafting` binding and events as alchemy
([alchemy-api.md](alchemy-api.md)); every payload names its `kind`.
Everything is in memory for the player's connection.

The synchronous `craftingRefunding(player, proposal, refund)` event lets a
resource request a full refund before an unfinished workpiece settles. See
[crafting refund policy](crafting-refunds.md) for reasons, callback lifetime,
and which reasons a client can claim. The default gamemode installs no
refund policy.

## Methods

```ts
Crafting.isAvailable("smithing");              // true
Crafting.stations("smithing", virtualWorld);   // smitheries players there can use
Crafting.recipes(player, "smithing");          // recipes enabled by required_dlc
Crafting.registerRecipe(definition);           // adds a recipe; see custom-recipes.md
Crafting.session(player);                      // { kind: "smithing", id, station, virtualWorld, phase: "working", recipe, materials, expiresInMs } | null
Crafting.occupancy(stationId, virtualWorld);   // who is working at a smithery
Crafting.cancel(player);                       // fails the workpiece (reason "cancelled"; a refund policy may return everything); the player's game leaves the smithery
```

- `stations` lists smitheries the way it lists alchemy tables, including
  placed ones with `placed: true`; see [alchemy-api.md](alchemy-api.md) and
  [World objects](world-objects.md#stations).
- A smithing recipe is known through its `perk`. The server checks the
  player's progression for it, so `player.addPerk(recipe.perk)` teaches it.
- `session.materials` are the units taken for the workpiece. Quest materials
  are required but never taken; since no inventory can hold one, a recipe
  that needs one is refused as missing its materials.
- A recipe ingredient with `quest: true` is a quest item, which only its
  quest hands out. The game cannot create one, so the inventory refuses it
  with `questItem` and `player.giveItem` returns false. A recipe that needs
  one cannot be prepared by a script.

## Events

```ts
Events.on("craftingStarting", (player, proposal) => {
    // { kind: "smithing", station, virtualWorld, recipe, continuationOf: "" }
    return proposal.kind !== "smithing" || player.virtualWorld === 0;
});
Events.on("craftingCompleting", (player, proposal) => {
    // { kind, session, station, outcome: "success", recipe, grade, product, amount, quality, xp, lockpicks }
    return proposal.kind !== "smithing" || proposal.quality <= 0.95;
});
Events.on("craftingCompleted", (player, event) => {}); // { kind, session, station, virtualWorld, outcome, recipe, grade, quality, xp, lockpicks, outputs }
Events.on("craftingRefunding", (player, proposal, refund) => {
    // `interrupted` is reported by the player's own client; see crafting-refunds.md before refunding on it.
    if (proposal.reason === "interrupted") refund();
});
Events.on("craftingEnded", (player, event) => {});     // { kind, session, station, virtualWorld, outcome, reason, refunded }
```

- **`craftingStarting`** runs before the materials are taken. A refusal
  leaves the recipe screen open and the inventory untouched.
- **`craftingStarted`** follows the take.
- **`craftingCompleting`** runs only for a finished workpiece:
  - `quality` is the workpiece quality the player's game reported, 0 to 1.
  - `grade` is the product's item tier.
  - `lockpicks` is the byproduct the server drew.
  - A refusal fails the workpiece: the failure share of the materials is
    spent and nothing is granted.
- **`craftingCompleted`**: `outputs` are the rows that received the product
  and the lockpicks. Do not grant them again.
- **`craftingEnded`**:
  - `outcome` is `success`, `failed` or `cancelled`.
  - `reason` is empty for a successfully finished workpiece. Otherwise
    it is one of:
    - `craftingFailed`, `craftingCompletingRejected`, `invalidQuality` or `abandonTooLate`
      (all failures);
    - `abandoned` (the recipe never reached the anvil: everything came
      back);
    - `inventoryUnavailable` (no room for the product: everything came
      back);
    - `cancelled`, `interrupted`, `disconnected`, `timeout` or
      `contextInvalidated` (cancelled; native failure settlement unless a
      refund policy requests all materials back).
  - `refunded` names the rows materials went back to.

## Testing a smithery

`/crafttest smithing` in the default gamemode:

- teleports the player to a smithery;
- grants one batch of every enabled recipe's materials;
- teaches the recipe perks the player lacks.

`/crafttest check` confirms the client sees the station.
