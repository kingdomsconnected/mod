// Run as a server resource on a disposable test server with no players.
// Uses two unused worlds and removes only the stock it creates there.
import assert from "node:assert/strict";

assert.equal(Player.all().length, 0, "Use a test server without players");
const world = 0xfffffff0;
const otherWorld = world - 1;
assert.ok(Stash.all().every((stash) => stash.virtualWorld !== world && stash.virtualWorld !== otherWorld), "The test worlds must be unused");

const created = new Set();
const subscriptions = [];
try {
    const count = Stash.all().length;
    for (const value of [-1, 0.5, NaN, Infinity, -Infinity, 0x100000000, "3", null, {}, true]) {
        assert.throws(() => Stash.createVirtual(value), /uint32/);
        assert.equal(Stash.all().length, count, "Invalid worlds must not create stock");
    }
    const defaults = Stash.createVirtual(undefined);
    created.add(defaults.id);
    assert.equal(defaults.virtualWorld, 0xffffffff);
    defaults.destroy();

    const stash = Stash.createVirtual(world);
    created.add(stash.id);
    assert.equal(stash.isVirtual, true);
    assert.equal(stash.virtualWorld, world);
    assert.equal(stash.guid, "");
    assert.equal(stash.levelGuid, "");
    assert.equal(stash.name, "");
    assert.equal(stash.isOpen, false);
    assert.equal(stash.holderId, 0);
    assert.equal(Stash.getById(stash.id).isVirtual, true);
    assert.equal(Stash.nearest(new Vector3(0, 0, 0), 5, world), null);
    assert.equal(stash.open(null), false);
    assert.equal(stash.addItem({ item: "bread", amount: 5, metadata: { health: 0.75 } }).ok, true);
    const saved = stash.getInventory();
    assert.equal(saved.items[0].amount, 5);
    assert.equal(saved.items[0].metadata.health, 0.75);
    assert.equal(stash.removeItem({ units: [{ id: saved.items[0].id, amount: 2 }], revision: saved.revision }).ok, true);
    assert.equal(stash.getInventory().items[0].amount, 3);
    assert.equal(stash.setInventory({ items: saved.items }).ok, true);
    assert.equal(stash.getInventory().items[0].amount, 5);

    // The spawn event may destroy a handle before createVirtual returns it.
    const offSpawn = Events.on("stashSpawn", (spawned) => {
        if (spawned.isVirtual && spawned.virtualWorld === otherWorld) spawned.destroy();
    });
    subscriptions.push(offSpawn);
    assert.throws(() => Stash.createVirtual(otherWorld), /failed to create stock/);
    offSpawn();
    assert.ok(Stash.all().every((entry) => entry.virtualWorld !== otherWorld));

    const second = Stash.createVirtual(world);
    const third = Stash.createVirtual(world);
    const survivor = Stash.createVirtual(otherWorld);
    for (const entry of [second, third, survivor]) created.add(entry.id);
    const destroyed = [];
    let nested = false;
    subscriptions.push(Events.on("stashDestroy", (entry) => {
        if (!created.has(entry.id)) return;
        destroyed.push(entry.id);
        entry.destroy(); // Repeating destroy during its event is harmless.
        if (entry.virtualWorld === world && !nested) {
            nested = true;
            Stash.destroyAll(world); // May remove the outer walk's next entry.
        }
    }));
    assert.equal(Stash.destroyAll(world), 1);
    assert.equal(new Set(destroyed).size, 3);
    assert.equal(destroyed.length, 3, "Each stash has exactly one destruction event");
    for (const entry of [stash, second, third]) assert.equal(Stash.getById(entry.id), null);
    assert.ok(Stash.getById(survivor.id), "Another world's stock survives");
    survivor.destroy();
    assert.equal(Stash.all().length, count);
    console.log("Virtual stash scripting API regression checks passed");
} finally {
    for (const unsubscribe of subscriptions) unsubscribe();
    for (const id of created) Stash.getById(id)?.destroy();
}
