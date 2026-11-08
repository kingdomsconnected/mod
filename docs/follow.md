# Following players and NPCs

`Follow` lets a client resource ask the local player to follow another player
or a streamed human NPC. It uses the game's follow action on foot and on
horseback. It does not issue orders to NPCs; use the server's `Npc.follow` for
that.

```ts
const target = Follow.targets().find((target) => Follow.canStart(target));
if (target) Follow.start(target);

Events.on("followStarted", (target) => console.log(`Following ${target.name}`));
Events.on("followStopped", (target, reason) => console.log(`${target.name}: ${reason}`));
```

`Follow.targets()` returns nearby streamed players and human NPCs within 20
metres. Each descriptor contains `id`, `kind`, `name` and `position`. The id is
resolved against the live replica when used. `Follow.start` and
`Follow.canStart` also accept a client `Player` handle.

The game decides whether combat, interactions, mount state and starting
distance allow follow. A successful `start` means the request was queued;
`followStarted` confirms it entered. `Follow.isPending()` reports a queued
request, and `Follow.getTarget()` returns the active target or `null`.

`Follow.stop()` cancels the calling resource's request. Movement input, loss
of the target, death or unconsciousness, a mount change, a large position jump,
or separation beyond 40 metres also ends follow. Stopping the owning resource
or ending the session releases the action. A failed queued request produces
`followStopped` without a preceding `followStarted`.

The sample gamemode shows a Follow action hint when looking at an eligible
player or NPC, and a Stop following hint while following. The hint uses
`chat_focus_follow_init`, the game's Follow control, so it respects keyboard
rebinding and controller glyphs. While a hint on this control exists, the
native focus/follow input actions are suppressed to avoid handling one press
twice. Other action hint keys keep their existing behavior.
