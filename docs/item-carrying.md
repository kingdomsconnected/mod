# Carrying items

A player can carry an item in their arms -- a basket of firewood, a stone
basket, a sack -- the way the game's own people do: the carrying walk, no
running, the put-down at the end. Put down, the item stays where it falls as
a prop anybody can take up again with the game's pick-up. Nothing new is
drawn or animated. The carry is the game's own, and what the mod adds is that
a script can start one, that the prop is a replicated ground item, and that
everyone else sees all of it.

Carrying another player's body on the shoulder is a separate carry; see
[Carrying players](player-carrying.md). A put-down item is an ordinary
ground stack; see [Ground items: stock and physics](ground-item-physics.md).

## Which items

Those whose manipulation type has a `MotionMovement` row: firewood, the
stone, egg and shopping baskets, the `basket_*` family, buckets, and -- for
men only -- sacks, barrels and crates. The server checks the item against the
animation catalog (`CarryWalkBodies`) and the player's body before it asks.

## What travels

| Where                                                                                   | What                                                                                                               |
| --------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------ |
| `CarryItem` (server to carrier)                                                         | carry an item of this class, or take up this claimed prop                                                          |
| `RequestCarryGroundItem` (client to server)                                             | the player chose the pick-up prompt over a prop                                                                    |
| `PlayerCarryItem` (client to server)                                                    | the item went into their arms (from which prop), the put-down clip started, or it left them (to the ground or not) |
| `HumanEntity::carriedItemClass`, `carriedGroundItemNetworkId`, `carriedItemPuttingDown` | the accepted carry, the prop it came from, and whether its put-down has started, server fields on the carrier      |
| `GroundItemEntity::carryable`                                                           | a prop: taken up by carrying, never into an inventory                                                              |
| `PutDownCarriedItem` (server to carrier)                                                | put it down, with or without the clip; `refused` returns a prop's item to it                                       |

The carrier's own client is authoritative for starting and ending a carry --
its game did both -- and the server for whether the carry stands and for the
props.

## Client

- **The item.** `PlayerService::StartItemCarry` makes an item of the class in
  the world inventory, without putting it on the ground, and requests the
  carry. The `Enter` hook sees the carry is one it asked to be instant and
  sets the phase to `PickingUp` through the game's own `SetCarryPhase`, so
  the item goes straight into the hand. Every item a carry on this machine
  holds is recorded until it is a prop's, a pile's, or removed.
- **Edges.** `player_service.cpp` hooks `Enter` and `Exit` for every body
  and keeps the running carries on `PlayerService`. The local player's edges
  go to `LocalEvents`, which reports `PlayerCarryItem`. The put-down's start
  is hooked as well (`BeginPutDownAnimation`, the one way into phase 4) and
  reported as it begins, so the copies play the clip alongside the carrier
  rather than after it; the carry itself ends when the put-down does.
- **Inventory authority.** `CompleteItemTransfer` on the local player is
  refused while the server holds the inventory, except when its destination
  is a carry's own holder. An instant carry transfers inside `Enter`, before
  the action could be registered, so the action counts as running from just
  before its `Enter`. A carried item is no held item either, so it is never
  published as `handItemClass`.
- **Replay.** `Human::ApplyCarriedItem` compares the carry a puppet runs with
  the fields. It takes up the prop's item with the pick-up, makes one straight
  in the hands, plays the put-down from the arms, or ends a carry outright.
- **Bodies that go away.** Before a body is despawned -- through
  `Human::Despawn` or the queued despawn -- its carry is finished through the
  action's queue (`C_Action::Finish`), which runs `Exit` and drops the item.

## Props on the ground

A put-down item becomes a carryable `GroundItem` -- the existing ground stack,
so streaming, the nearest client simulating its fall and the server settling
it are all the ground item's own.

- **Put-down.** The carrier reports the item went to the ground; the server
  ends the carry and spawns the prop falling from their hands, counted against
  their stacks on the ground. Every client's replica *adopts* the item its own
  copy of that carry dropped (`TryAdoptCarriedDrop`) -- the carrier's, and the
  one each observer's put-down dropped from the puppet -- so the physics item
  the hands let go of is the prop; nothing vanishes and reappears. A replica
  that arrives before the put-down played there waits for it, up to 6 s, then
  builds its own. A dropped item no prop takes over is removed after that.
