# Player stats and restoring status effects

`PlayerStat` names the four writable stats: `Health`, `Stamina`, `Exhaust`
and `Hunger`. `Exhaust` is the native name for the player's remaining energy:
higher means better rested. `Hunger` means nourishment, so a higher value
means the player is better fed.

Both client and server scripts can read stats. Only server scripts can set
them. Reads use the latest replicated snapshot and return zero when there is
no valid snapshot; check `player.ready` before treating zero as a real value.

`playerStatsChanged` reports changed resources and derived readings, including
poisoning and drunkenness. To control food and potion effects before they run,
use the [consumption events](consumables.md).

```ts
const stamina = player.getStat(PlayerStat.Stamina);
const dirtiness = player.getDerivedStat(DerivedPlayerStat.Dirtiness);

// Server only, once player.ready is true:
player.setStat(PlayerStat.Health, 80);
player.setStat(PlayerStat.Exhaust, 20);
player.setStat(PlayerStat.Hunger, 70);
player.setStat(PlayerStat.Stamina, 50);
```

`DerivedPlayerStat` is a separate, read-only enum:

| Group               | Values                                                                                            |
| ------------------- | ------------------------------------------------------------------------------------------------- |
| Conditions          | `Bleeding`, `Sleeping`, `Consciousness`, `Drunkenness`, `Poisoning`                               |
| Hygiene             | `Dirtiness`, `Bloodiness`, `Smell`, `SmellIntensity`, `Fragrance`                                 |
| Social and stealth  | `Charisma`, `Visibility`, `Conspicuousness`, `Noise`, `Morale`                                    |
| Carrying            | `CarriedWeight`, `InventoryCapacity`, `Encumbrance`                                               |
| Alcohol             | `Hangover`, `Alcoholism`                                                                          |
| Combat and movement | `ArmorRating`, `OverallArmorDefense`, `OverallWeaponAttack`, `NormalizedRunSpeed`, `RunSpeedBase` |

These are native computed values. The five conditions -- `Bleeding`,
`Sleeping`, `Consciousness`, `Drunkenness` and `Poisoning` -- follow the body
as it changes; the rest are sampled about every 250 ms, to a hundredth of a
native unit, and only a value that moved is sent. They are not all percentages and some can be negative. Equipment,
perks and buffs can affect them. Reading dirtiness does not clean the body or
equipment. There is no `setDerivedStat`; passing a derived value to `setStat`
throws. Each getter also rejects values from the other enum. The enum objects
are frozen and their members are string values.

The existing named getters remain available. `setHealth(value)` and
`setHunger(value)` remain convenience methods for their `setStat` equivalents.
Changing stamina does not stop its normal regeneration or consumption.
The current stamina maximum depends on health and energy. Restore those
first when restoring stamina; the game clamps stamina to that maximum.

## Restoring writable stats

Server scripts can read `player.health` and `player.hunger`, then restore
those values with `player.setStat(PlayerStat.Health, value)` and
`player.setStat(PlayerStat.Hunger, value)`.
Both setters can raise or lower the value. They use the game's own units,
and the client clamps each value to its current maximum. `hunger` means
nourishment: a higher value means the player is better fed.

Call the setters once the player has spawned and `player.ready` is true.
They return `false` for a missing, unready or dead body. A `true` result means
the request was sent, not that it has already happened. The getters update
when the player's client reports the result. Requests for a previous
connection or life are discarded. Both client and server need this version.

Zero health can kill the player. Raising health does not revive a dead body;
use `player.revive()` for that. Setting health leaves injuries and other
effects in place. Use `player.heal()` when those effects should be cured.
Lowering health through `setHealth` or `setStat(PlayerStat.Health, value)`
raises `playerDamage` with a null attacker and the amount of health removed.
A fatal change also raises `playerDied` with a null killer. Restoring a lower
saved health value follows this same path; damage handlers must not assume
every event came from an attack.

Invalid arguments throw: the value must be a finite number between
0 and 1,000,000. The upper bound is a protocol limit, not a gameplay maximum.
Positive health must remain greater than 0.00001 after conversion to the
native float; the game ignores health inputs at or below that threshold.
Positive stat values that would round to zero also throw. Zero itself
is valid for all four writable stats. Native changes within 0.00001 of the current
value may be ignored by the game.

## Example server resource

This stores health and nourishment across rejoins while the resource stays
loaded. Store the same data in your database to preserve it across restarts.
It saves only living bodies and discards any older record when a dead or
unready player leaves. `player.ready` alone does not mean the player is alive;
restoring zero health would kill the newly spawned body again.

```ts
import type {} from "@kingdomsconnected/types/server";

const saved = new Map<string, { health: number; hunger: number }>();
// Cache identity while connected: a kick can clear it before playerDisconnect.
const accounts = new Map(Player.all().map((player) => [player.id, player.steamId]));

Events.on("playerConnect", (player: Player) => {
    accounts.set(player.id, player.steamId);
});

Events.on("playerDisconnect", (player: Player) => {
    const account = accounts.get(player.id);
    accounts.delete(player.id);
    if (!account) return;
    saved.delete(account);
    if (player.ready && player.alive) {
        saved.set(account, {
            health: player.health,
            hunger: player.hunger,
        });
    }
});

Events.on("playerReady", (player: Player) => {
    const state = saved.get(player.steamId);
    if (!state || !player.steamId || !player.ready || !player.alive) return;
    player.setStat(PlayerStat.Hunger, state.hunger);
    player.setStat(PlayerStat.Health, state.health);
});
```

Use your server's authenticated account ID instead of `steamId` if it has its
own account system. Do not use the connection's network ID as a persistent key.

## Poison, bleeding and injuries

These are status effects, already exposed through `player.buffs`,
`player.addBuff(name)`, `player.removeBuff(name)` and `player.clearBuffs(tag)`.
Save the names of the effects your game mode wants to carry across rejoins,
then reapply them after the player is ready. Cache the names while connected,
updating the snapshot on `playerBuffAdded` and `playerBuffRemoved`. The buff
report has already been cleared when `playerDisconnect` runs, so reading
`player.buffs` there would lose the effects:

```ts
const effectNames = player.buffs
    .filter((buff) => buff.class === "poison" || buff.class === "injury" || buff.aiTag === "bleed")
    .map((buff) => buff.name);

// After rejoining, using effectNames loaded from your saved record:
for (const name of effectNames) {
    if (!player.hasBuff(name)) player.addBuff(name);
}
```

Reapplying an effect starts it using the game's normal rules. This does not
restore its exact remaining duration, stacked strength or elapsed progress.
Not every effect can be recreated by name alone. In live tests, `food_poison`
did not remain active, and `bleeding` alone did not produce bleeding strength.
`deadly_poison` and `test_bleeding` survived health changes and were reapplied
after reconnecting, with `test_bleeding` restoring bleeding strength 1. Check
the effects your resource saves in-game; do not treat a successful `addBuff`
request as proof that the effect took hold. `deadly_poison` was present while
the `poisoning` getter remained zero, so use `hasBuff` to check that named effect.

`buff.duration` is the definition's duration and `buff.since` is how long the
server has observed it, not a saved native timer. The numeric `poisoning` and
`bleeding` getters are derived from the effects and remain read-only.
