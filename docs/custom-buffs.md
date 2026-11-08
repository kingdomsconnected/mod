# Custom buffs and live progression editing

`Buffs.register(id, definition)` returns a GUID, just like `Items.register`.
The GUID and the registered ID both work with `player.addBuff`,
`player.removeBuff`, `player.hasBuff`, `Buffs.find`, and `player.buffs`.
The ordinary buff events retain their existing meaning.

```js
const blessing = Buffs.register("my_resource:warriors_blessing", {
    name: "Warrior's Blessing",
    description: "Increases strength and stamina.",
    visual: "potion_marigold_decoction",
    duration: 300,
    tickInterval: 5,
    modifiers: { strength: 5, stamina: 20 }
});
player.addBuff(blessing);

Events.on("playerCustomBuffTick", (player, buff) => {
    if (buff.name === "my_resource:warriors_blessing") {
        // Scripted effects can supplement the native modifiers.
    }
});
```

The visual donates its icon and presentation. Its potion, poison, perk,
exclusivity, family, and gameplay implementation are not inherited. Custom
effects use the native `BasicTimed` factory, modifier evaluator, active buff
index, and status-effect UI. Names and descriptions resolve through the
existing localization hook. The native UI uses the instance's remaining time.

## Definitions and lifecycle

IDs contain 1–128 ASCII letters, digits, `:`, `_`, `-`, or `.`. Use a resource
prefix to avoid collisions. Registration is immutable and idempotent: the
same ID and definition returns the same GUID; a different definition throws.
Definitions are published to connected clients and to future connections.
Register them during resource startup. They remain registered for the server
process; there is no unregister operation.

Names are 1–256 UTF-8 bytes; descriptions are optional, up to 4096 bytes.
Control characters, unknown properties, unsupported modifiers, invalid
numbers, and native ID collisions are rejected. There are at most 512
definitions per server lifetime. Native rows use a content-derived identity
and remain stable across reconnects, so live native instances never borrow a
replaced row or a string from the mod's allocator.

`duration` is positive real seconds, up to seven days, or `-1` for permanent.
Its default is `-1`. Permanent means until removal or session/body teardown;
effects are not written into game saves or automatically restored on join.
Native timers use the owner's simulation clock, including its pauses.

`stackable` defaults to false; `maxStacks` defaults to 1 and is an integer
from 1 to 16. Every stack is a separate native instance with its own timer.
At the limit, `refresh: true` replaces the oldest instance with a full-duration
one. With `refresh: false`, the additional application is ignored. Removing a
registered buff removes every stack. `player.buffs` has one row per instance.
Custom applications are ignored if the body already fills the existing
96-effect report limit.

Custom lifecycle events carry `BuffInfo`. Its `name` is the registered ID;
`displayName` and `description` carry the custom text, and `custom` is true.

- `playerCustomBuffApply(player, buff)` fires for each observed new instance,
  including refreshes and stacks.
- `playerCustomBuffTick(player, buff)` fires at `tickInterval`, on the server
  clock, while the owner's last report contains that instance. Zero disables
  callbacks; otherwise the interval is 0.1–86400 seconds. Missed ticks are
  skipped, with at most one callback per instance per server update.
- `playerCustomBuffRemove(player, buff, reason)` fires for each instance.
  Reasons are `server`, `expired`, `refreshed`, and `disconnect`. `expired`
  also covers other native removals.

An `addBuff`/`removeBuff` return of true acknowledges sending an instruction.
Read the reported state or observe lifecycle events to confirm application.
Ordinary `playerBuffAdded`/`playerBuffRemoved` still describe presence of the
definition, so refreshing it does not create another legacy added event.

## Modifiers

`Buffs.modifiers` lists supported names. A number adds points. An explicit
`{ operation: "add" | "multiply" | "set", value: number }` selects the
operation. Add values are from -10000 to 10000; multiply values are from 0 to
100; set values are from 0 to 10000. All values must be finite. Native query
contexts and bounds still apply; a stat/skill cannot exceed the native cap.

