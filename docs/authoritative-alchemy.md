# Server alchemy: how it works

The server rules on every alchemy table in memory. A player brews at a
native table exactly as in single player; every native input is held on the
client, asked of the server, and replayed only once the server agrees. The
server derives what the input means, how long its authored animation takes,
what happens to the pot's heat, and at the end which potion, how many, at
what quality and for how much XP. The client never names an outcome.

Nothing is stored. A session belongs to one connection: it lives in
`AlchemyModule` (`code/server/src/features/crafting/alchemy_module.h`),
owned by `CraftingService`, and ends when the player leaves the table,
disconnects, times out or is cancelled by a script. Policy -- who may
brew where, whether a result stands, what to save -- is the scripts'. See
[alchemy-api.md](alchemy-api.md).

## Sessions and tables

One session per player and one per table. A table is keyed by its station
and the virtual world, so the same placement in another world is another
table. The station is one the level authors, or one `SmartObject.spawn`
placed in the player's world or the global one; a level station a level
edit took out of the player's world refuses new sessions
([World objects](world-objects.md#stations)). Starting needs the player
within `kUseDistance` (3.25 m) of the station, able to act, and a character
the server can evaluate (see [authoritative-soul.md](authoritative-soul.md));
`craftingStarting` can refuse it.

A fresh entry waits for the table's opening animation (`alchemy_place_book`)
before the `entryready` intent is accepted. That moment captures the entry
quality bonus (derived stat 184) once per visit. A finished batch keeps
the table for the same player:
`start` with `continuationOf` naming the batch that just finished opens the
next one at once, without another opening, and with the bonus the visit
captured. The table is released when the player leaves it, when a script
cancels, on disconnect, and when a session's 30-minute deadline passes or
the player walks away, dies or changes world (`contextInvalidated`).

Every action carries the next sequence number and is refused before the
previous action's authored animation has played (`alchemyActionTooEarly`;
the response says how long to wait in `retryAfterMs`).

## Ingredients

An ingredient leaves the player's inventory when it is taken to the table:
the take names the inventory row, and the server removes one unit from it
(reason `alchemy`). The unit on the table keeps its item class and canonical
properties, and herb condition decides its fresh, dried or decayed quality.
Base liquids come from the station and never touch the inventory.

Unless a [refund policy](crafting-refunds.md) requests all original
ingredients back for an unfinished batch, what comes back follows the native
reset:

- An explicit Back on an untouched ingredient in hand returns it.
- When a session ends for any reason, every wholly unmilled group in the
  bowl, the mortar or a herb hand is refunded. A group with one milled
  member is spent whole.
- Everything else -- the pot, the retort, the flask, the special-ingredient
  hands -- was spent when it went in.

A refund is an `Inventory::Add` of the unit's class and properties, so it
may merge into a row other than the one it came from.

## Heat and timing

The server simulates the pot on its own clock in 50 ms steps with the
native heating rules (`AlchemyThermal::Step`), crediting each ingredient in
the pot with weak and strong boiling time. Pot-on-fire changes take effect
at the kettle motion's authored events, transfers at the clip's own detach
markers. Action durations are the authored clip lengths at scale one; a
mill includes picking up the mortar, the last bellows pump of a distillation
includes the finish clip.

## Results

Finishing from the flask, from the mortar, or the last distillation pump
stages the result; `complete` settles it once the finish animation has
played. The arithmetic is the native one: the recipe program match with
the character's boiling tolerance, step quality against the allowed
mistakes, the base and ingredient quality weights, the score modifier
(74), the entry bonus, the product rank and Secret of Secrets, Bundle
Alchemist and the yield modifier (73) floored, Experimental Powder's item
buff on bombs, and XP rounded to 1/128. A brew that matches no recipe gives
the generic failed potion.

`craftingCompleting` sees the frozen result first. A refusal ends the batch
as failed: what was spent stays spent, nothing is granted, and the table is
released. Otherwise the output is added to the inventory, the base XP is
ordered on the alchemy track through `Progression::AddXp` (the owner's
engine applies its own XP multipliers), and a successful recipe is marked
fully known.

## Recipe knowledge

The module holds each connected player's known-steps mask per recipe. It
starts empty every session; scripts restore it with `Crafting.setKnowledge`
and read it back from `Crafting.knowledge` or `craftingCompleted`. The owner
receives it in the `character` object and answers the game's knowledge
queries from it while at the table. The table book lists a recipe only when
at least one conditional step is known.

## Protocol

`KCDC::AlchemyRequest` carries one JSON intent (at most 8 KiB);
`KCDC::AlchemyResponse` carries the answer. Requests are parsed by
`ReadRequest` in `code/shared/features/crafting/alchemy_protocol.h`:
`start`, `entryready`, `action`, `return`, `pulse`, `complete`, `cancel` and
`inspect`. `cancel` may carry `reason` (`cancelled`, `interrupted` or
`clientError`, as the client classified the native exit; absent means
`cancelled`), which `craftingEnded` and `craftingRefunding` report. Every
response has this shape:

```
{ "request": "<echoed token, or empty for a push>",
  "op": "<echoed op, or character | closed for a push>",
  "ok": true, "code": "",
  "session": "alchemy-12", "sequence": "4",
  "inventoryRevision": 57,
  "state": { "resources": [ { "id": 2, "item": "<GUID>", "itemId": "<row, empty for a base>",
                              "position": 6, "base": false, "milled": false, "distilled": false } ],
             "held": 21, "onFire": false, "terminal": false, "pendingTerminal": false,
             "readyAtMs": 123456, "tableClosed": false, "outcome": null,
             "retiredResourceIds": [],
             "distillation": { "requiredPulses": 2, "completedPulses": 1 } },
  "character": { "skill": 15, "relativeSkill": 0.5, "sessionQualityBonus": 0,
                 "abilities": { "19": false, "30": false, "63": false, "92": false },
                 "knowledge": { "16": 536870911 } } }
```

- `state` is present whenever the player has a session or has just ended
  one. `distillation` only while a retort is being pumped. A staged finish
  has `terminal` and `pendingTerminal` true and no resources; an ended
  batch has `terminal` true, its `outcome`, and `tableClosed` true unless
  the table is kept for the next batch.
- `character` accompanies `inspect`, `start`, `entryready` and `complete`,
  and a `character` push after a script changes knowledge. No push is sent
  before the player's first progression digest; the next response carries
  the knowledge instead. Its `sessionQualityBonus` is the visit's captured
  bonus while one is held.
- A `closed` push (`request` empty) tells the owner the server ended the
  session: a script cancel, a timeout or `contextInvalidated`, named in
  `reason`.
- `inventoryRevision` is the player's inventory revision after the request.
  The client holds its inventory projection until it has synced that
  revision, so the native table move and the row change land together.
- `readyAtMs` is on the server's own clock and only orders one state
  against another; `retryAfterMs` accompanies `alchemyActionTooEarly` with
  the wait in milliseconds.

Refusal codes include `alchemyCharacterUnavailable`, `alchemyNotAuthorized`,
`playerBusy`, `stationBusy`, `staleAlchemyTable`, `craftingStartingRejected`,
`unknownAlchemySession`, `alchemyEntryPending`, `invalidAlchemyEntryPhase`,
`invalidAlchemySequence`, `alchemyActionTooEarly`, `alchemySessionExpired`,
`alchemyTerminalPending`, `invalidAlchemySelection`,
`invalidAlchemyIngredient`, `alchemyIngredientCannotReturn`,
`invalidAlchemyTerminal`, `craftingCompletingRejected`,
`alchemyCompletionSuperseded` and `sessionNotOwned`.

## Tests

The alchemy suite in `RunKCDCTests` covers the module against a real in-memory
`Inventory`: taking and returning, the refund groups on cancel, a staged
finish and its timing, the output, XP and events of a completion, a
continuation keeping the entry bonus, a refused result, one session per
table and player, sequence and distance refusals, expiry, and knowledge.
It covers the `craftingRefunding` policy: processed ingredients restored
but never the station's water, re-entry refused while it runs, server
closures keeping the native loss, a duplicate cancel granting nothing, and
the cancel `reason` carried through to `craftingEnded`. It also covers the
character evaluator, the protocol parser and its cancel reasons, the thermal
step, the recipe catalog and the entry continuation.
