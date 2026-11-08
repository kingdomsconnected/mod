# Food, potions and poison

Food and potion use waits for the server before consuming an inventory item
or applying its effects. Cooking pot servings follow the same decisions.
Both client and server must run this version.

Server resources receive these events in order:

1. `playerConsuming(player, consumption)` runs before reserving stock.
   Return `false` to refuse the meal or drink and preserve the item.
2. `playerConsumptionEffects(player, consumption)` runs after use is allowed.
   Return `false` to consume without native effects. This suppresses nutrition,
   energy, alcohol, potion buffs, spoilage effects and added pot poison.
3. `playerConsumed(player, consumption)` runs once when the client confirms
   successful native use. Apply replacement stats and buffs here.

Veto handlers must return synchronously. An async function cannot veto.
Handlers receive a snapshot of the server's serving. Changing that snapshot does not change the serving or its effects. An inventory
restore, body replacement or world change during a decision invalidates it.
Changing a pot's contents during its decision also invalidates that serving.

`Consumption` includes the source (`inventory` or `cookPot`), item class GUID,
kind (`food` or `potion`), inventory row or pot GUID, item metadata and any
additional pot poison. `baseEffects` describes the native table's buff GUID,
health, energy, nourishment and alcohol inputs. These values precede item
condition, perks and soul modifiers; health changes can happen over time.
They are not final stat deltas. Potion classification uses the native food
type, rather than searching the item's name.

`nativeEffects` is the final decision in `playerConsumed`. With no vetoes,
native food and potion calculations continue to run normally. Server scripts
can use `player.addBuff`, `removeBuff`, `clearBuffs`, `setStat`, `setHealth`
and `setHunger` to implement their own rules.

For example, this server replaces Marigold's native effects with one heal:

```ts
const marigold = "b38c34b7-6016-4f64-9ba2-65e1ce31d4a1";

Events.on("playerConsumptionEffects", (_player, consumption) => {
  if (consumption.item === marigold) return false;
});

Events.on("playerConsumed", (player, consumption) => {
  if (consumption.item === marigold && !consumption.nativeEffects) {
    player.setHealth(Math.min(player.maxHealth, player.health + 15));
  }
});
```

Do not grant replacement effects in a pre-use event: the native action can
still fail. Stat setters are asynchronous instructions; values read from a
player remain the latest client report until the next update. A game mode
running its own timed effects should keep its own effect state and cancel
timers when the player disconnects or changes body.

## Poison and stat observations

For added cooking pot poison, `playerPoisonAbsorbing(player, consumption,
buff)` runs before applying the poison on the eating receipt. Returning
`false` suppresses that poison while leaving the approved meal intact.
`buff` is the native buff name accepted by `player.addBuff`. It does not fire
when `playerConsumptionEffects` suppressed the entire effect bundle.

An item's own potion buff and spoilage effects belong to the native bundle.
Use `playerConsumptionEffects` to replace that bundle. `Buffs.claim` and
`playerBuffBlocked` remain available when a game mode needs to decide whole
buff classes, including effects originating outside food consumption.

`playerStatsChanged(player, changes)` reports changed resources and derived
readings, including health, energy (`exhaust`), nourishment (`hunger`),
poisoning, drunkenness and hangover. Each entry has `stat`, `previous` and
`current`. Changes are batched per server tick; initial reports and new bodies
establish a baseline. `playerBuffAdded` and `playerBuffRemoved` continue to
report individual status effects.

Stat-change events are observations and cannot be cancelled. Reports can
combine intermediate native changes and do not identify the cause. To own a
poison's damage progression, suppress its native application and run the
effect in server code. Native effect timers still run on the owning client;
this change does not make the server simulate the native RPG system.

## Reservations

The server reserves one inventory food or potion unit before replying.
Duplicate requests and receipts cannot spend or apply it twice. The client
keeps that native unit until the answer, then runs the native use once. A
reported native refusal refunds the original class and properties. If stock
has filled the inventory meanwhile, the refund waits for space. An explicit
inventory restore supersedes previous reservations and refunds.

A missing receipt or a receipt after the ten-second deadline does not refund
stock: the server cannot know whether the client consumed it. A late approval
never runs on a replacement body or in another world. Successful inventory
uses also raise `playerItemUsed`; refused reservations do not.

These checks validate the serving and the server decision. As with the
existing inventory and buff protocols, the owning client reports whether the
native action ran and what stats its body has.
