# Custom inventory items

Server resources register immutable item types and give their returned GUIDs
to the existing inventory APIs. No extra assets or gathering system are
required. Both peers must run protocol version 2.0.0 or later.

```ts
const woodDefinition = {
    name: "Wood",
    description: "Timber gathered for the village",
    visual: "bsmt_woodExotic",
    weight: 2,
    price: 12,
};
const wood = Items.register("village:wood", woodDefinition);

// In an existing, server-validated gathering reward handler:
function rewardWood(player: Player, amount: number): InventoryResult {
    return Inventory.add(player, { item: wood, amount });
}

// Example bonus for the existing herb gathering system. This does not
// introduce mining or woodcutting interactions.
Events.on("gatheringHarvested", (player) => rewardWood(player, 1));
```

`visual` accepts an exact base-game GUID or internal table name from the
MiscItem, Misc, CraftingMaterial or Document categories. The native class is
a new MiscItem: it borrows model, material and icon, with the generic
MiscItem entity script from `arrow_broken`. It does not inherit the visual's
quest, consumable, crafting or reading behavior. In particular, a letter
appearance does not automatically start vanilla reading.

Defaults are description `""`, price `0`, stackable `true`, usable `false`
and useLabel `"Use"`. Name and weight are required, and either a visual or
an own model and icon. IDs contain
letters, digits and `:_-.`, up to 128 bytes. Names are at most 256 bytes,
descriptions 4096 and action labels 64, without control characters. Weight
is finite, from 0 to 1,000,000; price is a whole number from 0 to
2,147,483,583. Higher prices overflow the game's float-to-integer pricing
calculation. Large supported prices follow the game's floating-point rounding.
At most 4096 definitions are accepted per server lifetime.

Registering the same ID and canonical definition again returns the same
GUID. A different definition for an existing ID throws. Registration can
happen during play; `Inventory.add` can immediately follow it. The client
installs definitions on its game thread before projecting their items.
Registration failure leaves inventory readiness false and logs the reason
on the client and server.

## Own model and icon

A type can bring its own look instead of borrowing it. `model` is a path
relative to `Objects/`, the way the game's item tables write it, and draws
the item on the ground, in hand and in the inventory preview. The game puts
`Objects/` in front itself and adds `.cgf` when the path has no extension,
so a path that starts with `objects/` is refused. A model the resource
streams as `stream/objects/kcdc/<resource>/box.cgf` is
`kcdc/<resource>/box.cgf`. The model needs a physics proxy, as the game's
own item models have, for the item to be picked up from the ground: a model
without one shows there but cannot be picked up. `material` is a material
path in the same form,
allowed only with `model`; without it the model's own material is used.
`icon` is a base64 PNG of at most 256 KiB and 4096x4096 pixels, scaled on
each client to the stock 64x64 icon with its aspect kept and served from
memory. A file cut short or damaged is refused when it is registered. The
server does not decode the image itself, so a PNG whose pixel data a client
cannot read still registers; that client shows the visual's icon, or a stock
one without a visual, and logs a warning. Without `visual`, `model` and
`icon` are required; with it, each one given replaces what the visual would
lend. The type is still a MiscItem.

```ts
const box = Items.register("village:box", {
    name: "Village box",
    model: "kcdc/village/box.cgf",
    icon: fs.readFileSync(path.join(__dirname, "box.png")).toString("base64"),
    weight: 1,
});
```

## Personalized letters and use

```ts
const letterDefinition = {
    name: "Letter",
    visual: "a54ab5ef-d4a2-4929-9045-1a1efde935c5",
    weight: 0.1,
    stackable: false,
    usable: true,
    useLabel: "Read",
};
const letterType = Items.register("village:letter", letterDefinition);

function deliver(player: Player, mailId: string, sender: string) {
    return Inventory.add(player, {
        item: letterType,
        amount: 1,
        metadata: {
            custom: {
                name: `Letter from ${sender}`,
                description: "Addressed to Henry",
                data: { mailId },
            },
        },
    });
}

// A real resource would load these pages from its own persistent mail store.
const mail = new Map([
    ["letter-123", ["<paragraph>Henry, meet me by the mill. Anna.</paragraph>"]],
    ["letter-456", ["<paragraph>Your timber order is ready. Hans.</paragraph>"]],
]);

Events.on("playerCustomItemUse", (player, row, revision) => {
    if (row.item !== letterType) return;
    const custom = row.metadata.custom as { data?: { mailId?: string } };
    const pages = mail.get(custom?.data?.mailId ?? "");
    if (!pages) return;
    // Only the intended content is sent; the private metadata is not.
    player.emit("village:read-mail", JSON.stringify({ pages }));

    // For a consumable instead, scripts explicitly request consumption:
    // Inventory.remove(player, { units: [{ id: row.id, amount: 1 }], revision });
});
```

The matching client resource uses the existing Book API:

```ts
Events.on("village:read-mail", (payload: unknown) => {
    const message = typeof payload === "string" ? JSON.parse(payload) : payload;
    if (!message || !Array.isArray(message.pages)) return;
    Book.open({ style: "letter", pages: message.pages });
});
```

