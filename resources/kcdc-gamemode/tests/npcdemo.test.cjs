const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const { test } = require("node:test");

function setup(spawnHunter = true) {
    const handlers = new Map();
    const npcs = new Map();
    let tick;
    let nextId = 100;
    class Vector3 { constructor(x, y, z) { Object.assign(this, { x, y, z }); } }
    const rotation = { rotateVector: v => v };
    const players = [1, 2].map(id => ({ id, ready: true, canAct: true, virtualWorld: 5, position: new Vector3(0, id === 1 ? 0 : 7, 0), rotation }));
    const emit = (name, ...args) => { for (const fn of handlers.get(name) ?? []) fn(...args); };
    const globals = {
        console: { log() {} }, Vector3, Quaternion: { fromAxisAngle: () => rotation },
        Player: { all: () => players },
        Events: { on: (name, fn) => handlers.set(name, [...(handlers.get(name) ?? []), fn]) },
        setInterval: fn => { tick = fn; return 1; }, clearInterval: () => { tick = undefined; },
        Npc: {
            roles: () => ["townsman", "guard"],
            getById: id => npcs.get(id) ?? null,
            create: params => {
                const npc = { ...params, id: nextId++, locomotion: params.locomotion ?? "native", alive: true, wearing: ["outfit"], intent: "hold", attacks: [],
                    getAnimalLoot() { return null; },
                    wear(items) { this.wearing = items; return true; },
                    attack(target, options) { this.attackOptions = options; this.attacks.push(target.id); this.intent = "attack"; return true; },
                    moveTo(point) { this.intent = "moveTo"; this.destination = point; this.status = "running"; },
                    lookAt() { this.intent = "lookAt"; },
                    pin(target) { this.simulator = target; }, hold() { this.intent = "hold"; },
                    remove() { npcs.delete(this.id); emit("npcDestroy", this); },
                };
                npcs.set(npc.id, npc); return npc;
            },
        },
    };
    function load(file) {
        const context = { ...globals, exports: {}, require: name => load(path.resolve(path.dirname(file), name)) };
        vm.runInNewContext(fs.readFileSync(file, "utf8"), context);
        return context.exports;
    }
    const mod = load(path.join(__dirname, "../dist/server/commands/npcdemo.js"));
    mod.installNpcDemoHandlers();
    const run = (subcommand, ...args) => mod.npcDemoCommand.run({ player: players[0], args: [subcommand, ...args], reply: { line() {} }, usage() { throw Error("unexpected usage"); } });
    if (spawnHunter) run("hunter");
    return { players, npcs, hunter: [...npcs.values()][0], emit, run, tick: () => tick?.(), stopped: () => tick === undefined };
}

test("hunter equips its role, selects nearest player, and retains that target", () => {
    const s = setup();
    assert.equal(s.hunter.wearing.length, 2);
    assert.equal(s.hunter.outfit, "d5b503bb-b0f4-4f40-9ef1-52d7f9997c7d");
    assert.equal(s.hunter.virtualWorld, 5);
    s.tick(); assert.deepEqual(s.hunter.attacks, [2]);
    assert.equal(s.hunter.simulator, undefined);
    s.players[0].position.y = 8;
    s.tick(); assert.deepEqual(s.hunter.attacks, [2]);
});

test("hunter loses distant or inactive targets without pinning simulation", () => {
    const s = setup(); s.tick();
    s.players[1].position.y = 100;
    s.players[0].canAct = false;
    s.tick(); assert.equal(s.hunter.intent, "hold"); assert.equal(s.hunter.simulator, undefined);
    s.players[0].canAct = true; s.players[0].virtualWorld = 9;
    s.tick(); assert.deepEqual(s.hunter.attacks, [2]);
    s.players[0].virtualWorld = 5;
    s.tick(); assert.deepEqual(s.hunter.attacks, [2, 1]);
});

test("replacement, reset, owner disconnect and resource stop clean up hunters", () => {
    for (const action of ["reset", "disconnect", "stop", "replace"]) {
        const s = setup(); s.tick();
        if (action === "reset") s.run("reset");
        if (action === "disconnect") s.emit("playerDisconnect", s.players[0]);
        if (action === "stop") { s.emit("resourceStop", "unrelated"); assert.equal(s.stopped(), false); s.emit("resourceStop", "kcdc-gamemode"); assert.equal(s.stopped(), true); }
        if (action === "replace") s.run("hunter");
        assert.equal(s.npcs.has(s.hunter.id), false);
        assert.equal(s.npcs.size, action === "replace" ? 1 : 0);
    }
});

test("hunter returns to its pre-combat position and ignores targets beyond its leash", () => {
    const s = setup(); s.tick();
    const home = { ...s.hunter.position };
    s.hunter.position.y += 61;
    s.players[1].position.y = s.hunter.position.y + 1;
    s.tick();
    assert.equal(s.hunter.intent, "moveTo");
    assert.deepEqual({ ...s.hunter.destination }, home);
    s.tick(); assert.equal(s.hunter.attacks.length, 1);
    s.hunter.position = s.hunter.destination;
    s.tick(); assert.equal(s.hunter.intent, "attack"); // Already home: the nearby caller is eligible.
    s.players[1].position.y = home.y + 1;
    s.tick(); assert.equal(s.hunter.attacks.length, 2);
});

test("an idle move becomes the leash anchor for the next fight", () => {
    const s = setup();
    s.players.forEach(p => { p.canAct = false; });
    s.hunter.position.y += 100;
    s.tick();
    const home = { ...s.hunter.position };
    s.players[1].canAct = true; s.players[1].position.y = home.y + 1;
    s.tick(); assert.deepEqual(s.hunter.attacks, [2]);
    s.hunter.position.y += 61;
    s.tick(); assert.deepEqual({ ...s.hunter.destination }, home);
});


test("a returning hunter may fight inside its original leash without moving the anchor", () => {
    const s = setup(); s.tick();
    const home = { ...s.hunter.position };
    s.hunter.position.y = home.y + 61;
    s.players[1].position.y = home.y + 62;
    s.tick(); assert.equal(s.hunter.intent, "moveTo");
    s.hunter.position.y = home.y + 45;
    s.players[1].position.y = home.y + 50;
    s.tick(); assert.equal(s.hunter.intent, "attack");
    s.hunter.position.y = home.y + 61;
    s.tick(); assert.equal(s.hunter.intent, "moveTo");
    assert.deepEqual({ ...s.hunter.destination }, home);
});

test("bow hunter equips a bow and arrows and requests ranged combat", () => {
    const s = setup(); s.run("hunter", "bow"); s.tick();
    const bow = [...s.npcs.values()][0];
    assert.equal(bow.name, "Bow hunter");
    assert.equal(bow.wearing.length, 3);
    assert.equal(bow.attackOptions.weapon, "bow");
});


test("crowd varies roles and orders while keeping native locomotion", () => {
    const s = setup(false);
    s.run("crowd", "8");
    const crowd = [...s.npcs.values()];
    assert.equal(crowd.length, 8);
    assert.equal(new Set(crowd.map(npc => npc.soul)).size, 2);
    assert(crowd.some(npc => npc.intent === "moveTo"));
    assert(crowd.some(npc => npc.intent === "lookAt"));
    assert(crowd.every(npc => npc.locomotion === "native"));
});
