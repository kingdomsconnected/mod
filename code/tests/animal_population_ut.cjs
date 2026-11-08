const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const { test } = require("node:test");

function setup(overrides = {}, installed = false) {
    let now = 0;
    let nextId = 1;
    const npcs = new Map();
    const handlers = new Map();
    const players = [{ id: 100, ready: true, canAct: true, virtualWorld: 0, position: { x: 100, y: 0, z: 0 } }];
    let offset = 0;
    let calls = 0;
    let population;
    let interval;
    const context = {
        exports: {}, console: { log() {} },
        Player: { all: () => players },
        Navigation: {
            ready: true,
            closestPoint: point => point,
            randomPoint: () => ({ x: offset++ * 3, y: 0, z: 0 }),
        },
        Inventory: { set(npc, stock) { npc.stock = stock; return { ok: true }; } },
        Events: { on: (event, handler) => handlers.set(event, handler) },
        setInterval(callback) { interval = callback; return 1; }, clearInterval() { interval = null; },
        Npc: {
            animalSpawnpoints: () => [], getById: id => npcs.get(id) ?? null,
            create(params) {
                calls++;
                const npc = { ...params, id: nextId++, alive: true, status: "idle", intent: "hold", moves: [], attacks: [],
                    getAnimalLoot() { return [{ item: "meat", amount: 8, fraction: 1, variation: 0, food: true, harvester: false }]; },
                    moveTo(point) { this.moves.push(point); this.status = "running"; return true; },
                    attack(target) { this.attacks.push(target.id); this.intent = "attack"; return true; },
                    hold() { this.intent = "hold"; this.status = "idle"; },
                    remove() { npcs.delete(this.id); population.lost(this.id, false); },
                };
                npcs.set(npc.id, npc); return npc;
            },
        },
    };
    function load(file) {
        const module = { ...context, exports: {}, require: name => load(path.resolve(path.dirname(file), name)) };
        vm.runInNewContext(fs.readFileSync(file, "utf8"), module);
        return module.exports;
    }
    context.exports = load(path.join(path.dirname(process.argv[2]), "animals.js"));
    const settings = { ...context.exports.animalPopulationSettings, ...overrides };
    population = installed ? context.exports.animalPopulation : new context.exports.AnimalPopulation(settings, () => now, () => 0);
    const point = { guid: "farm", level: "kutnohorsko", actorClass: "SheepEwe", soul: "sheep", count: 1, position: { x: 0, y: 0, z: 0 } };
    return { population, context, settings, npcs, players, point, handlers, calls: () => calls, advance(ms) { now += ms; }, tick() { population.tick(); }, runInterval() { interval?.(); } };
}

test("all sites can register while live animals and corpses share the body cap", () => {
    const s = setup({ maxAnimals: 4 });
    assert.equal(s.population.add({ ...s.point, count: 100 }, 0), true);
    assert.equal(s.population.add({ ...s.point, guid: "second", count: 100 }, 0), true);
    assert.equal(s.population.add(s.point, 0), false);
    s.tick();
    assert.equal(s.npcs.size, s.settings.spawnsPerTick);
    s.tick();
    assert.equal(s.npcs.size, 4);
    const corpse = [...s.npcs.values()][0];
    corpse.alive = false;
    s.population.lost(corpse.id, true);
    s.advance(15_000); s.tick();
    assert.equal(s.calls(), 4);
    s.advance(75_000); s.tick();
    assert.equal(s.npcs.has(corpse.id), false);
    assert.equal(s.npcs.size, 4);
    assert.equal(s.calls(), 5);
});

test("default installation waits for navigation then populates authored sites", () => {
    const s = setup({}, true);
    const distant = Array.from({ length: 300 }, (_, index) => ({ ...s.point, guid: `distant-${index}`, position: { x: 2000 + index * 100, y: 0, z: 0 } }));
    s.context.Npc.animalSpawnpoints = () => [...distant, s.point, { ...s.point, guid: "empty", count: 0 }];
    s.context.Navigation.ready = false;
    let queries = 0;
    s.context.Navigation.closestPoint = point => { queries++; return point; };
    s.context.exports.installAnimalPopulation();
    s.runInterval();
    assert.equal(queries, 0);
    assert.equal(s.calls(), 0);
    s.context.Navigation.ready = true;
    s.runInterval();
    assert.equal(s.calls(), 1);
    s.population.clear();
    s.runInterval();
    assert.equal(s.calls(), 1);
    s.handlers.get("resourceStop")("kcdc-gamemode");
    assert.equal(s.population.add(s.point, 0), false);
});

