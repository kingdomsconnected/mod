const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const { test } = require("node:test");

const LINDEN = "objects/natural/vegetation/trees/normal_trees/tilia_cordata/tilia_cordata_d.cgf";
const BIRCH = "objects/natural/vegetation/trees/normal_trees/betula_pendula/betula_pendula_g.cgf";
const LINDEN_STUMP = "objects/natural/vegetation/trees/stumps/stump_tilia_cordata_a.cgf";
// axeWork01, 1fc42528-2bef-4dde-bf8a-04febeef41c8, as a hand reports it.
const WORK_AXE = "4dde2bef1fc42528c841efbefe048abf";

class Vector3 { constructor(x = 0, y = 0, z = 0) { Object.assign(this, { x, y, z }); } }

function setup() {
    const handlers = new Map();
    const timers = [];
    const edits = [];
    const props = [];
    const traces = [];
    let now = 1000;
    let nextId = 100;
    const emit = (name, ...args) => { for (const fn of handlers.get(name) ?? []) fn(...args); };
    const context = {
        exports: {}, console: { log() {} }, Vector3, Math, Promise,
        Date: { now: () => now },
        setTimeout: (fn, ms) => { const timer = { fn, at: now + ms }; timers.push(timer); return timer; },
        clearTimeout: (timer) => { const index = timers.indexOf(timer); if (index >= 0) timers.splice(index, 1); },
        Events: { on: (name, fn) => handlers.set(name, [...(handlers.get(name) ?? []), fn]) },
        Chat: { sendToPlayer() {} },
        LevelEdit: {
            apply(definition, world) {
                const edit = { definition, world, exists: true, restore() { this.exists = false; } };
                edits.push(edit);
                return edit;
            },
        },
        Prop: {
            spawn(model, position, rotation, scale, physics, world) {
                const prop = { id: nextId++, model, position, rotation, scale, physics, world, mass: 0, velocity: null, destroyed: false };
                prop.setVelocity = (velocity, angular) => { prop.velocity = { velocity, angular }; return true; };
                prop.destroy = () => { prop.destroyed = true; };
                props.push(prop);
                return prop;
            },
        },
        World: {
            raycast: async (player, from, to) => {
                traces.push({ from, to });
                return { answered: true, hit: { model: LINDEN_STUMP, position: new Vector3(from.x, from.y, 112.4) } };
            },
        },
    };
    vm.runInNewContext(fs.readFileSync(path.join(__dirname, "../dist/server/woodcutting.js"), "utf8"), context);
    context.exports.installWoodcutting();
    const woodcutter = { id: 1, nickname: "Woodcutter", virtualWorld: 0, position: new Vector3(564.5, 351.3, 111.7), rightHandItem: WORK_AXE, leftHandItem: "" };
    const tree = (name) => ({ kind: "brush", name, at: new Vector3(564.54, 354.36, 111.2), rotation: { x: 0, y: 0, z: 0, w: 1 }, scale: new Vector3(1, 1, 1) });
    const blow = (object, player = woodcutter) => emit("playerWeaponHit", player, { category: "tree", kind: "vegetation", model: object.name, levelObject: object });
    // Runs every timer due by then, and lets the awaited ones resolve.
    const advance = async (ms) => {
        now += ms;
        for (let round = 0; round < 5; ++round) {
            await new Promise((resolve) => setImmediate(resolve));
            for (const timer of timers.filter((candidate) => candidate.at <= now)) {
                timers.splice(timers.indexOf(timer), 1);
                timer.fn();
            }
        }
        await new Promise((resolve) => setImmediate(resolve));
    };
    return { context, emit, edits, props, traces, woodcutter, tree, blow, advance };
}

test("a hand reports a class the way the game stores its GUID", () => {
    const s = setup();
    assert.equal(s.context.exports.packedItemClass("1fc42528-2bef-4dde-bf8a-04febeef41c8"), WORK_AXE);
});

test("three blows of an axe fell a linden onto its own stump, tipped away from the woodcutter", async () => {
    const s = setup();
    const linden = s.tree(LINDEN);
    s.blow(linden); s.blow(linden);
    await s.advance(0);
    assert.equal(s.edits.length, 0);

    s.blow(linden);
    await s.advance(500);
    assert.equal(s.edits.length, 1);
    assert.equal(s.edits[0].definition.model, LINDEN_STUMP);
    assert.equal(s.edits[0].definition.name, LINDEN);
    assert.equal(s.props.length, 1);
    const trunk = s.props[0];
    assert.equal(trunk.model, LINDEN);
    assert.equal(trunk.physics, "rigid");
    assert.equal(trunk.mass, 500);
    // On top of the traced stump, not inside it.
    assert.ok(Math.abs(trunk.position.z - 112.42) < 1e-6);
    // The woodcutter stands south of it, so the crown goes north.
    assert.ok(trunk.velocity.velocity.y > 0);
    // About the axis up × north = -x, which carries the top north.
    assert.ok(trunk.velocity.angular.x < 0);

    // A fourth blow fells nothing more.
    s.blow(linden);
    await s.advance(500);
    assert.equal(s.props.length, 1);

    // Once it lies still, it is a log.
    s.emit("propRest", trunk, trunk.position, trunk.rotation);
    assert.equal(trunk.physics, "static");
});

test("a tree the game has no stump for stands, and so does one hit with something else", async () => {
    const s = setup();
    const birch = s.tree(BIRCH);
    for (let i = 0; i < 5; ++i) s.blow(birch);
    const swordsman = { ...s.woodcutter, rightHandItem: "0123456789abcdef0123456789abcdef" };
    const linden = s.tree(LINDEN);
    for (let i = 0; i < 5; ++i) s.blow(linden, swordsman);
    await s.advance(1000);
    assert.equal(s.edits.length, 0);
    assert.equal(s.props.length, 0);
});

test("blows a minute apart start over, and a felled tree grows back", async () => {
    const s = setup();
    const linden = s.tree(LINDEN);
    s.blow(linden); s.blow(linden);
    await s.advance(61_000);
    s.blow(linden);
    await s.advance(500);
    assert.equal(s.edits.length, 0);

    s.blow(linden); s.blow(linden);
    await s.advance(500);
    assert.equal(s.edits.length, 1);

    await s.advance(10 * 60_000);
    assert.equal(s.edits[0].exists, false);
    assert.equal(s.props[0].destroyed, true);
});

test("/trees regrow gives every felled tree back at once", async () => {
    const s = setup();
    const linden = s.tree(LINDEN);
    s.blow(linden); s.blow(linden); s.blow(linden);
    await s.advance(500);
    const lines = [];
    s.context.exports.treesCommand.run({ args: ["regrow"], reply: { line: (text) => lines.push(text) }, usage() {} });
    assert.deepEqual(lines, ["Grew back 1 felled tree(s)."]);
    assert.equal(s.edits[0].exists, false);
    assert.equal(s.props[0].destroyed, true);
});
