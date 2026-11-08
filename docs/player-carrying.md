# Carrying players

A player can pick up another player's body and carry it on their shoulder,
the way Henry carries a corpse. Nothing new is drawn or animated: every part
of it -- the prompt, the pick-up, the walk, the put-down -- is the game's own
corpse carry, and what the mod adds is that everyone else sees it.

Items carried in the arms -- baskets, firewood, sacks -- go through the
game's item carry instead; see [Carrying items](item-carrying.md).

## What travels

| Where                                | What                                                                                   |
| ------------------------------------ | -------------------------------------------------------------------------------------- |
| `PlayerCarry` (client to server)     | the carried player's network id, and whether the body is now on the shoulder or off it |
| `HumanEntity::carriedNetworkId`      | the accepted carry, a server field on the carrier                                      |
| `PutDownCarried` (server to carrier) | let go, with or without the put-down clip                                              |

The carrier's own client is authoritative for starting and ending a carry --
its game already did both -- and the server for whether the carry stands.

## Client

- **Capture.** `player_service.cpp` hooks `Enter` and `Exit` for every
  body and keeps a register of running carries on `PlayerService`, as the
  ladder does. The local player's edges go to `LocalEvents`, which reports
  one `PlayerCarry` per edge, and only for a body that is another player.
  A replayed grab is refused if the carried body no longer exists.
- **Replay.** `Human::ApplyCarry` puts on the carrier puppet's shoulder
  whichever copy of the carried player this machine has: a puppet, or the
  local player's own body. It compares what the carry component actually
  holds with the field, and grabs, puts down or lets go to match. A carry
  already running when the body is first seen is joined with the instant grab.
- **Nobody carries the living.** The replay only grabs a body whose soul is
  dead or unconscious. A revival lets go first, on every machine
  (`Human::Respawn` for the local body, `Human::ApplyDeathState` for a
  puppet), because the body gets up on the spot while the server's word that
  the carry is over is still in flight.
- **Bodies that go away.** Before either body of a carry is despawned --
  through `Human::Despawn` or the queued despawn a departing replica leaves --
  the carry is let go synchronously, before either body is removed.

## Server

`PlayerService::ReportCarry` accepts a grab when the body is a connected
player other than the carrier, nobody else carries them, they carry nobody,
and the two are within 10 m and can see each other's virtual world, and then
only if no `playerCarrying` handler returns `false`. Any refusal is answered
with `PutDownCarried { immediate }`. A put-down, a disconnect of either
player and a revival end the carry and raise `playerPutDown`.

```js
Events.on("playerCarrying", (carrier, carried) => carrier.nickname !== "nobody");
Events.on("playerCarry", (carrier, carried) => console.log(`${carrier.nickname} carries ${carried.nickname}`));
Events.on("playerPutDown", (carrier, carried) => console.log(`${carrier.nickname} put ${carried.nickname} down`));
```

A player is only there to be carried while they stay down, and that is the
gamemode's call. The demo gamemode revives at once, so it never leaves one.

## Limits

- Only dead or unconscious bodies can be carried.
- The prompt refuses female carriers, as the game does.
- Tested live with two clients, `putDown()` from a script included. A third
  observer and a body streaming in mid-carry have not been run.
