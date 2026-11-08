# Stew pots

Stew pots share a finite supply between players in the same virtual world.
A stocked pot starts with four portions. Eating takes one portion; the pot
looks half full with two left and empty with none. Pots authored as empty
start empty. They do not refill automatically.

The server approves each meal before the game applies its nourishment and
other food effects. An empty or disabled pot cannot feed a player. Ordinary
cooking at the fire and wine barrels keep their existing behavior.

Game modes can refuse a serving, replace its food effects or block its added
poison through the [consumption events](consumables.md).

Pots serve porridge (`lentil`, 10 nutrition), vegetable stew (`soup`, 15), or
meat stew (`goulash`, 25). The native food effects and appearance follow the
current meal type. Changing a pot affects everyone in its virtual world.

## Refill or poison a pot

The default game mode provides these chat commands:

- `/pot` describes the nearest pot within four metres.
- `/pot recipes` lists the ingredients for each meal.
- `/pot refill porridge`, `/pot refill vegetable`, or `/pot refill meat`
  spends the required inventory food and fills an empty pot with four
  portions. Missing ingredients leave both the inventory and pot unchanged.
- `/pot poisons` lists your poison inventory rows.
- `/pot poison <row>` consumes one dose from that row and poisons the nearby
  stocked pot. The poison keeps its native strength and ingestion effect.

The example recipes use four bread rolls for porridge; four cooked cabbages
and four cooked carrots for vegetable stew; or four cooked beef items for
meat stew. These are game-mode recipes, not recipes taken from the base game.
Server owners can change them in `resources/kcdc-gamemode/src/server/cook-pots.ts`.
Spoiled ingredients are skipped. Split stacks can supply the same recipe.

A poisoned pot affects subsequent approved player meals until a fresh refill
clears it. Adding another dose is refused. `/pot` does not reveal poison.
The native hold-to-poison prompt remains hidden: use the game-mode command.
NPC poisoning and the base game's witness/crime reactions are not included.

## Server scripts

`CookPot.all(virtualWorld = 0)` returns frozen snapshots of the pots on the
server's level. Each snapshot has `guid`, `position`, `portions`, `capacity`,
`state`, `enabled`, `food`, `nutrition`, `poison`, and `virtualWorld`.
`poison` is the ingested buff GUID, or `null`; it is available to server
scripts. `CookPot.get(guid, virtualWorld = 0)`
returns one snapshot, or `null` for an unknown GUID. GUIDs name the fireplace
smart object, rather than its eating trigger or visible cauldron.

```js
const pot = CookPot.all()[0];
if (pot) {
    CookPot.setEnabled(pot.guid, false); // Block eating; keep its contents.
    CookPot.setState(pot.guid, "half");  // Two portions, visibly half full.
    CookPot.refill(pot.guid, 8);         // Eight portions, visibly full.
    CookPot.setFood(pot.guid, "soup");   // Vegetable stew and its food effects.
    CookPot.setEnabled(pot.guid, true);
}
```

- `CookPot.refill(guid, portions = 4, virtualWorld = 0)` replaces remaining
  stock and its capacity. `portions` must be an integer from 0 to 1000. Half
  full means at most half of that refill remains. A refill clears poison and
  preserves the meal type and enabled state.
- `CookPot.setState(guid, state, virtualWorld = 0)` accepts `"empty"`,
  `"half"`, or `"full"`. These set zero, two, or four portions, with a capacity
  of four. Changing the appearance changes the stock too. It preserves the
  meal type, poison, and enabled state; it does not create a fresh batch.
- `CookPot.setEnabled(guid, enabled, virtualWorld = 0)` allows or blocks
  eating without changing the stock or its appearance.

`CookPot.setFireLit(guid, lit, virtualWorld = 0)` lights or puts out the
pot's fire, with its steam and smoke, for every player in that world. Until a
script calls it, each player's own fireplace AI lights the fire when a
villager stokes it and puts it out on its own timer, as in single player, so
players can see the same fire differently. Once set, that AI no longer
changes it; `null` hands it back. Snapshots carry it as `fireLit`: `true`,
`false`, or `null` while it is each player's own.

```js
// "Light it with a torch, put it out with E" is a game mode's choice:
// a client script shows an ActionHint at the fireplace and sends the press;
// the server checks the player and decides.
CookPot.setFireLit(pot.guid, true);
```

`CookPot.setFood(guid, food, virtualWorld = 0)` accepts `"goulash"`,
`"lentil"`, or `"soup"`. It changes the appearance and future meals while
preserving stock, poison, and enabled state.

These setters return `false` for an unknown pot. Invalid argument types
or values throw. Refilling and changing state preserve `enabled`.

