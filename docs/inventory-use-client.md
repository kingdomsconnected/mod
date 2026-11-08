# Handle use from the native inventory

Client resources can register `Inventory.onUse(item, handler)` to replace
an item's native inventory action before the game consumes it or applies
its effects. This works with ordinary game items and custom items.

```ts
// Client resource
const appleUse = Inventory.onUse("apple", (use) => {
    // Start your resource's animation here. The apple is still in inventory.
    // When it finishes, ask your server resource to validate and consume it.
    Events.emitServer("food:use-apple", JSON.stringify({
        id: use.id,
        revision: use.revision,
    }));
});

```

When the override ends, `Inventory.offUse(appleUse)` restores the previous
handler or native behavior. Resource shutdown already removes its handlers.

`item` accepts an exact catalog name such as `apple`, a class GUID, a custom
item ID, or `{ uid: "..." }` for one native instance. A UID is a decimal
string so its 64 bits stay intact in JavaScript. It is local to this client
and can change when a stack splits, merges or is projected again. Use a
class handler for behavior that should survive those changes.

Custom definitions must have reached this client before registration; an
unknown name or GUID throws. For custom types the server registers before
joining, install their handlers in the client `playerReady` event. Ordinary
catalog items such as `apple` can be registered as the resource loads.

The callback receives:

| Field      | Meaning                                                |
| ---------- | ------------------------------------------------------ |
| `item`     | Class GUID; custom items use their logical GUID.       |
| `uid`      | Selected native instance's UID, as a string.           |
| `id`       | Server inventory row represented by that instance.     |
| `revision` | Inventory revision when the action was intercepted.    |
| `amount`   | Always `1`: one use, even when a row displays a stack. |
| `action`   | `primary`, `secondary`, or `activate`.                 |

The primary action becomes **Use** for a registered item, including items
with no normal use action. The secondary action covers the game's
**Eat/Drink** and **Learn** buttons. The generic equip-toggle path also
dispatches, including double-click for custom items. Native double-click
weapon-slot and food-pouch operations keep their existing behavior, as do
quick-slot actions outside the inventory. Unregistered items keep their
native actions.

Handlers run on the next feature update, after the native callback has
returned. Only the local player's owned, synchronized inventory can
dispatch. Two presses on the same native instance in one frame produce one
callback. At most 16 uses can wait for dispatch; further presses that frame
are suppressed. A removed handler, missing item or changed inventory
revision discards a pending use without running the native action.

An instance handler takes priority over class handlers. Among handlers for
the same selector, the newest wins. Removing it reveals the previous one.
`onUse` returns a numeric registration ID; `offUse(id)` removes only a
handler owned by the calling resource and returns whether it existed.
There can be at most 256 active handlers. Resource stop and session end
remove handlers and pending uses.

The callback's return value is ignored. Throwing, returning `false` or
waiting for a promise does not resume native behavior. No consumption,
stat change or animation happens automatically. `playerItemUsed` is a
notification after native use; it is not raised for the suppressed action.
For custom items, a client handler also replaces the automatic
`playerCustomItemUse` request.

The server must treat a client request as untrusted. Check the sender's
current row, class, available quantity, revision and any gameplay
restrictions. Use the server `Inventory.remove` result to decide whether
to grant an effect. An animation can outlive its inventory revision, so
handle rejection without granting the effect. The client API does not
expose private custom metadata or make the inventory authoritative.
