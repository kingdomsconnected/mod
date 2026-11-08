# Inventory scripting API

Server scripts own players' and NPCs' inventories. The server holds their
items in memory and their game shows them, but it keeps nothing
between sessions. Save what you need from `playerInventoryChanged` or
`playerDisconnect`, and restore it with `Inventory.set`.

The generated declarations in `scripting-api/generated/server-api.d.ts` are
the reference; this page is the overview.

Custom resource types, personalized letters and script-controlled actions are
documented in [Custom inventory items](custom-inventory-items.md). Register
saved definitions before restoring their inventory rows.

Client resources can replace native inventory actions before consumption
with [Inventory.onUse](inventory-use-client.md), including Eat/Drink for
ordinary game items. The client handler does not grant authority to change
server inventory.

## Reading

```ts
player.getInventory(): InventoryState | null
npc.getInventory(): InventoryState | null
Inventory.get(owner: Player | Npc): InventoryState | null

interface InventoryState {
    ready: boolean;      // player projection ready, or NPC stock established
    revision: number;    // advances by one with every change
    items: InventoryRow[];
    harvested: boolean | undefined; // NPC carcass state
    quickslots: InventoryQuickslots | null;
}

interface InventoryQuickslots {
    weapons: { primary: string | null; secondary: string | null }[];
    items: (string | null)[];
    activeWeapon: number;
    activeItem: number;
}

interface InventoryRow {
    id: string;          // unique within this owner's inventory
    item: string;        // item class GUID
    name: string;        // the game's table name, not localized
    amount: number;
    quality: number;     // same as metadata.quality
    health: number;      // absolute; same as metadata.health
    condition: number;   // 0..1; same as metadata.condition
    equipped: number;    // units the body wears or holds
    metadata: Record<string, unknown>;
}
```

`null` means the owner no longer exists. A player inventory exists from
`playerConnect` until after `playerDisconnect`; NPC stock lasts until removal. `metadata` is described in
[Inventory item properties](inventory-properties.md); pass it back unchanged to keep an item exactly.
`equipped` is what the player's game last reported the body wears or holds,
as far as a body can wear it: each piece of gear once, and one ammunition
stack per native type (arrows, bolts, balls and scatter ammunition). The
fullest reported stack of each type is kept independently. Rows with the
same class and native properties share that stack, so all reported rows in
it remain equipped. Other stacks of that type are left out. Each worn count
shrinks with its row. Reports sent before an `Inventory.set` reached the
player's game are ignored.

A worn unit can leave like any other: `remove`, `transfer` and a death drop
take it off the player's body as it goes, and their game reports the
equipment that is left. When a row shrinks, the loose units of its kind go
first and the body keeps what it wears.

Belts and pouches that provide quickslots are included in `equipped`.
`quickslots` describes the current native outfit's assignments and selected
slots. Both arrays always have four entries, ordered from slot 0 to slot 3.
References are inventory row IDs, with `null` for empty slots. Weapon slots
hold a primary and secondary item; an item can appear in several slots.
`activeWeapon` and `activeItem` are indices from 0 to 3. The snapshot is
`null` until the client reports it or a script restores it. Inactive native
outfits are not included.

Save `items` and `quickslots` together and restore them with
`Inventory.set(player, { items, quickslots })`. Referenced rows must retain
their IDs. Removing a row clears its references. Restore equipped belts and
pouches too: their capacity determines which slots the game can use. Native
eligibility, weapon pairing and capacity still apply; the client verifies
restoration before reporting its equipment back. A successful `set` accepts
the server state; it does not promise that the game can equip every request.
If the client cannot create a saved item, its quickslot reference is retained
without blocking reports of other equipment changes. A visible replacement
in that slot takes precedence over the unavailable item.
If the selected item is unavailable, the reported selection follows the
game's current selection once restoration finishes.
Omitting `quickslots`, or passing `null`, leaves assignments to the game's
normal equipment handling. Pass a snapshot with all references `null` and
both selections `0` to clear the menus explicitly.

## Changing

Every call happens at once and in full, or not at all, and returns:

```ts
interface InventoryResult {
    ok: boolean;
    code: string;          // "" on success
    revision: number;      // after the call; a transfer's is the source's
    items: InventoryUnit[]; // the rows that received units
}
```

