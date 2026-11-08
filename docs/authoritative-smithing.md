# Server smithing: how it works

The server owns every material and every product of smithing. A player
works at the game's own smithery as in single player: the forge, the anvil,
the strikes and the quench all stay native and run on the owner. What the
minigame would do to the inventory, the server does instead:

- it takes the materials when the player presses Craft;
- it decides what the finished workpiece grants, and grants it.

Nothing is stored. A workpiece belongs to one connection. It lives in
`SmithingModule` (`code/server/src/features/crafting/smithing_module.h`),
owned by `CraftingService` beside alchemy. Policy is the scripts' job: who
may smith where, whether a result stands, what to save. See
[smithing-api.md](smithing-api.md).

## What the server does not simulate

The minigame scores each strike from its coverage of the workpiece.
A server without the geometry cannot recompute it. The one value the owner
reports is therefore the workpiece quality the minigame reached. It can only
choose among the recipe's own authored products:

- a quality below the lowest product threshold is a failure;
- a quality outside 0..1 is a failure.

A `craftingCompleting` handler sees the quality and the product, and can
refuse them.

Strike and finishing effects on the character stay where they already are:

- Strike XP reaches the server through the ordinary XP requests and
  `playerXpGaining` (see [player-progression.md](player-progression.md)).
- Stamina use during work stays native.

## Beginning a workpiece

Pressing Craft sends the selected recipe to the server with `SmithingBegin`.
The recipe screen stays open until the server answers.

The server admits the workpiece when all of these hold:

- **Station:** the player is within 6 m (`kUseDistance`) of a catalogued
  smithery in their level, or one `SmartObject.spawn` placed in their
  virtual world or the global one, and can act. A catalogued smithery a
  level edit took out of their world does not count
  ([World objects](world-objects.md#stations)).
- **One at a time:** the player has no other workpiece and holds no
  alchemy table, and nobody else is working at that smithery in that
  virtual world.
- **Recipe:** it is in the catalog and the server's DLC policy allows it.
- **Recipe perk:** the player owns the recipe's unlock perk, per their
  accepted progression.
- **Skill:** effective craftsmanship (skill 8) meets the recipe minimum.
- **Materials:** the inventory holds every material in units the body
  does not wear. Worn units are never taken, and rows are spent oldest
  first.
- **Script:** `craftingStarting` does not refuse.

It then removes the materials from the inventory (reason `smithing`) and
answers with a session. The client replays the Craft callback on its next
frame. It does so only onto the same minigame and the same selected recipe;
anything else is reported as abandoned.

The skill counts the player's level plus the permanent perk contributions the
server can evaluate. A perk's own buff counts only through the perk in the
accepted progression; a report of it without the perk is ignored. Swordmaker's Wisdom (`Cpp:Blacksmithing`, "counts as 3
levels higher during blacksmithing") is counted. Equipment weighting and
timed potions are not simulated by the server, so they do not count towards
the minimum.

## Tools

The smithery lends its tools for the workpiece. They are not the player's
items and do not count towards the server inventory. The loan ends when
the tool goes back or the session ends.

`tools/generate_crafting_catalog.py` compiles the tongs and hammer slots
linked by each smithery into `g_CraftStationTools`. Those slots keep
spawning while ordinary level loot stays disabled.

## Materials

The server settles the materials once:

- **Quest materials** (`IsQuestItem`) must be held, and are never taken.
  No server inventory can hold one -- the game cannot create a quest item,
  so the inventory refuses it (see
  [inventory-authority.md](inventory-authority.md)) -- and a recipe that
  needs one is refused for missing materials.
- **Success:** every unit taken is spent.
- **Failure** (broken at the anvil, cancelled, left, disconnected, timed
  out, or refused by a script):
  - A divisible material (`IsDivisible`) loses
    `trunc(required × 0.5)` units. When that rounds to zero, a server-drawn
    coin decides a single unit. The coefficient is
    `wh_pl_BlacksmithReturnIngredientCoef`, which the native body removes
    rather than returns.
  - Every other material comes back whole.
- **Abandoned** within 5 s of admission: everything comes back. This covers
  a recipe that never reached the anvil. An abandon later than that is a
  failure.

Before an unfinished workpiece settles, a
[refund policy](crafting-refunds.md) can request all its original materials
back instead of the native loss. Returned units go back with the properties
they were taken with.

## Finishing

The client reports `SmithingFinish` with the outcome and quality. The
server grants the product, finishing XP and lockpicks.

A report is ruled on as the player stands when it arrives: one from out of
reach, another world or a dead player fails the workpiece. One that arrives
after the 30-minute deadline fails it as `timeout` and is answered
`SessionExpired`. A minigame that shuts down without a result, or whose
body goes away mid-work, is reported as `Interrupted` and settled with
the failure share. A normal exit is reported as `Cancelled`; see
[crafting-refunds.md](crafting-refunds.md#compatibility).
An approval that arrives after its press gave up is answered as abandoned.

A success grants, in the native arithmetic:

- **Product:** the highest threshold the truncated percent reaches.
  A quest product is not created; its quest
  creates it.
- **Tier:** the workpiece health above the critical threshold is
  remapped to 0..1 and mapped to a tier with overlapping bands. The tier is
  held at 3 without soul ability 65.
- **Condition:** health within the tier, remapped into
  `BlacksmithingMinCondition`..`MaxCondition` (0.1..0.8). A horseshoe keeps
  full condition.
- **Hardened Steel:** with ability 87, a weapon gets the
  `e82de782` on-equip buff.
- **Lockpicks:** 1..n of `8d76f58e`, where n is derived stat 189 (`lfb`,
  base 0; `perk_locksmith_lockpicks` authors `+3`). The server draws the
  number.
- **Finishing XP:** `(int(q·128+0.5) × int(135·128+0.5)) >> 7` in 1/128
  units, on the craftsmanship track through `Progression::AddXp`. The
  owner's multipliers apply on top.

## Ending on the server's side

A script cancel, the 30-minute lifetime, walking out of reach, dying and
changing world all fail the workpiece and send `SmithingClosed`. The client
then cancels the native minigame without reporting the result again.
A disconnect fails the workpiece before `playerDisconnect`, so the returned
units are in the inventory a script saves there.

## Protocol

Typed messages in `code/shared/features/crafting/smithing_rpc.h`:

| Message           | Direction      | Carries                                                                                             |
| ----------------- | -------------- | --------------------------------------------------------------------------------------------------- |
| `SmithingBegin`   | owner → server | request token, station GUID, recipe id                                                              |
| `SmithingVerdict` | server → owner | request token, session, `SmithingCode`                                                              |
| `SmithingFinish`  | owner → server | session, `SmithingOutcome` (`Success`, `Failure`, `Abandoned`, `Cancelled`, `Interrupted`), quality |
| `SmithingClosed`  | server → owner | session, `SmithingCloseReason`                                                                      |

## Tests

The smithing suite in `RunKCDCTests` covers:

- the character evaluation (perks, skill, lockpick limit);
- recipe admission;
- product, tier, condition, Hardened Steel and XP;
- the failure share and its coin;
- every admission refusal and the material take;
- success, failure, a script veto and a quality below threshold;
- prompt and late abandons;
- walking away, timeout, disconnect, a refused quest-material recipe and a
  graded quest product;
- the `craftingRefunding` policy: one call per exit carrying the held
  materials and its exact reason, re-entry refused while it runs, server
  closures keeping the native loss, and a full return when requested;
- every smithery's tongs and hammer slots in the station catalog.
