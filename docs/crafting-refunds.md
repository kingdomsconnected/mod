# Crafting refund policy

The server settles smithing materials and alchemy ingredients. Resources can
request a full refund for an unfinished craft through a synchronous event.
The scripting side of each craft is in [alchemy-api.md](alchemy-api.md) and
[smithing-api.md](smithing-api.md); the native settlement rules a refund
replaces are in [authoritative-alchemy.md](authoritative-alchemy.md#ingredients)
and [authoritative-smithing.md](authoritative-smithing.md#materials).

```ts
Events.on("craftingRefunding", (player, proposal, refund) => {
    if (proposal.reason === "interrupted") {
        refund();
    }
});
```

The default gamemode installs no refund policy, so every exit keeps its
native material loss unless a resource opts in.

`interrupted` and `clientError` always come from the player's own client,
and so does `cancelled` unless a script called `Crafting.cancel`. A
modified client can report any of them for any exit, so a policy
that refunds `interrupted` lets that client keep its materials whenever it
leaves a craft. Choose such a policy only where that trade is acceptable.

## Contract

`CraftRefundProposal` is frozen and contains `kind` (`alchemy` or `smithing`),
`session`, `station`, `virtualWorld`, `outcome` (`failed` or `cancelled`),
`reason`, and `materials`. Each `CraftRefundMaterial` has an item class GUID
in `item`, an `amount`, and the original canonical inventory `metadata`.

- Call `refund()` synchronously to request all remaining original ingredients
  back, including processed alchemy ingredients. Do not grant items yourself.
- No handler, or no call to `refund()`, preserves the native settlement rules.
  Handler return values are ignored; `refund()` is the only request.
- A thrown JavaScript exception is logged, but does not cancel a refund
  already requested by that handler or another handler.
- The callback expires when event dispatch returns. Retaining it, calling it
  after `await`, or calling it from a timer has no effect. Repeated calls do
  not increase the refund.
- The event runs once before an unfinished craft settles, even when the
  native rules would return nothing. The session cannot be cancelled or
  restarted recursively while that decision runs.
- `Crafting.cancel(player)` runs `craftingRefunding` synchronously, before
  it returns; only `craftingEnded` waits for the next tick.
- A completed batch or granted smithing product never triggers this decision.
  This includes an alchemy failure that already granted the failed potion.
- Station liquids and ingredients explicitly returned earlier are excluded.
  Original ingredient properties are preserved; returning an item can merge
  it into a different inventory row.
- `craftingEnded` follows settlement and inventory-change dispatch. Its
  `refunded` list reports the rows actually credited, not just the requested
  ingredients. Inventory admission still applies.

## Reasons

| Reason                       | Meaning                                                                                                                                 |
| ---------------------------- | --------------------------------------------------------------------------------------------------------------------------------------- |
| `interrupted`                | The native minigame ended without a normal exit or a crafting result: another body action replaced the minigame's, or it was torn down. |
| `cancelled`                  | Normal native exit, or `Crafting.cancel(player)` from a script.                                                                         |
| `craftingFailed`             | Smithing reported a broken workpiece.                                                                                                   |
| `clientError`                | Alchemy's client adapter retired after a failed request or continuation.                                                                |
| `contextInvalidated`         | The server found the player unable to act, out of range, or in another world/context.                                                   |
| `disconnected`               | The connection ended.                                                                                                                   |
| `timeout`                    | The session lifetime expired.                                                                                                           |
| `craftingCompletingRejected` | A script refused the proposed result.                                                                                                   |
| `invalidQuality`             | Smithing quality did not resolve to a permitted product.                                                                                |
| `abandoned`                  | An approved smithing recipe never started within the abandon window; native settlement already returns all materials.                   |
| `abandonTooLate`             | The smithing abandon arrived too late for the native full refund.                                                                       |
| `inventoryUnavailable`       | A smithing product could not be added; native settlement already returns all materials.                                                 |

A gamemode may choose a policy for any of these reasons. A server
context check takes precedence over a client interruption report, so a fatal
hit or a world change produces `contextInvalidated` instead. An expired
session uses `timeout`, even if the report arrives before the expiry sweep.

Client exit reasons are observations from the owning client, not independent
server proof of an attack. `playerDamage` remains a separate event; damage
correlation is not used to guess why a minigame stopped.

## Compatibility

Client and server builds must match: smithing reports carry the
`Cancelled` and `Interrupted` wire outcomes, and an alchemy `cancel`
carries a `reason` the server validates. An alchemy `cancel` without a
reason is treated as `cancelled`. For scripts, a broken smithing workpiece
ends with reason `craftingFailed`, and a normal native smithing
cancellation ends with outcome `cancelled`, not `failed`.
