# Vendors on the game's own trade screen

Server-owned price lists and purses, traded on KCD2's native shop screen. The
server creates a shop for each session and sends its stock to the trade
screen. The stock stays separate from the keeper's body.

- **Live re-stock.** A new list while the screen is up is diffed onto the
  shelf, deferred while any shelf item sits in the trade basket (the basket
  holds items by pointer).
- **Server settles in one commit.** The basket names the server rows
  of every unit offered for sale, read by kind from the owner's projection.
  The server checks them against the priced sell lines, picks the money
  owed from the player's unworn money rows, and makes one `Exchange` on the
  player's inventory: sold units and money out, bought items and change in.
  Worn units are never sold or spent. The `playerInventoryChanged` reason
  is `trade`. The vendor's lists and purse move on after the commit.

There is no fallback list panel: patterns abort the load when they miss, so
there is no build on which the native path is unavailable.

## Files

| Path                                                                       | What                                                                        |
| -------------------------------------------------------------------------- | --------------------------------------------------------------------------- |
| `code/shared/features/vendor/vendor_rpc.h`                                 | `VendorOpen`, `VendorStock`, `VendorBasket`, `VendorResult`, `VendorClose`  |
| `code/server/src/features/vendor/vendor_service.{h,cpp}`                   | `VendorService::Get()`: vendors, sessions, re-pricing and settlement        |
| `code/server/src/features/vendor/vendor_scripting.{h,cpp}`                 | the `Vendor` global and the `vendorTrade` / `vendorClosed` events           |
| `code/client/src/features/vendor/vendor_shop.{h,cpp}`                      | `VendorShop`: the one native shop a session stands up                       |
| `code/client/src/features/vendor/vendor_module.{h,cpp}`                    | `VendorModule::Get()`: RPCs, phases, the close watch, the hook entry points |
| `code/client/src/features/vendor/vendor_hooks.cpp`                         | the price and commit detours                                                |
| `code/client/src/sdk/wh/shopmodule/c_shop.h`, `c_shopping_transaction.h`   | the shop and the deal, typed                                                |
| `code/client/src/sdk/wh/guimodule/c_ui_shop.h`, `wh/framework/i_ui_shop.h` | the screen and its close                                                    |
| `resources/kcdc-gamemode/src/server/commands/vendor.ts`                    | `/vendor stall`, `/vendor here`, `/vendor reset`                            |

## Protocol

```
server  VendorOpen  { session, npc, flags }         open the screen
server  VendorStock { session, revision, purse, stock[], buys[] }   first fill and every change
client  VendorBasket{ session, revision, lines[], soldUnits[] }  on Trade; nothing has moved
server  VendorResult{ session, outcome }            after settling
either  VendorClose { session, reason }             instruction / report
```

A basket priced against an older `revision` is refused with `StaleList` and
the current list is resent. Sold units that do not add up to the sell lines,
or that are worn or gone, refuse the basket with `ItemsMissing`;
too little money gives `PlayerCannotAfford`, a player whose inventory cannot
act yet `InventoryUnavailable`, and a deal that would overflow the inventory
`InventoryFull`. A refused basket moves nothing.

## Script events

Server: `vendorTrade` after a deal has settled, `vendorClosed` when a session
ends. Client, local to the machine and about the screen rather than the
session: `vendorOpened [session, npc | null]` when the trade screen actually
comes up, and `vendorClosed [session, reason]` exactly once after it, with
`reason` one of `player`, `server`, `replaced`, `levelChange`, `sessionOver`.
A screen that never came up raises neither.

## Not verified in game

Built, but not yet tested in game. These cases still need validation:

- A second trade on one open screen may do nothing.
- Updating the stock while the trade screen is open.
- Showing a server-owned NPC as keeper, with its name and portrait.
  `/vendor here` uses the player's own soul.

## Known limits

- Bought items arrive with the server's default properties: the stock rows
  carry a class and a price, not a quality or a condition.
- Sold units are named by kind -- class and properties -- not by the stack
  the player dragged. The screen moves part of a stack as a split-off item
  with a WUID no projection has seen, and folds what it moves into one
  basket item, so a basket
  item is rarely a stack the projection made. Rows of one kind are
  interchangeable, and units the body wears are left out.
- Each session leaves one 0xC8 transaction in the shop module's map, keyed by
  a WUID never reused. Closed, so nothing ever walks into it.
- Vendors are not cleaned up when the resource that made them stops, the same
  as conversations.