- **Pick-up.** The game draws no prompt over an NPC tool lying in the world
  -- only classes its tables script as `CarryableItem` get the carry prompt
  -- so the client offers its own: a native action hint on the use key, in
  the game's own words (`@ui_pickup_item`), over the nearest carryable prop
  within 2 m while the player's hands are free and the shared rule
  (`ItemCarryRefusal`: able to act, not riding, carrying nothing) allows
  it (`GroundItemService::UpdateCarryPrompt`, through `ActionHints`). An
  inventory pickup the game starts on a prop is intercepted the same way
  (`RequestCarryInstead`). Either asks the server, which checks the same
  rule and the stack (`CarryRefusal`: a single item, resting within reach,
  held by nobody), runs `playerCarryingItem`, claims the stack, and answers
  `CarryItem` naming it. A prop of a class the game scripts as
  `CarryableItem` -- the carryable quest sacks -- keeps the game's own
  carry prompt, which starts the carry without asking; the client then
  releases the prop's item and names the prop, and the server claims it on
  the spot, gamemode willing.
  `player.carryItem(groundItem)` claims and orders the same way. The carrier
  takes up the prop's own item with `RequestCarryItem` -- exactly the
  `CarryableItem` pick-up -- and every other client does the same with its
  own copy when `carriedGroundItemNetworkId` arrives. A kind the game has no
  pick-up clip for (the stone basket) goes into the hands straight from the
  ground instead. The game's pick-up moves an item loose in the world
  to where its clip holds it in front of the carrier
  (`BeginItemPickupAnimation`); a pile's item it leaves in place and
  aims the fragment's `TargetPos` at instead, which the PositionAdjust
  clips bring the carrier to. Every body's pick-up of a prop is given
  the pile's treatment: the item is put back and `TargetPos` names the
  carrier pose that holds it there. A put-down prop starts where the
  carrier's game let the item go, which the put-down report carries.
  A claim lapses after 5 s if the carrier never reports
  the carry. Once the pick-up is accepted the server raises
  `groundItemPickup` for the prop, as an inventory pickup does, while it can
  still be read. The stack is destroyed 3 s later, once everyone has taken
  it up; a client that sees the carry later makes the item in the hands.
  The same mechanism, an `ActionHints` record on the use key, gives
  [siege engines](siege-engines.md#working-an-engine) the prompt that takes
  their place.
- **Refusals.** The server refuses an inventory pickup of a carryable stack.
  A carry it refuses after the pick-up started is let go with `refused`, and
  the item goes back to its prop instead of becoming a second one.

## Server

`PlayerService::ReportItemCarry` judges a reported carry with
`JudgeItemCarryReport` (`item_carry_policy.h`, unit-tested): a class the
game lacks is refused; a carry the server ordered -- `carryItem`, or a prop
the prompt asked for -- was already decided; one the player started at a
pile needs a class with a carrying walk and is put to `playerCarryingItem`;
a prop is only taken up by the carrier it is claimed for, or claimed on the
spot when the game's own prompt started the carry. Claims, and the delay
before a taken prop leaves the ground, are `GroundCarryClaims`
(`ground_stock.h`, unit-tested). Any refusal is answered with
`PutDownCarriedItem { immediate, refused }`, which releases the prop's claim
so the item goes back to it. A
put-down and a disconnect end the carry, leave a prop where the item fell,
and raise `playerPutDownItem` with it.

```js
Events.on("playerCarryingItem", (player, item, groundItem) => item !== "barrel");
Events.on("playerCarryItem", (player, item, groundItem) => console.log(`${player.nickname} carries ${item}`));
Events.on("playerPutDownItem", (player, item, groundItem) => console.log(`${item} fell at ${groundItem?.position}`));

player.carryItem("basket_full_wood");
player.putDownItem();
const prop = GroundItem.spawn("stonebasket", position, rotation, 1, 0, { carryable: true });
other.carryItem(prop);
```

- `player.carryItem(item | groundItem)` orders a carry and returns whether
  the order went out; the carry stands once the client reports it, when
  `playerCarryItem` fires and `player.carriedItem` names it. It throws for a
  name that is no carryable item (`Animations.props()` lists the props and
  their `hand`), or for one the player's body has no carrying walk for. It
  returns `false` when the shared rule refuses the player, or when a prop
  cannot be claimed for them.
- `player.putDownItem({ immediate? })` plays the game's put-down, or lets
  go at once with `immediate`. It returns `false` when nothing is carried.
- `player.carriedItem` is the carried item's name, or `null`.
- `GroundItem.spawn(item, position?, rotation?, amount?, virtualWorld?,
  { carryable: true })` lays a prop down instead of stock. It throws unless
  `amount` is 1 and the item has a carrying walk. `groundItem.carryable`
  says whether a stack is such a prop.
- `playerCarryingItem(player, item, groundItem | null)` is asked, and may
  return `false`, only for a carry the player started -- at a pile, or at a
  prop. `playerCarryItem` and `playerPutDownItem` take the same arguments;
  the put-down's `groundItem` is `null` when the item went into a pile or
  the player has no room left on the ground.

The default gamemode's `/carry` tries it out:

| Command              | What it does                                                                                                                                             |
| -------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `/carry <item>`      | carry an item                                                                                                                                            |
| `/carry prop <item>` | lay a carryable prop 1.2 m in front of you, for the pick-up prompt                                                                                       |
| `/carry list`        | suggest items: `basket_full_wood`, `basket_b_apples`, `stonebasket`, `firewoodChipsHand`, `waterBucket`, `eggbasket`, `shoppingbasket`, `sack`, `barrel` |
| `/carry drop [now]`  | put it down; `now` lets go at once                                                                                                                       |

## Limits

- The player's hands have to be free. A drawn weapon or a torch makes the
  game refuse the carry, and nothing is reported.
- A prop is never anybody's: it enters no inventory, and nothing survives a
  restart. A player has at most 64 stacks on the ground; past that a put-down
  item is simply gone.
- The prompt over a prop is an action hint on the use key rather than the
  world-space prompt of the game's `CarryableItem` entity, which only the
  item tables can give a class.