| Call                                                 | What it does                                                                                                                                                                                                                                                                                                                                     |
| ---------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `Inventory.add(player, { item, amount, metadata? })` | Gives 1..10000 of a class, named by GUID or by the exact name the item tables use. Omitted properties mean quality 1 at full condition; `metadata.quality` gives a higher tier, up to the class's own maximum. Joins the row already holding that kind of item.                                                                                  |
| `Inventory.remove(player, { units })`                | Takes exact units from named rows, all or none.                                                                                                                                                                                                                                                                                                  |
| `Inventory.setProperties(player, { id, metadata })`  | Replaces one row's properties; id and amount stay.                                                                                                                                                                                                                                                                                               |
| `Inventory.set(player, { items, quickslots? })`      | Replaces the whole inventory. Rows keep the `id` they are given (letters, digits, `-_:.`, up to 64 characters) and get one when they have none. A row's `equipped` count (gear including belts and pouches, 48 rows in all) dresses the body in it. Optional `quickslots` restores assignments and selections. `items: []` clears the inventory. |
| `Inventory.transfer(source, target, { units })`      | Moves exact units with their properties, all or none. The target's rows are in `result.items`.                                                                                                                                                                                                                                                   |
| `player.giveItem(item, amount?)`                     | `add` with default properties; returns `ok`.                                                                                                                                                                                                                                                                                                     |
| `player.takeItem(item, amount?)`                     | Takes a class across rows, all or none; the returned promise is already settled.                                                                                                                                                                                                                                                                 |
| `player.dropInventory({ keepEquipped? })`            | Moves the player's items into a container spawned at their body (reason `drop`), keeping worn units with `keepEquipped`. Returns the `Stash`, or null when nothing would move. See `docs/stash-inventory-api.md`.                                                                                                                                |

Every request accepts an optional expected `revision` (a transfer takes
`sourceRevision` and `targetRevision`). Given, the call is refused as
`staleRevision` when the inventory changed since you read it -- use it when a
decision depends on what you read, such as a price check before a sale.

Refusal codes: `inventoryUnavailable` (not connected), `invalidRequest`,
`staleRevision`, `invalidItem` (unknown class), `invalidItems` (bad or
repeated unit list, bad id), `invalidAmount`, `unknownItem` (no such row),
`insufficientItems`, `inventoryCapacity` (4096 rows or 448 KiB of rows),
`sameInventory`, `invalidEquipment` (an `equipped` count over the row, on a
class that is not gear, or on more than 48 rows), `invalidQuickslots` (bad
snapshot shape, selection outside 0..3, or a reference absent from the
replacement rows), and the property policy's codes: `invalidMetadata`,
`unknownItemClass`, `questItem` (a quest item, which the game cannot create;
only its quest hands one out), `invalidQuality`, `immutableItemHealth`,
`invalidItemHealth`, `contradictoryItemHealth`, `invalidCreationSentinel`,
`unsupportedPoisonProperties`, `unsupportedOnEquipBuffs`.

The `Inventory` operations accept `Player | Npc` owners. A call given neither,
or a request that is not an object, throws.

## Events

```ts
playerInventoryChanged: [player: Player, change: InventoryChange]
playerInventoryReady: [player: Player]
npcInventoryChanged: [npc: Npc, change: InventoryChange]
npcInventoryReady: [npc: Npc]
playerItemUsed: [player: Player, item: string, kind: "food" | "potion" | "ointment" | "shot", amount: number]

interface InventoryChange {
    revision: number;
    reason: string; // add, remove, properties, set, transfer, wear, repair, sharpen, dry, smoke, cook, use, shot, pickpocket, trade, drop, deposit, withdraw, ...
    items: { id: string; before: InventoryRow | null; after: InventoryRow | null }[];
}
```

