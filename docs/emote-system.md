# Emotes

## Usage

The emote wheel offers six default gestures: wave, big wave, nod,
respectful nod, bow and cheer. A connected session is required.

The F5/F6 debug overlay has been removed. Native callers request a gesture
through `KCDC::Features::Social::EmoteService::RequestLocal(id)`; zero
cancels the current request.

Only requested catalog gestures are replicated. NPC gestures, dialogue,
combat and scripted `C_Player::PlayAnim` calls are unaffected.

## Wheel layout from client scripts

The wheel has `EmoteWheelService::kSlotCount` (six) slots, numbered
clockwise from the one straight up. Each chooses one catalog row. Until a
script changes them they hold `EmoteWheelService::kDefaultSlots` -- wave,
big wave, nod, respectful nod, bow, cheer -- pinned by catalog id rather
than read off the head of the catalog, so adding rows to the catalog never
changes the wheel a player gets by default. Neither does the slot count
follow the catalog: a catalog that grows gives scripts more to choose from,
not a busier wheel.

Client scripts rearrange it through the `EmoteWheel` global:

```ts
const bow = EmoteWheel.getCatalog().find((e) => e.name === "Bow")!;
EmoteWheel.setSlot(0, bow.id);           // one slot
EmoteWheel.setSlots([5, 6, 1, 2, 3, 4]); // all of them, or nothing
EmoteWheel.reset();
```

The layout is local and cosmetic: every client can already play every
catalog gesture, so it changes what is within reach, not what is allowed,
and nothing about it reaches the server. A changed slot is redrawn in place
on the next tick -- its figure cleared and inked again, its label rewritten
-- rather than recomposing the wheel. The layout is dropped with the
session, so the next server's scripts start from the default.

A gesture the wheel has no drawn figure for gets the cheer figure; the
label is what names it.

## Catalog and scope selection

`tools/generate_emote_catalog.py` reads
`Tables.pak/Libs/Tables/action/actor_anim_action.xml` and emits the compiled
`code/shared/generated/emote_catalog.cpp` vector. It requires exact one-shot
rows, human class hash `1578932418`, option zero, and durations under ten
seconds. It fails if the selected rows change or become ambiguous.

`Animations.pak/Animations/Mannequin/ADB/kcd_male_fragmentids.xml` binds
Greetings and GreetingsUpperBody to `kcd_greetings_tags.xml`. That file
contains the selected wave, nod and bow tags. `kcd_male_controllerdefs.xml`
assigns GreetingsUpperBody to UpperBody; Greetings to FullBody (with
Looking for bowBig), and TournamentCheersHappyGesture to FullBody.

Option zero is selected explicitly on every peer. Fragment and tag CRCs
are resolved against each receiving actor's dictionaries; local numeric
fragment/tag indices and pointers never travel on the wire. Native global
tags still affect database matching. Waves come from empty-hand rows;
weapon, stance and first-person differences need separate testing.

## Event flow

1. A UI command reaches the game thread and constructs a native action.
2. The native Enter hook reports Start through `LocalEvents` after the
   action is installed.
3. The server validates the catalog ID, phase, sequence and decoded packet.
   It derives the actor from the sending connection and replaces any actor
   ID in the payload. Starts are limited to one per 750 ms per connection.
4. Receivers enqueue the RPC and apply it on the game thread. Owner echoes
   are ignored. Missing or unready bodies skip transient starts; they do
   not play stale gestures when they later stream in.
5. Native Exit/Fail hooks report Stop through `LocalEvents`. Explicit
   cancellation reports the stop request once, then calls ForceFinish.
   Remote events never pass through the local report path.
6. Stops must match both the accepted sequence and gesture. Duplicates and
   old stops cannot cancel a newer action. Disconnect and despawn release
   references and clear session bookkeeping.

Update drains commands and RPCs and releases references marked by callbacks.
Native pending expiry can remove an action without Exit. A one-second
deadline releases the mod reference if Enter never arrives; Queue has a
half-second pending timeout. No native animation state is polled.

Only transient events are sent; a late joiner does not replay an old emote.
Matching client and server binaries are required.

## Verification

- Three emote tests cover serialization, every byte truncation, invalid IDs
  and phases, duplicates, rate limiting, mismatched stops, and sequence reset.
- The catalog generator reproduces its output from the installed tables.

Regression checklist:

1. Use each Emotes button with empty hands and observe a single matching
   gesture on the other client, with no owner echo or repeated restart.
2. Stop and replace gestures; ensure old stop messages never end the new one.
3. Check natural completion, combat interruption, movement, held torch,
   first/third-person views, and both streamed human appearances.
4. Try a request while the native controller cannot install it; no Start
   should be relayed, and its retained reference should expire.
5. Despawn, stream out, disconnect and reconnect during playback. Confirm
   no stale start, crash, or native action survives its session.