The server validates ownership, current inventory revision and usability
before emitting `playerCustomItemUse`. Its row is frozen. Both native
primary-action and double-click paths are intercepted before vanilla use.
No item is consumed automatically. Async scripts should retain the revision
and handle a stale result before performing a later inventory mutation.

A client resource can instead register [Inventory.onUse](inventory-use-client.md)
for a custom item ID or GUID. That handler takes priority over
`playerCustomItemUse`; it does not also send the automatic server event.
The same client API works with ordinary native items such as apples.

`InventoryRow.name` remains the internal table name (the registration ID for
a custom type). The optional display overrides live in `metadata.custom`.
Client inventory list/detail rows and the pickup-name binding use them.

```ts
Inventory.updateCustom(player, {
    id: row.id,
    name: "Opened letter from Anna",
    data: { mailId: "letter-123", opened: true },
    revision: Inventory.get(player)!.revision,
});
Inventory.updateCustom(player, { id: row.id, name: null });
```

Omitted fields stay unchanged. Null name/description restores the type
default. Supplied data replaces the whole previous object; null clears it.
The operation updates the whole row and preserves its ID, quantity and
native properties. Custom properties are limited to 16 KiB of JSON and 48
levels of object/array nesting, including the custom object itself.

Stackable rows merge only when all native and custom properties match,
including private data. Nonstackable grants create one row per unit, even
when their properties match. To personalize part of a stack, grant separate
rows with different custom properties. Native split clones retain their
display overrides and equality identity.

## Transfers, trading and persistence

Inventory transfer, stash deposit/withdraw and ground drop/pickup preserve
the properties. Clients receive only name/description overrides and a
server-secret-derived equality token. `custom.data` remains on the server,
including while the item is on the ground or in a stash.

`Vendor.setStock` accepts optional `metadata` on each stock row. Purchases
credit those exact properties. `vendorTrade` lines include `metadata`; sold
variants have separate event lines, preserving their data for scripted
reselling. As before, a sale does not automatically add an item to the
vendor's stock, and the native price list supports one stock row per class.

Give types intended for native vendors a positive base `price`. A zero-price
type may be missing from the shop even with a positive `Vendor.setStock`
price. The demo letter uses base price 1.

The server still does not provide automatic disk persistence. Save your
registration IDs and definition objects alongside inventory rows, then
register the saved definitions before `Inventory.set` or `stash.setInventory`.
The returned logical GUID is stable across restarts. Preserve each row's
`id`, `item`, `amount` and complete `metadata` when saving/restoring.

Native GUIDs include definition content, so the same logical ID with a
different definition on another server gets a different native class.
Disconnect clears session mappings and instance bindings. Appended native
classes and their default text remain for the process lifetime because
existing game items may still reference them.

Instance labels live inside their binding. The client checks up to 64
bindings per game tick against the native item manager, removing retired
WUIDs and obsolete class identities. Live items in any holder retain their
properties; vendor stock and split clones are reclaimed after destruction.

## Validation

Run from the KCDC repository root:

```bat
builds\build.bat RunKCDCInventoryTests 64
builds\build.bat RunKCDCWorldInventoryTests 64
builds\build.bat KCDCClient 64
builds\build.bat KCDCServer 64
```

The default gamemode provides `/customitems` to register types during play
and grant 10 timber plus independently personalized letters from Anna and
Hans. `/customitems rename` changes the first letter's display;
`/customitems defaults` restores its default text. Read either letter with
the primary action or double-click. `/customitems vendor` opens custom stock;
close and repeat it to exercise native stock retirement. Use
`/give money 100` before buying. The commands retain private sender data on
the server. Run `npm run test:customitems` in `resources/kcdc-gamemode` for the
demo's input, display-update, Read dispatch and vendor-session tests.

Nonstackable items have separate native list identities. Stackable variants
share a list identity only when their native class, public overrides and
opaque equality token match. Renaming a nonstackable item keeps its list
identity. The IDs used by Read and trade remain unchanged.

Two-client smoke checks verified live registration with readable receipt names
and icons, opening the native vendor, and all four Anna/Hans inventory rows
appearing. The vendor's Buy list showed "Letter from Hans" (3) and "Village
timber" (20), with their icons. Empty custom display metadata survived the
vendor packet round trip.

Regression tests cover identical nonstackable clones, WUIDs with matching
low bits, private-only variant differences, split updates and group cleanup.

The automated tests do not establish in-game transfer, split, rename, Read
or reconnect behavior. These cases still need validation with two matching
clients on the supported game version:

1. Register wood after joining, immediately grant 10 units, and confirm its
   name, weight, price, appearance and quantity. Split the native stack.
2. Transfer units to another player, deposit/withdraw a stash, drop/pick up
   the stack, and buy/sell configured custom stock. Verify exact quantities
   and private data in server snapshots.
3. Deliver the two letters above while the inventory is open and confirm
   their independent names immediately appear. Rename one; verify its list
   and detail text updates and the other letter stays unchanged. Drop it and
   verify its pickup prompt.
4. Verify the hint reads "Read" and both primary action and double-click
   invoke script behavior without vanilla effects or automatic consumption.
   Select and read each letter independently, then sell one and check the
   exact remaining server row and its sender data.
5. Save definitions and rows, restart, register then restore. Reconnect to a
   server changing the same logical ID's definition and check fresh visuals
   and default text. Inspect client and server registration failure logs.