| Name                            | Meaning                                                                                                |
| ------------------------------- | ------------------------------------------------------------------------------------------------------ |
| Progression track names         | Effective strength, agility, vitality, speech, or skill; earned XP remains unchanged                   |
| `charisma`                      | Effective charisma                                                                                     |
| `stamina`, `maxStamina`         | Maximum stamina; these are aliases and cannot appear together                                          |
| `carryCapacity`                 | Inventory capacity                                                                                     |
| `runSpeed`                      | Native base run speed                                                                                  |
| `armorRating`, `bodyArmor`      | Native armor rating or base body armor                                                                 |
| `hungerDrain`, `energyDrain`    | Native digestion and exhaustion modifiers                                                              |
| `staminaRegeneration`           | Native stamina regeneration modifier                                                                   |
| `weaponAttack`, `weaponDefense` | Native weapon attack and weapon defense adjustments (`wat`, `wde`), read by the hit calculation        |
| `attackSpeed`                   | Native attack speed adjustment (`asp`): the swing speed multiplier, as the broken-arm injury lowers it |
| `moveSpeed`                     | Native real move speed modifier (`rms`), which the game's horseshoes carry beside `runSpeed`           |
| `courage`                       | The courage stat, which decides whether a horse throws its rider and whether an NPC flees              |
| `healthRegeneration`            | Additive health per simulation second; only `add` is accepted                                          |
| `maxHealth`                     | Health capacity; `set` requires at least 1                                                             |

For example, a permanent rule can use
`hungerDrain: { operation: "multiply", value: 0.5 }` and the same value for
`energyDrain`. This changes the native drain calculations rather than
periodically overwriting nourishment and energy.

Native `multiply` scales the original base: two independent 1.1 multipliers
contribute +20% of that base, rather than compounding to +21%. Operations run
in native order. Permanent duration and the native long-term query reason are
different concepts: custom modifiers are short-term and do not change the
base values used by long-term or base-only queries.

Health capacity is a deliberate extension of the existing `GetStateMax` hook:
the game reads a fixed constant for maximum health and offers no native
modifier key for it. Additions and original-base scales precede assignments;
the result is bounded to 1–1000000. Lowering capacity clamps current health;
increasing it or removing the effect does not heal. Observers use the owner's captured
maximum, so they do not run another buff simulation.

There is no generic scalar for the internals of specialized existing effects,
such as drunkenness or hangover severity. Custom numeric effects can use the
operations above, and lifecycle callbacks can implement additional behavior.
Borrowing an icon does not scale or copy those specialized effects.

## Horses

`horse.addBuff`, `horse.removeBuff`, `horse.hasBuff`, `horse.clearBuffs` and
`horse.buffs` take the same names as the player verbs. A horse's stats are
its soul's derived stats -- speed is `RunSpeedBase`, with stamina, courage
and carrying capacity beside it -- and the game moves them with buffs:
horseshoes are `rms+0.04,rsb+0.04`, the race boosts `mst+30,rms+0.01,rsb+0.01`,
horse plate `courage+5,mst+10`. So `runSpeed`, `moveSpeed`, `stamina`,
`courage` and `carryCapacity` are the horse modifiers.

Unlike a player's buffs, a horse's are state on the entity rather than an
order to one client: every copy of the horse puts them on its own soul, so the
simulator's speed, every observer's copy, a late joiner's and a new owner's
agree, and a body re-spawned after a level change is given them again. A
listed effect lasts until it is removed, whatever its `duration`: a copy whose
effect ran out, or that the game took off, is given it again within a second.
A horse carries at most sixteen. Effects the game gives the horse itself are
not on the list and are not touched: a listed effect the horse already wears
from its gear is left to the gear, and removing it from the list does not take
the gear's off.

```ts
const shoes = Buffs.register("stable:racing_shoes", {
    name: "Racing shoes", visual: "item_horse_shoe", duration: -1,
    modifiers: { runSpeed: 0.08, moveSpeed: 0.08, stamina: 40 },
});
horse.addBuff(shoes);
```

## Exact progression assignments