test("default capacity supports hundreds of animals without exceeding tick budgets", () => {
    const s = setup();
    s.players[0].position = { x: 100, y: -150, z: 0 };
    let queries = 0;
    s.context.Navigation.closestPoint = point => { queries++; return point; };
    s.context.Navigation.randomPoint = point => { queries++; return point; };
    for (let index = 0; index < 300; index++) {
        s.population.add({ ...s.point, guid: `site-${index}`, position: { x: index % 20 * 10, y: Math.floor(index / 20) * 10, z: 0 } }, 0);
    }
    for (let tick = 0; tick < 300; tick++) {
        queries = 0;
        const before = s.calls();
        s.tick();
        assert.ok(queries <= s.settings.queriesPerTick);
        assert.ok(s.calls() - before <= s.settings.spawnsPerTick);
        assert.ok(s.npcs.size <= 256);
        s.advance(1000);
    }
    assert.equal(s.npcs.size, 256);
});

test("explicit empty startup selection disables automatic populations", () => {
    const s = setup({}, true);
    s.context.Npc.animalSpawnpoints = () => [s.point];
    s.context.exports.animalSpawnpoints = [];
    s.context.exports.installAnimalPopulation();
    s.runInterval();
    assert.equal(s.calls(), 0);
    assert.equal(s.population.add(s.point, 0), true);
    s.runInterval();
    assert.equal(s.calls(), 1);
    s.handlers.get("resourceStop")("kcdc-gamemode");
    assert.equal(s.npcs.size, 0);
});

test("explicit startup GUIDs restrict the automatic population", () => {
    const s = setup({}, true);
    s.context.Npc.animalSpawnpoints = () => [s.point, { ...s.point, guid: "selected", soul: "selected-sheep" }];
    s.context.exports.animalSpawnpoints = ["selected"];
    s.context.exports.installAnimalPopulation();
    s.runInterval();
    assert.equal(s.calls(), 1);
    assert.equal([...s.npcs.values()][0].soul, "selected-sheep");
});

test("distant animals release capacity for other sites and can return", () => {
    const s = setup({ maxAnimals: 1 });
    s.context.Navigation.randomPoint = point => point;
    s.population.add(s.point, 0);
    s.population.add({ ...s.point, guid: "far", position: { x: 1000, y: 0, z: 0 } }, 0);
    s.tick();
    assert.equal([...s.npcs.values()][0].position.x, 0);
    s.players[0].position.x = 1100;
    s.advance(15_000); s.tick(); s.tick();
    assert.equal(s.npcs.size, 1);
    assert.equal([...s.npcs.values()][0].position.x, 1000);
    s.players[0].position.x = 100;
    s.advance(15_000); s.tick(); s.tick();
    assert.equal(s.npcs.size, 1);
    assert.equal([...s.npcs.values()][0].position.x, 0);
});

test("distance cleanup preserves animals near a player in an overlapping world", () => {
    for (const world of [0, 0xFFFFFFFF]) {
        const s = setup();
        s.population.add(s.point, 0); s.tick();
        const npc = [...s.npcs.values()][0];
        npc.position = { x: 1000, y: 0, z: 0 };
        s.players[0].position.x = 1100;
        s.players[0].virtualWorld = world;
        s.tick();
        assert.equal(s.npcs.has(npc.id), true);
        s.players[0].position.x = 100;
        s.tick();
        assert.equal(s.npcs.has(npc.id), true);
        s.players[0].virtualWorld = 1;
        s.tick();
        assert.equal(s.npcs.size, 0);
    }
});

