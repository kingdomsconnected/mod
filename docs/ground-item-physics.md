# Ground items: stock and physics

A ground stack is a server entity holding exactly what an inventory gave up.
A carryable stack is a prop people take up in their arms rather than stock;
see [Carrying items](item-carrying.md).

## Drop and pickup

- **Drop.** A quantity selection, including the maximum quantity, sends one
  `GroundItemDropRequest`. The client collects the game's per-stack drop
  callbacks and combines amounts belonging to the same server inventory row.
  The request names up to 64 rows, with an amount and held-item marker for
  each. It does not require a matching inventory revision.
  The server checks the entire selection against the player's current
  inventory and ground capacity before removing anything (reason `drop`).
  A missing row, insufficient quantity, or capacity refusal leaves the whole
  selection in the inventory. Native stackable items, such as arrows, become
  one ground stack per row. Non-stackable items, such as torches and weapons,
  become one ground object per unit. The server counts those objects against
  the available ground capacity before removing any items. The entire
  selection still uses one request and one inventory mutation.
  If a server entity fails to spawn, its units return
  to the inventory (reason `dropRefund`). Each selection spends one inventory
  request token; one player may have at most 64 ground stacks at once.
- **Pickup.** The first claim reserves the stack (`GroundItemPickupStarted`).
  When the native pickup completes, the server checks the stack is resting,
  the claimant is in its virtual world, within 4 m of the server's rest pose
  and able to act, adds the stack to
  their inventory (reason `pickup`), and destroys it. A full inventory
  releases the claim and leaves the stack where it is.
- **Scripts.** `GroundItem.spawn` puts server-authored stock down. The
  events are `groundItemSpawn`, `groundItemPickup` and `groundItemDestroy`.
  `GroundItem.spawn` creates one ground object: a non-stackable class must use
  quantity 1. A larger quantity is refused instead of creating invisible items.
  Nothing survives a restart; a script that wants ground stock back
  respawns it. A `name` in `GroundItem.spawn`'s properties is read back as
  `groundItem.name`, and a world resource can lay named stacks too; see
  [World objects](world-objects.md#items).

Native stackability comes from the generated item-property catalog. The game
creates separate native items for non-stackable classes. A client rejects a
multi-unit ground replica for such a class before creating anything. If native
creation returns an unexpected number of items or units, it removes the new
items and records the counts in the log. This client failure does not currently
trigger a server refund.

## Physics

A dropped stack falls under the Framework `DelegationManager`: the nearest
client within 40 m in its virtual world simulates the native pickable and
writes the pose. Once that client reports it resting -- or after 10 s, or
when that client disconnects -- the server settles it: it keeps the
reported rest pose unless it lies more than 8 m from where the stack was
dropped, forces that pose to the simulator, and takes ownership back. A
resting stack has no delegation policy, so it is never handed out again,
and its pose -- the one pickup reach is checked against -- is the server's.
A stack a script spawns is resting from the start.

`GroundItemEntity::CanOwnerDestroy()` is false: a client simulating a
stack cannot take it off the ground. Each viewer receives the nearest 64
stacks.

Observers place a falling stack every frame and a resting one once per
pose. Unattached pickables request native static physics on observers and
rigid physics on the elected simulator. The static body retains the pickup
collider without falling away from the server's rest pose.

Arrows and bolts keep the interaction ray available while players, NPCs
and horses walk through them. Replicated transforms are world-space and
keep the collider where the stack is drawn.

## Recovered ammunition

The shooter's ground replica adopts the pickable that native collision
created, by its post-transfer WUID. Other viewers materialize the same item
class at the reported final impact pose. The server still requires an
unclaimed spent round, so the original and its replica represent one item.

Recovery preserves an arrow's native attachment and physics state.
Unattached observer copies use the static profile above.
