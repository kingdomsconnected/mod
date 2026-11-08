const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

function fixture() {
    const calls = [], lines = [];
    const player = { virtualWorld: 0xffffffff, position: { x: 0, y: 0, z: 0 } };
    const pot = { guid: "near", position: { x: 1, y: 0, z: 0 }, food: "soup", portions: 0, nutrition: 15, enabled: true, poison: "hidden-effect" };
    let result = { ok: true, code: "ok" };
    let usage = 0;
    const globals = {
        CookPot: {
            all(world) { calls.push(["world", world]); return [{ ...pot, guid: "far", position: { x: 10, y: 0, z: 0 } }, pot]; },
            refillFromInventory(...args) { calls.push(["refill", ...args]); return result; },
            poison(...args) { calls.push(["poison", ...args]); return result; },
            poisons() { return ["bane"]; },
        },
        Inventory: { get() { return { items: [{ id: "1", item: "bread", name: "breadroll", amount: 4 }, { id: "2", item: "bane", name: "banePoison", amount: 2 }] }; } },
    };
    const cache = new Map();
    function load(file) {
        if (cache.has(file)) return cache.get(file);
        const context = { ...globals, exports: {}, require: name => load(path.resolve(path.dirname(file), name)) };
        vm.runInNewContext(fs.readFileSync(file, "utf8"), context);
        cache.set(file, context.exports);
        return context.exports;
    }
    const command = load(path.join(__dirname, "../dist/server/commands/pot.js")).potCommand;
    return { calls, lines, player, pot, refuse(code) { result = { ok: false, code }; }, usage: () => usage,
        run(...args) { command.run({ player, args, reply: { line: text => lines.push(text) }, usage() { usage++; } }); } };
}

test("refill selects a server recipe and nearest pot in the stock world", () => {
    const f = fixture();
    f.run("refill", "vegetable");
    assert.deepEqual(f.calls[0], ["world", 0]);
    const [, player, guid, recipe] = f.calls[1];
    assert.equal(player, f.player);
    assert.equal(guid, "near");
    assert.equal(recipe.food, "soup");
    assert.equal(recipe.portions, 4);
    assert.equal(recipe.ingredients.length, 2);
    assert.match(f.lines[0], /Refilled/);
});

test("numbered worlds stay separate and pot inspection does not disclose poison", () => {
    const f = fixture();
    f.player.virtualWorld = 7;
    f.run();
    assert.deepEqual(f.calls, [["world", 7]]);
    assert.match(f.lines[0], /Vegetable stew/);
    assert.doesNotMatch(f.lines[0], /hidden-effect|poison/i);
});

test("invalid or inherited recipe names never execute a player action", () => {
    for (const args of [["refill", "constructor"], ["refill", "__proto__"], ["refill", "meat", "1000"], ["unknown", "2"]]) {
        const f = fixture();
        f.run(...args);
        assert.equal(f.usage(), 1);
        assert.equal(f.calls.filter(call => call[0] !== "world").length, 0);
    }
});

test("poison list shows only accepted inventory rows and action names the row", () => {
    const f = fixture();
    f.run("poisons");
    assert.equal(f.lines.length, 1);
    assert.match(f.lines[0], /2: banePoison/);
    f.run("poison", "2");
    assert.deepEqual(f.calls.at(-1), ["poison", f.player, "near", "2"]);
});

test("refusals report no successful refill and distant pots cannot be used", () => {
    const f = fixture();
    f.refuse("missing_ingredients");
    f.run("refill", "meat");
    assert.match(f.lines[0], /required food/);
    f.player.position.x = 50;
    f.run("poison", "2");
    assert.match(f.lines.at(-1), /no food pot/);
    assert.equal(f.calls.filter(call => call[0] === "poison").length, 0);
});