test("initial spawns wait for nearby players to move away, and stay in their world", () => {
    const s = setup();
    s.population.add(s.point, 0);
    s.players[0].position.x = 10;
    s.tick(); assert.equal(s.calls(), 0);
    s.players[0].position.x = 100;
    s.players[0].virtualWorld = 1;
    s.advance(15_000); s.tick(); assert.equal(s.calls(), 0);
    s.players[0].virtualWorld = 0;
    s.advance(15_000); s.tick(); assert.equal(s.calls(), 1);
});

test("death keeps the corpse, repeats do not restart cooldown, replacement waits until hidden", () => {
    const s = setup();
    s.population.add(s.point, 0); s.tick();
    const corpse = [...s.npcs.values()][0];
    corpse.alive = false;
    s.population.lost(corpse.id, true);
    s.advance(60_000); s.population.lost(corpse.id, true); s.tick();
    assert.equal(s.npcs.has(corpse.id), true);
    s.players[0].position.x = 10;
    s.advance(240_000); s.tick();
    assert.equal(s.npcs.has(corpse.id), true);
    assert.equal(s.calls(), 1);
    s.players[0].position.x = 100;
    s.tick();
    assert.equal(s.npcs.has(corpse.id), false);
    assert.equal(s.calls(), 2);
    assert.equal(s.npcs.size, 1);
});

test("global-world visibility also gates spawning and corpse removal", () => {
    for (const [playerWorld, siteWorld] of [[0xFFFFFFFF, 0], [7, 0xFFFFFFFF]]) {
        const s = setup();
        s.players[0].virtualWorld = playerWorld;
        s.players[0].position.x = 10;
        s.population.add(s.point, siteWorld);
        s.tick(); assert.equal(s.calls(), 0);
        s.players[0].position.x = 100;
        s.advance(15_000); s.tick(); assert.equal(s.calls(), 1);
        const corpse = [...s.npcs.values()][0];
        corpse.alive = false;
        s.population.lost(corpse.id, true);
        s.players[0].position.x = 10;
        s.advance(300_000); s.tick();
        assert.equal(s.npcs.has(corpse.id), true);
        assert.equal(s.calls(), 1);
    }
});

test("unexpected removal schedules one delayed replacement", () => {
    const s = setup();
    s.population.add(s.point, 0); s.tick();
    s.npcs.clear(); s.tick();
    s.advance(299_999); s.tick(); assert.equal(s.calls(), 1);
    s.advance(1); s.tick(); assert.equal(s.calls(), 2);
});

test("missing mesh and failed placement retry without allocating NPCs", () => {
    const s = setup();
    s.population.add(s.point, 0);
    s.context.Navigation.ready = false;
    s.tick(); assert.equal(s.calls(), 0);
    s.context.Navigation.ready = true;
    s.context.Navigation.closestPoint = () => null;
    s.tick(); assert.equal(s.calls(), 0);
    s.context.Navigation.closestPoint = point => point;
    s.advance(14_999); s.tick(); assert.equal(s.calls(), 0);
    s.advance(1); s.tick(); assert.equal(s.calls(), 1);
});

test("one query per tick can project a home and spawn on a later tick", () => {
    const s = setup({ queriesPerTick: 1 });
    let queries = 0;
    s.context.Navigation.closestPoint = point => { queries++; return point; };
    s.context.Navigation.randomPoint = point => { queries++; return point; };
    s.population.add(s.point, 0);
    s.tick();
    assert.equal(queries, 1);
    assert.equal(s.calls(), 0);
    queries = 0;
    s.advance(15_000); s.tick();
    assert.equal(queries, 1);
    assert.equal(s.calls(), 1);
});

test("returning to a known home does not spend a random-point query", () => {
    const s = setup();
    s.population.add(s.point, 0); s.tick();
    const npc = [...s.npcs.values()][0];
    npc.position = { x: 50, y: 0, z: 0 };
    s.settings.queriesPerTick = 0;
    s.context.Navigation.randomPoint = () => { assert.fail("returning home needs no point query"); };
    s.advance(3000); s.tick();
    assert.equal(npc.moves.length, 1);
    assert.equal(npc.moves[0], s.point.position);
});

