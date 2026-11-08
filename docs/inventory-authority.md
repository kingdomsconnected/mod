# Server inventory

The server holds every connected player's items in memory. The game's own
inventory on the player's machine shows them; it is not a source of items.
Nothing is stored: a player's inventory is created empty when they connect
and dropped after `playerDisconnect`. Keeping inventories between sessions
is a server script's job, done with the scripting API
([Inventory scripting API](inventory-scripting-api.md)).

## Server

`InventoryService` owns one `Inventory` per connection
(`code/server/src/features/inventory/`). An inventory is a set of rows, each a
stack of one item class with one set of properties, and a revision that
advances with every change.

- Every operation -- add, remove, take, set properties, restore, transfer,
  wear -- validates in full and then applies in full. A refusal changes
  nothing. There is nothing uncertain to recover from, so there are no
  transaction ids or receipts; the optional expected `revision` is how a
  script refuses to act on state that moved.
- Properties go through the shared property policy on every write, so the
  stored metadata is canonical and equal items are equal. A grant or a
  transfer merges into the row that already holds that kind of item.
- An inventory is bounded at 4096 rows and 448 KiB of encoded rows, so the
  whole of it always fits one sync message.
- Row ids are unique within an inventory. The server makes new ones from a
  process-wide counter; a restore keeps the ids the script saved.

Every change is recorded and raised as `playerInventoryChanged` on the next
tick, after all changes of that tick are collected, so a handler that changes
an inventory never re-enters the one being changed. Changes of a player who
disconnects are raised before `playerDisconnect`, which can still read the
inventory.

## Sync

The owner's client is sent its inventory whole when it finishes loading and,
after that, once per tick at most, only the rows that changed since what it
last received (`shared/features/inventory/inventory_sync.h`). Messages travel
reliable and ordered, so a delta always meets its base. A client that loses
its place -- it could not apply a delta, or its queue overflowed -- asks for
the whole inventory again. Other players are sent nothing.

The client acknowledges each revision it projected onto the game. The first
acknowledgement raises `playerInventoryReady`; `ready` in `Inventory.get`
says whether the current revision is shown.

The client sends two kinds of message, both small and both rate limited per
connection (a token bucket that drops, and a looser one past which the client
is disconnected): acknowledgements, and wear reports. A message whose length
field claims more than the packet carries is refused before anything is
allocated for it.

## Client projection

`NativeInventoryProjection` makes the native inventory hold exactly the
server's rows. It keeps each native stack that holds rows the server still
has, or that matches a kind of item the server has room for -- so a stack
already there on join, or after the game rebinds the actor, is adopted
rather than removed and recreated. It removes the rest and creates what is
missing. `CreateItems` can report failure after adding the stack, so the
projection counts what changed instead of trusting the return, and never
calls it twice for one kind in a single pass. Only item ids are kept between
frames.

A row the game cannot show is left out on its own, and every other row is
still projected. That covers a row the client cannot read (an unknown class,
properties it refuses) and a kind `CreateItems` will not make as asked --
too few, other properties than asked, or merged into a different stack. What
that call added is taken back out, so a stack it merged into keeps what it
held before. The client logs each newly left-out row with its class and
reason (`Inventory row ... could not be shown`), tells the player in chat,
and names the rows in its acknowledgement as `unshown`; the server logs each
once (`Player N could not show row ...`). The revision still counts as
acknowledged: the client spends only from stacks it shows, so a left-out row
cannot be used, and one bad row does not hold every gather, pickup, stash and
trade. A kind that failed is not created again until the server's rows of it change
or the body is rebound. Containers project the same way.

If a projection fails as a whole -- its own bookkeeping could not be kept
consistent -- the client retries after 250 ms, then 1 second, then 3
seconds. Each pass reconciles against what the game actually holds, so a
previous attempt that created an item is counted before anything else is
created. A new inventory sync or a rebound native body resets the retry
budget. Crafting and consumption holds still defer projection. After four
attempts the client logs the failure and stops retrying; it never
acknowledges a revision it could not project.

## Restoring equipment

Every successful script inventory restore carries an `equipmentRestore`
counter in the owner's sync, including restores that change only which
items are equipped. A new counter takes precedence over an outstanding
local equipment report. Reports from before that restore cannot overwrite
it, even if the inventory row revision stayed the same.

The client checks the requested equipment after each dressing attempt has
had two seconds to settle. Missing pieces are retried, up to four attempts.
Only a verified outfit allows normal equipment reporting to resume. If it
still cannot equip the requested pieces, it logs the failure and preserves
the requested record instead of reporting the partial outfit as success.
A new restore or native-body rebind starts another attempt budget.
If a delayed native equip finishes after the retry limit, its equipment
event allows the client to verify completion and resume reporting without
issuing another equip attempt.

These fields are additive. Older clients retain their revision-based report
check, and a new client talking to an older server revisits a deferred
equipment record when its five-second echo window ends. Update both peers
for explicit restore precedence and protection against stale reports.

## Native changes

Changes the game makes to an inventory do not change the server's rows.
The client records them and restores the server's inventory on the next
projection. Wear is reported separately; see
[Inventory item properties](inventory-properties.md).

A native item the game only lends the player -- a smithery's tool while it
is worked -- is lent to the projection, which then leaves it alone until it
is given back.

Actions whose adapters are not part of this inventory core are refused where
they start, with a notice to the player: drops, pickups, container transfers,
consumables, shooting and pickpocketing. Alchemy and smithing have their own
adapters ([authoritative-alchemy.md](authoritative-alchemy.md),
[authoritative-smithing.md](authoritative-smithing.md)), and a drying rack's,
smokehouse's or fire's conversion is asked of the server after the game made it
([drying racks, smokehouses and fires](inventory-scripting-api.md#drying-racks-smokehouses-and-fires)). The server side
of ground items, vendors and pickpocketing stays unregistered until those
adapters move items through the server inventory.

## Quest items

The property policy refuses every write of a quest item with `questItem`:
a grant, a restore, a transfer or a crafting product. A quest product is
graded but not created. Its quest hands it out.

## Validation

`builds\build.bat RunKCDCTests 64` runs the inventory model, the
sync codec, the rate limit, the message bound, the item class index and the
property policy. It also checks projection retry deadlines, restore ordering,
legacy echo timeout handling, dressing verification and stale equipment
report rejection. Native item creation and wardrobe interactions still need
in-game validation.