`playerInventoryChanged` is raised on the next server tick, in the order the
changes happened, once per inventory an operation touched -- a transfer raises
it for both players. It carries only the changed rows. A handler may change
inventories; those changes are raised on a later tick. `wear` is the player's
game wearing an item down in use, which the server accepts only as a loss.
`equipment` means worn equipment, a quickslot assignment or a selected slot
changed. These changes are combined into one event per player per tick.
Its `items` list is empty and the item revision does not advance; read
`Inventory.get(player)` in the handler to save the current state.
`repair` is the player's game applying repair kits: the kits' durability loss
and every selected item's health gain arrive in one change, or the selection is
projected back (see [Repair kits](inventory-properties.md#repair-kits)).
`sharpen` is a weapon's health raised on a grindstone (see
[Grindstones](inventory-properties.md#grindstones)).
`dry`, `smoke` and `cook` are items converted on a drying rack, in a
smokehouse or at a fire (see
[Drying racks, smokehouses and fires](#drying-racks-smokehouses-and-fires)).
`deposit` and `withdraw` are moves into and out of a container; see
[Chest contents](stash-inventory-api.md).

Food and potions reserve one unit before native use. Server resources can
refuse consumption, suppress native effects and apply their own stats after
confirmation; see [Food, potions and poison](consumables.md). Successful use
raises `playerConsumed` and `playerItemUsed`. The inventory change is `use`;
a native refusal produces `use_refund`.

Ointments and ammunition still report their spent units after the game acts.
The server accepts only matching consumable rows, at most ten units per use
and sixteen per shot; ammunition must have been in the quiver. These also
raise `playerItemUsed` after the tick's inventory changes. Consumption receipts
describe what the owning client reports, not a server simulation of the action.
A round that
comes to rest where it can be picked up -- ground, wall, body -- is laid down
again as a ground item anyone can take: once per round fired, within twenty
minutes, within 300 m of the archer and in the world it was fired in. A round
that broke on impact comes back as the broken round of its own kind, and every
recovered round counts towards the archer's stacks on the ground.

A thief takes `pickpocket` units from their victim's unworn items; see
`docs/pickpocketing.md`.

A vendor deal is one `trade` change: the exact units sold and the money paid
leave, what was bought and any change arrive, in one commit. `vendorTrade`
reports the priced lines; see `docs/vendor-system.md`.

`playerInventoryReady` is raised once per session, when the player's game
first shows their inventory.

## Drying racks, smokehouses and fires

A drying rack dries herbs and food; a smokehouse smokes food; a fire cooks
food. Racks and smokehouses are the game's `so_smokehouse` smart object, told
apart by its `isSmokehouse` property: 128 of them are placed across the three
levels. A fire is a fireplace with a cook pot, as `CookPot.all` lists them
(418 across the levels). The player picks items in the game's selection
screen, an animation plays, and the moment it ends the game turns the picked
items into their `DriedItemId`, `SmokedItemId` or `CookedItemId` class.
Nothing about it takes time and nothing is left behind; a herb comes out at
full health and food keeps the health it had. The same event gives survival
XP per item: 0.5 dried, 3 smoked, 1 cooked (`SurvivalXpRewardPerDriedItem`,
`...PerSmokedItem`, `...PerCookedItem`).

The player's game converts its own copy and asks the server for the same
conversion of the rows it held. The server checks that the player can act
within four metres of a placed rack, smokehouse or cook pot of that kind, and
that every row converts that way, then raises

```ts
playerPreparing: [player: Player, preparation: Preparation]

interface Preparation {
    kind: "dry" | "smoke" | "cook";
    station: string; // "<level>:<guid>", the same on every machine and across restarts; a fire's guid is its CookPot guid
    items: { id: string; item: string; amount: number; product: string }[];
}
```

Return `false` synchronously to refuse: the rows stay as they were and the
player's game puts them back. Accepted, the rows are taken and the products
given as one `dry`, `smoke` or `cook` inventory change, and the server grants
the survival XP for the units converted, as it does a gathered herb's: like
`player.addXp`, through the player's own multipliers, without
`playerXpGaining`, and whatever `Progression.setXpRate`, `setXpBudget`,
`setLevelCap` or `setNativeXp(false)` say. The player's game holds its own XP
back, so a refused or invalid conversion gives none, and none can be had
twice for the same items. That makes the station a key a game mode can hang
ownership on, and a refusal the way to make it take time:

```js
const owners = new Map(); // station -> nickname

Events.on("playerPreparing", (player, preparation) => {
    if (owners.get(preparation.station) !== player.nickname) return false;
    if (preparation.kind !== "smoke") return true;
    // Smoke for an hour: take the meat now, hand the smoked meat over later.
    const units = preparation.items.map(({ id, amount }) => ({ id, amount }));
    if (!Inventory.remove(player, { units }).ok) return false;
    setTimeout(() => {
        for (const { product, amount } of preparation.items) Inventory.add(player, { item: product, amount });
    }, 60 * 60 * 1000);
    return false;
});
```

A rack in a quest layer that is not loaded on the player's machine cannot be
used there; a rack or fire the catalogs do not list (DLC, spawned at run time)
refuses every conversion. The game lights a fire a player cooks at; whether a
fire a script put out with `CookPot.setFireLit` may still cook is the
script's to decide, from `CookPot.get(guid).fireLit`.

## NPC stock, looting and harvesting

`Inventory.get`, `add`, `remove`, `setProperties`, `set` and `transfer` also
accept `Npc` handles. Transfers can move items between either kind of owner.
The same row identities, canonical properties, revisions and atomic capacity
checks apply. A normal NPC first acquires its native default stock from the
client elected to simulate it; this census happens once. Wait for
`npcInventoryReady` before adding to that stock. `Inventory.set` can provide
an explicit baseline immediately, including before any body exists.

Stock survives simulator changes, streaming, body rebuilds and revival.
`npc.wear` reuses held items and adds missing ones. Undressing retains the
clothes in stock, and looting a worn unit removes it from the outfit on every
client. Removing an underlayer also unequips clothing that requires it, keeping
that clothing in stock and available to loot. Dressing and restoring stock
respect the same equipment dependencies. Deposited items stay loose. The
server never regenerates an emptied corpse's original clothes.

A nearby living player can open a dead, Lootable NPC's native exchange screen.
The server grants exclusive access, checks both inventories' revisions and
moves a selection atomically. Disconnecting, changing world, leaving range,
revival, restoring stock and expired access invalidate the grant. An inventory
that is still projecting cannot be spent.

Animals use the native butchering animation and a server-owned completion.
The default gamemode's `npc-spawn.ts` populates their inventory at spawn with
`Inventory.set`. `npc.getAnimalLoot()` exposes the compiled game-data
candidates, quantities and native tags; the core does not roll drops.
Default chances are 100% for meat and 50% for special parts, editable in that
gamemode file. Authored quantity fractions and variation still bound amounts.
The native Harvester tag is metadata, not a hardcoded perk requirement.

Butchering unlocks the prepared inventory. It never adds stock or rerolls it.
Cancellation keeps that stock inaccessible. Revival retains whatever remains
and makes the body harvestable again, without replenishing taken units.
Human gear is censused once and tracked as inventory with worn row counts;
the animal policy leaves it untouched. Adopted animals resolve their catalog
from the native soul census; a resource can populate them after
`npcInventoryReady` using the same public API.

Butchering access and completion travel with the inventory snapshot. Other
players cannot use a body while its action or loot screen is held. Completing
changes every client's interaction from Butcher to Loot and applies the native
butchered model, including clients that stream the body in later. Classes
without a butchered model retain their authored appearance. Restoring an
unharvested animal or reviving it restores its original model. The player's
native animation and its cancellation use body-action replication.

NPC reads include `harvested`. To restore a corpse without harvesting it twice,
pass both `items` and `harvested` to `Inventory.set(npc, saved)`. Omitting
`harvested` preserves the NPC's existing state. Save on `npcInventoryChanged`
for stock mutations and on `npcHarvested` for completion, which changes the
harvested flag without changing item quantities. The latter includes the NPC
and the player who completed it, so rewards also belong to the gamemode.

## Item classes

```ts
Inventory.getItemPrice(item: string, metadata?: Record<string, unknown>): { pristine: number; current: number } | null
Inventory.setItemName(item: string, name: string | null): string[]
Inventory.setItemStats(item: string, stats: ItemStatOverrides | null): boolean
Inventory.getItemStats(item: string): { [stat: string]: number } | null
```

`item` is a class GUID or its exact table name.

`getItemPrice` is what the game prices one unit at, in money units, computed
on the server the way the game does it (`C_RPGInventory` slots 9 and 10):
the class's authored `Price`, scaled by health weighted 0.99, rounded to a
hundredth. `current` uses the item's own health and `pristine` the best
health its quality allows, so quality moves the price only through health.
Metadata is read as `Inventory.add` reads it, so a row prices as
`Inventory.getItemPrice(row.item, row.metadata)`. It is null for an unknown
class or for metadata `Inventory.add` would refuse. Shop terms and haggling
are not part of it; a `Vendor` charges what its script sets.

`setItemName` changes what every player's game calls a class -- inventory,
shops, loot, anywhere the game names it -- for everyone, including players
who join later, until the server stops. `null` gives the game's own name
back. The game names items through shared localization keys (`UIName`), and
many classes share one: every kite shield is a Kite Shield. Renaming one
renames all of them, and the result lists their GUIDs. A name is up to 128
bytes with no control characters, and is shown as given in every language.
A class the game shows no name for, an unknown class or a name that breaks
those limits throws.

`setItemStats` rewrites the combat numbers of a class on every player's game,
now and for whoever joins later, until the server stops; `null` gives the
class its authored numbers back. The game keeps these numbers on the class
row, never on the item: every `longsword` shares one row, and the hit
calculation reads attack and defense from it on every swing. So one call
reaches every copy of the class at once and takes effect on the next hit,
with nothing to re-equip. A set that names only some stats leaves the rest as
authored, and replaces the previous set whole.

| Table element             | Stats                                                                                        |
| ------------------------- | -------------------------------------------------------------------------------------------- |
| `MeleeWeapon`             | `attack`, `attackModStab`, `attackModSlash`, `attackModSmash`, `defense`, `strReq`, `agiReq` |
| `Armor`, `Helmet`, `Hood` | `defenseStab`, `defenseSlash`, `defenseSmash`, `noise`, `strReq`                             |
| `MissileWeapon`           | `power`, `limbResistance`, `defense`, `strReq`, `agiReq`                                     |
| `Ammo`                    | `attackStab`, `attackSlash`, `attackSmash`, `powerMod`                                       |

Any other name, a name the element lacks, a non-finite value or one beyond
100000 throws; a class that is none of these elements throws too. `strReq`
and `agiReq` are whole stat levels, rounded, and cannot be negative. `getItemStats` reads the
server's own override back; the authored numbers are the game's tables, which
the server does not carry.

A class a running resource's KCD2 mod adds takes its override like a shipped
one; a client writes it once the mod's table patches have loaded, and writes
every override again after a level edge reloads the databases.

Swing speed is not a column: the game computes it per attacker from agility,
the weapon skill and the `MaxAttackSpeed*` rpg params, and the one runtime
lever is the `attackSpeed` buff modifier. Per-player numbers are buffs too:
`Buffs.register` takes `weaponAttack`, `weaponDefense` and `attackSpeed`, and
`player.addBuff` puts the result on one body. A stronger copy of one class for
one player is a class of its own, from `Items.register`.

```ts
// Every longsword hits harder, for everyone, until the server stops.
Inventory.setItemStats("longsword", { attack: 120, attackModSlash: 1.5 });
// One player's swings are 20% faster and 25% harder.
const frenzy = Buffs.register("arena:frenzy", {
    name: "Frenzy", visual: "potion_marigold_decoction", duration: 60,
    modifiers: { attackSpeed: { operation: "multiply", value: 1.2 }, weaponAttack: { operation: "multiply", value: 1.25 } },
});
player.addBuff(frenzy);
```

Renaming a single item rather than its class is not supported: the game keeps
no name on an item, only on its class.

## Examples

Keep inventories between sessions:

```ts
Events.on("playerConnect", (player) => {
    const saved = db.load(player.nickname); // your storage
    if (saved) Inventory.set(player, saved);
});

Events.on("playerDisconnect", (player) => {
    const state = player.getInventory();
    if (state) db.save(player.nickname, { items: state.items, quickslots: state.quickslots });
});
```

Save as things change instead, if a crash must not lose a session:

```ts
Events.on("playerInventoryChanged", (player) => {
    const state = Inventory.get(player);
    if (state) db.save(player.nickname, { items: state.items, quickslots: state.quickslots });
});
```

Trade one item after the script's own checks:

```ts
function giveOne(from: Player, to: Player, id: string) {
    const result = Inventory.transfer(from, to, { units: [{ id, amount: 1 }] });
    if (!result.ok) console.log(`trade refused: ${result.code}`);
    return result.items[0]?.id; // the row it landed in on the target
}
```

Starter kit on first join:

```ts
Events.on("playerConnect", (player) => {
    if (!db.load(player.nickname)) player.giveItem("b9de1d84-a0c1-4b81-9f60-8d7fbb3cb9d4", 5); // Sage
});
```