test("roaming samples navigation and does not replace an active route", () => {
    const s = setup();
    s.population.add(s.point, 0); s.tick();
    const npc = [...s.npcs.values()][0];
    s.advance(3000); s.tick(); assert.equal(npc.moves.length, 1);
    s.advance(30_000); s.tick(); assert.equal(npc.moves.length, 1);
    npc.status = "blocked";
    s.tick(); assert.equal(npc.moves.length, 2);
});

test("wolves acquire a nearby live target and stop when it leaves the leash", () => {
    const s = setup();
    s.population.add({ ...s.point, actorClass: "Wolf" }, 0); s.tick();
    const wolf = [...s.npcs.values()][0];
    s.players[0].position.x = 12;
    s.tick(); assert.deepEqual(wolf.attacks, [100]);
    s.tick(); assert.deepEqual(wolf.attacks, [100]);
    s.players[0].position.x = 100;
    s.tick(); assert.equal(wolf.intent, "hold");
});

test("wolves retain their target and retry a blocked attack with a delay", () => {
    const s = setup();
    s.population.add({ ...s.point, actorClass: "Wolf" }, 0); s.tick();
    const wolf = [...s.npcs.values()][0];
    s.players[0].position.x = 12;
    s.tick();
    s.players.push({ ...s.players[0], id: 101, position: { x: 5, y: 0, z: 0 } });
    wolf.status = "blocked";
    s.advance(14_999); s.tick();
    assert.deepEqual(wolf.attacks, [100]);
    s.advance(1); s.tick();
    assert.deepEqual(wolf.attacks, [100, 100]);
});

test("stopping or clearing from npcSpawn does not leave an untracked animal", () => {
    for (const method of ["stop", "clear"]) {
        const s = setup();
        const create = s.context.Npc.create;
        s.context.Npc.create = params => {
            const npc = create(params);
            s.population[method]();
            return npc;
        };
        s.population.add({ ...s.point, count: 4 }, 0);
        s.tick();
        assert.equal(s.npcs.size, 0);
        assert.equal(s.calls(), 1);
    }
});

test("clearing from corpse removal cannot respawn a cleared site", () => {
    const s = setup();
    s.population.add(s.point, 0); s.tick();
    const corpse = [...s.npcs.values()][0];
    const remove = corpse.remove;
    corpse.remove = () => { remove.call(corpse); s.population.clear(); };
    corpse.alive = false;
    s.population.lost(corpse.id, true);
    s.advance(300_000); s.tick();
    assert.equal(s.npcs.size, 0);
    assert.equal(s.calls(), 1);
});

test("stopping removes only this sample's bodies and forbids future respawns", () => {
    const s = setup();
    s.population.add(s.point, 0); s.tick();
    s.npcs.set(999, { id: 999 });
    s.population.stop();
    assert.deepEqual([...s.npcs.keys()], [999]);
    s.advance(600_000); s.tick(); assert.equal(s.calls(), 1);
    assert.equal(s.population.add(s.point, 0), false);
});


test("population prepares loot once per new body and keeps it through death", () => {
    const s = setup();
    let writes = 0;
    const set = s.context.Inventory.set;
    s.context.Inventory.set = (npc, stock) => { writes++; return set(npc, stock); };
    s.population.add(s.point, 0); s.tick();
    const corpse = [...s.npcs.values()][0];
    assert.equal(corpse.stock.items[0].item, "meat");
    assert.equal(corpse.stock.items[0].amount, 8);
    assert.equal(corpse.stock.harvested, false);
    s.tick(); corpse.alive = false; s.population.lost(corpse.id, true); s.tick();
    assert.equal(writes, 1);
    s.advance(300_000); s.tick();
    assert.equal(writes, 2, "replacement gets its own prepared stock");
});

test("loot preparation failure removes the body and retains the spawn cooldown", () => {
    const s = setup();
    s.context.Inventory.set = () => ({ ok: false, code: "inventoryUnavailable" });
    s.population.add(s.point, 0); s.tick();
    assert.equal(s.npcs.size, 0);
    s.tick(); assert.equal(s.calls(), 1);
    s.advance(15_000); s.tick(); assert.equal(s.calls(), 2);
    assert.equal(s.npcs.size, 0);
});