For player actions, use `CookPot.refillFromInventory(player, guid, recipe)`
and `CookPot.poison(player, guid, rowId)`. Both return `{ ok, code }` and
validate the player's current world, distance, ability to act, and inventory
readiness. A refused action changes nothing. The recipe comes from trusted
server code; do not accept ingredient costs or yield directly from a client.

```js
const result = CookPot.refillFromInventory(player, pot.guid, {
    food: "soup",
    portions: 4,
    ingredients: [
        { item: "cabbageCooked", amount: 4 },
        { item: "carrotCooked", amount: 4 },
    ],
});
if (!result.ok) console.log(result.code);
```

Recipes require 1Ã¢â‚¬â€œ1000 portions and 1Ã¢â‚¬â€œ16 ingredient entries, each naming a
food class and a positive integer amount. Costs are combined across matching
inventory rows and removed in one change. Player refills require an empty,
enabled pot. Administrative `CookPot.refill` does not spend inventory items.

`CookPot.poisons()` lists the item class GUIDs accepted by `CookPot.poison`.
The latter requires a stocked, enabled pot without poison and consumes
exactly one item from the player's named row. Inventory changes use the
reasons `cook_pot_refill` and `cook_pot_poison`.

Scripts choose who may call these actions and which recipes to offer. There
is no global recipe registration or automatic resource-owned refill policy.

World 0 has its pots from server start. Any other world gets its own set
when a player first enters it or a setter first names one of its pots; until
then `CookPot.all` returns an empty list for it and `CookPot.get` returns
`null`. Disabling and re-enabling a pot does not change its stock, so a meal
reserved before the toggle is still refunded if the game refuses it.

Stock is separate for each virtual world and survives player disconnects and
resource restarts. It lasts until the server stops. Scripts can save snapshots
and restore stock when the server starts, or schedule refills. There is no
automatic disk persistence.

Players with the default global visibility setting use stock world 0.
Moving a player to a numbered virtual world selects that world's supply.
The server sends this stock context explicitly; visibility on its own does
not give a client permission to eat from another world's pots.

## Native behavior and coverage

The generated catalog contains 418 pots in the base `klaster`, `kutnohorsko`,
and `trosecko` level archives. It records the fireplace GUID, eating-trigger
GUID, cauldron GUID, trigger position, food type, and home/camp variant. A
quest campfire without a cauldron is excluded. DLC and dynamically created
pots are not registered; unidentified stew triggers refuse eating.

The catalog also records meal item classes from `FireplaceSmartObject.lua`,
and nutrition and poison `BuffDefinitionId` values from `Tables.pak`.
Weapon-coating effects are not used for meals.

The client holds an eat press until the server reserves a portion. On a
grant it sets the fireplace's food property to the reserved meal and checks
whether the game allows the player to eat. If allowed, it replays the held
press through the ordinary interaction; otherwise the portion is returned.
Disabled pots offer no eat prompt, and the native poison prompt stays hidden.

The client keeps the pot's appearance and edible state in sync with the
server's stock, including after local fireplace AI changes. An empty pot
shows the game's own "empty kettle" refusal. A half-full pot remains edible.

A request identifies a pot, connection-local sequence, player incarnation,
virtual world, and server-issued context revision. The server resolves the
player from the sender, checks their current world, body, inventory
readiness, ability to act and distance within four metres, then reserves one
portion. Other players immediately see that portion as unavailable. Native
refusal returns it once, provided a script has not refilled or set the stock
meanwhile. Disconnects and missing receipts spend the reserved portion so an
already eaten meal cannot be refunded by reconnecting. A new meal can be
admitted at most once per second per player.

Clients reject late approvals after body, level, world or session changes,
or when the player has moved away. A reservation can
therefore be lost during a disconnect or timeout. Stock remains finite.

Reservations keep the food type and poison present when admitted. Later
refilling, changing the meal, or adding poison cannot change an outstanding
meal. Poison is sent through the existing server buff API once an accepted
eating receipt arrives; a refusal applies no effect, and a duplicate receipt
does nothing. The effect names the body that ate, so a delayed delivery
cannot poison a replacement body. This uses the existing meal receipt's
trust model. A missing receipt spends the portion but does not apply poison.
Local fireplace AI is not given the server's poison history, so it cannot
apply the same dose twice.

## Reproducing the checks

From the mod repository:

```powershell
python tools/generate_cook_pot_catalog.py --game <game-directory>
python -m unittest discover -s tools -p test_generate_cook_pot_catalog.py -v
```

From the Framework root, `builds\build.bat RunKCDCTests 64 Debug` includes
the stock and packet validation tests. Build both `KCDCServer` and
`KCDCClient`; both sides must be updated for the pot protocol.
The `cook-pot-rpc` suite exercises inventory costs and meal/effect receipts
with two transport clients. It does not validate native visuals, animations,
or the health effects in a running game.
