# Picking another player's pocket

One player robs another on the game's own Rob entry, charge, rosette and loot
grid. The thief's machine keeps deciding what it always decided -- whether
the victim noticed -- and the server decides what moves.

This replaced an accident. Rob was offered on a remote player's body because
nothing refused it, and the whole single-player chain then ran on the
thief's machine against a body that is not an NPC: the victim's own client
never heard of it, and a victim that "noticed" started the crime self-help
dialogue on the thief's machine, which crashed a tester's client on the
persuade line.

## Scripting

Server `EventMap`:

- `playerPickpocketStart(thief, victim)` -- synchronous; `false` refuses.
- `playerPickpocketed(thief, victim, items: PickpocketLine[])` -- what really
  moved, possibly nothing.
- `playerPickpocketCaught(thief, victim)`.

What any of it costs -- a fine, a bounty, letting the victim fight back -- is
the gamemode's.

## Known limits

- Stolen items arrive through `GiveItem`: class and amount only, not quality,
  condition or the game's stolen mark.
- The thief's own game shows "succeeded" as the loot screen closes; the
  server may settle less if the victim spent or dropped the item meanwhile.
- If the victim's body already carries a loose stack of a stocked class (a
  missile shadow stack) and `CreateItems` stacks into it rather than making a
  new item -- not verified either way -- that line is hidden from the grid,
  which shows only created WUIDs.
- The thief's client is trusted for detection, as it already is for its own
  inventory. What the server bounds is what can leave the victim: only lines
  it offered, only up to the amounts offered, only loose items still there.

## Files

| Path                                                                    | What                    |
| ----------------------------------------------------------------------- | ----------------------- |
| `code/shared/features/pickpocket/pickpocket_rpc.h`                      | The seven payloads      |
| `code/client/src/features/pickpocket/pickpocket_module.{h,cpp}`         | Thief and victim halves |
| `code/client/src/features/pickpocket/pickpocket_hooks.cpp`              | The six detours         |
| `code/client/src/sdk/wh/playermodule/c_minigame.h`, `c_pickpocketing.h` | Minigame layouts        |
| `code/client/src/sdk/wh/rpgmodule/c_steal_items_effect.h`               | The transfer effect     |
| `code/server/src/features/pickpocket/pickpocket_service.{h,cpp}`        | Authority               |
| `code/server/src/features/pickpocket/pickpocket_scripting.{h,cpp}`      | Events                  |