`player.setXp(track, xp)` assigns cumulative XP since level zero, quantized
down to 1/128 XP. It can change both level and XP inside a level, including
zero. `player.removeXp(track, xp)` subtracts unscaled cumulative XP from the
accepted state plus pending orders, clamping at zero. It requires the first
progression report. `player.setLevel(track, level)` now accepts reductions
and clears XP inside the selected level.

Reductions write the proven native progression records, notify native
property listeners, and recompute induced skills. They do not revoke perks
or unspent points. Positive exact assignments populate and finalize native
XP notification data before deferred event dispatch, retaining native
level-up notifications and point grants without applying XP multipliers to
the requested absolute value. `addXp` retains normal native earning behavior.
Lowering XP does not revoke already earned perks or perk points. Use
`removePerk` or an explicit progression snapshot to change those too.

`restoreProgression` validates its entire input before sending any orders.
Supplied tracks are restored exactly, supplied perks replace the reported
perk set, and supplied point banks are assigned after progression events
drain. Omitted sections remain unchanged. Fencing is recomputed from weapon
skills and cannot be assigned as earned progression.

The progression order identifier is explicitly versioned as
`KCDC::ProgressionOrder/v2`. Deploy matching updated clients and server; do
not mix these changes with older binaries.

## QA

Build from the Framework root:

```powershell
$previous = $env:FW_CMAKE_ARGS
try {
    $env:FW_CMAKE_ARGS = ($previous + ' -DKCDC_AGENT_TESTING=ON').Trim()
    .\builds\build.bat KCDCClient 64 Debug
    .\builds\build.bat KCDCLauncher 64 Debug
    .\builds\build.bat KCDCServer 64 Debug
    .\builds\build.bat RunKCDCTests 64 Debug
} finally { $env:FW_CMAKE_ARGS = $previous }
```

From the KCDC root, run `tools/start_custom_buff_qa.ps1 -Clients 2`. It
prepares the bundles under `builds/build-64/custom-buff-qa-<timestamp>`, stages
them into the permanent `server`, `client-a` and `client-b` QA slots through
`scripts/kcdc-qa-slots.ps1` (see [kcdc-qa-runtime.md](kcdc-qa-runtime.md)),
binds a loopback server on port 27731, launches `BuffQA_A` and `BuffQA_B` from
those slots, records session metadata and binary hashes, and tiles their
agent-testing windows. Wait if another run holds a slot. Check each title and `Agent testing graphics` log
line before relying on the preset.
Use `-ServerOnly` to smoke-test registration and resource startup without
launching a client.

The automatic scenario checks live level reductions, within-level XP
reductions, exact restoration in both directions, modifiers, metadata,
refresh, tick and expiry events, stack limits, removal of every stack,
health regeneration, maximum-health clamping, and carrying capacity. It also
verifies exact XP assignment while a native XP bonus is active.
Each server runtime writes
`buff-qa-<nickname>.json`, including individual checks and lifecycle events.

After the automatic checks, enter `/buffqa ui` in chat and open the game's
character/status-effect panel. Verify both custom titles and descriptions,
the borrowed marigold icon, a decreasing five-minute timer, and the permanent
effect. Use `/buffqa status` for reported values and `/buffqa clear` to remove
the effects. `/buffqa hunger` enables half drain for a comparison over equal,
unpaused periods. Use `-Manual` to leave automatic checks off, and
`/buffqa auto` to trigger them yourself.

Stop only the recorded test processes with
`tools/stop_custom_buff_qa.ps1 -SessionPath <returned session.json>`. It waits
for the ports to free, copies the reports and logs from the server slot into
the run's folder, and releases the run's slots.
For a manual run with a reusable cleanup path:

```powershell
$sessionPath = .\tools\start_custom_buff_qa.ps1 -Clients 1 -Manual
# In game: /buffqa auto, then /buffqa ui and /buffqa clear.
.\tools\stop_custom_buff_qa.ps1 -SessionPath $sessionPath
```

Before producing a normal client from this cached build tree, rebuild with
`-DKCDC_AGENT_TESTING=OFF`. Never ship the test binaries.
