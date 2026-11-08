"use strict";
const assert = require("node:assert/strict");
const { spawnNpc } = require(process.argv[2]);
const meat = { item: "meat", amount: 8, fraction: 1, variation: 0, food: true, harvester: false };
const skin = { item: "skin", amount: 1, fraction: 1, variation: 0, food: false, harvester: true };
let candidates, writes, removed, result;
const npc = { getAnimalLoot: () => candidates, remove: () => { removed++; } };
global.Npc = { create: options => { assert.equal(options.soul, "fixture"); return npc; } };
global.Inventory = { set: (owner, stock) => { assert.equal(owner, npc); writes.push(stock); return result; } };
function reset(rows) { candidates = rows; writes = []; removed = 0; result = { ok: true }; }
function spawn(values) {
    return spawnNpc({ soul: "fixture" }, () => {
        assert.notEqual(values.length, 0, "unexpected random roll");
        return values.shift();
    });
}
reset(null);
assert.equal(spawn([]), npc);
assert.equal(writes.length, 0, "human gear and excluded animals remain untouched");
reset([]);
spawn([]);
assert.deepEqual(writes, [{ items: [], harvested: false }], "empty presets establish an empty inventory");
reset([meat, skin]);
spawn([0.999999, 0.499999]);
assert.deepEqual(writes[0].items, [{ item: "meat", amount: 8 }, { item: "skin", amount: 1 }]);
assert.equal(writes.length, 1, "the entire inventory is committed atomically once");
reset([meat, skin]);
spawn([0, 0.5]);
assert.deepEqual(writes[0].items, [{ item: "meat", amount: 8 }], "exact 50% boundary excludes the part");
reset([skin]);
spawn([0.99]);
assert.deepEqual(writes[0].items, [], "failed rolls still establish empty stock");
for (const [quantitySample, amount] of [[0, 0], [0.5, 1], [0.999999, 1]]) {
    reset([{ ...skin, variation: 0.5 }]);
    spawn([0, quantitySample]);
    assert.deepEqual(writes[0].items, amount ? [{ item: "skin", amount }] : [], "authored fractional quantities are bounded");
}
for (const invalid of [-1, 1, Infinity, NaN]) {
    reset([meat]);
    assert.throws(() => spawn([invalid]), /random sample/);
    assert.equal(removed, 1);
    assert.equal(writes.length, 0, "invalid generation cannot commit a partial inventory");
}
reset([meat]);
result = { ok: false, code: "invalidClass" };
assert.throws(() => spawn([0]), /invalidClass/);
assert.equal(removed, 1, "failed preparation rolls back the animal spawn");
console.log("Gamemode animal spawner: all assertions passed.");

require("./animal_population_ut.cjs");
