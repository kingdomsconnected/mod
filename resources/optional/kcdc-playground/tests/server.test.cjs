const { test } = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

function setup() {
    const events = new Map(), clients = new Map(), cache = new Map(), npcs = new Map(), horses = new Map(), dogs = new Map(), drops = new Map();
    const grants = [], teleports = [], knowledge = [], perks = [], vendorStocks = [], destroyedVendors = [];
    let now = 1000, serial = 1, responseId = 1;
    class Vector3 { constructor(x, y, z) { Object.assign(this, { x, y, z }); } }
    class Quaternion { constructor(x, y, z, w) { Object.assign(this, { x, y, z, w }); } rotateVector() { return new Vector3(0, 1, 0); } }
    const player = { id: 7, ready: true, alive: true, virtualWorld: 0, position: new Vector3(0, 0, 0), rotation: new Quaternion(0, 0, 0, 1), horse: null, dog: null, emit: (_name, payload) => { player.response = JSON.parse(payload); }, teleport: (p, name) => { teleports.push({ p, name }); return true; }, hasPerk: () => false, addPerk: (perk) => { perks.push(perk); return true; }, setAppearance: (appearance) => { player.appearance = appearance; return true; } };
    const players = new Map([[7, player]]);
    function entity(store, values = {}) { const row = { id: serial++, ...values, destroy() { store.delete(this.id); }, remove() { store.delete(this.id); }, setVirtualWorld(world) { this.virtualWorld = world; }, giveTo(owner) { this.ownerId = owner.id; }, hold() {}, follow() { this.followed = true; return true; } }; store.set(row.id, row); return row; }
    const api = {
        console: { log() {} }, Vector3, Quaternion, Date: { now: () => now },
        Player: { all: () => [...players.values()], getById: (id) => players.get(id) ?? null },
        Events: { on: (name, callback) => events.set(name, [...(events.get(name) ?? []), callback]), onClient: (name, callback) => clients.set(name, callback) },
        Inventory: { classes: () => [{ id: "sword", name: "Test sword", category: "MeleeWeapon", maximumQuality: 4 }, { id: "bow", name: "Test bow", category: "MissileWeapon", maximumQuality: 1 }], add: (p, request) => { grants.push({ player: p.id, ...request }); return { ok: true }; }, getItemPrice: () => ({ current: 10 }) },
        World: { level: "trosecko", pointsOfInterest: () => [{ name: "Troskowitz", group: "Map locations", level: "trosecko", position: { x: 2000, y: 2100, z: 100 } }], resolveGround: async (_p, position) => ({ answered: true, hit: { position, normal: { z: 1 } } }) },
        Npc: { roles: () => ["guard", "townswoman"], placements: () => [], create: (options) => entity(npcs, options), getById: (id) => npcs.get(id) ?? null },
        Horse: { spawn: (position) => entity(horses, { position }), getById: (id) => horses.get(id) ?? null },
        Dog: { spawn: (_p, position) => entity(dogs, { position }), getById: (id) => dogs.get(id) ?? null },
        GroundItem: { spawn: (item, position, _rotation, amount, virtualWorld, properties) => entity(drops, { item, position, amount, virtualWorld, properties }), getById: (id) => drops.get(id) ?? null },
        Vendor: { create: () => serial++, setStock: (_id, rows) => { vendorStocks.push(rows); return true; }, destroy: (id) => destroyedVendors.push(id) },
        Crafting: { recipes: (_p, kind) => [{ key: { kind, id: kind === "alchemy" ? 42 : "smith-1" }, name: "Test recipe", perk: "recipe-perk", minSkill: 5, ingredients: [{ item: "iron", name: "Iron", amount: 2, quest: false }] }, { key: { kind, id: "quest" }, ingredients: [{ item: "quest-item", amount: 1, quest: true }] }], knowledge: () => ({}), setKnowledge: (_p, id, mask) => { knowledge.push({ id, mask }); return true; }, session: () => null },
        Dice: { matchOf: () => 0 }, Messages: { request: async () => ({ message: "Moved to dice." }) },
    };
    const context = vm.createContext(api);
    function load(name) {
        const file = path.resolve(__dirname, "../dist/server", name);
        if (cache.has(file)) return cache.get(file).exports;
        const module = { exports: {} }; cache.set(file, module);
        const wrapper = vm.runInContext(`(function(require,module,exports){${fs.readFileSync(file, "utf8")}\n})`, context, { filename: file });
        wrapper((relative) => load(path.relative(path.resolve(__dirname, "../dist/server"), path.resolve(path.dirname(file), relative))), module, module.exports);
        return module.exports;
    }
    load("index.js");
    return { api, player, players, grants, teleports, knowledge, perks, npcs, horses, dogs, drops, vendorStocks, destroyedVendors,
        config: load("config.js").config,
        emit: async (name, ...args) => { for (const callback of events.get(name) ?? []) await callback(...args); },
        send: async (body, raw = false) => { now += 150; player.response = undefined; await clients.get("kcdc-playground:request")(player, raw ? body : { id: responseId++, ...body }); return player.response; },
    };
}

test("equippable catalog search is paged and rejects arbitrary classes and quality", async () => {
    const h = setup();
    let r = await h.send({ action: "items", query: "sword", page: 0 });
    assert.equal(r.items.length, 1); assert.equal(r.items[0].id, "sword");
    for (const body of [{ item: "quest" }, { item: "bow", quality: 2 }, { item: "sword", quality: 1.5 }, { item: "sword", amount: 21 }]) {
        r = await h.send({ action: "item", item: "sword", quality: 1, amount: 1, destination: "inventory", ...body }); assert.equal(r.ok, false);
    }
    assert.equal(h.grants.length, 0);
    r = await h.send({ action: "item", item: "sword", quality: 4, amount: 2, destination: "inventory" });
    assert.equal(r.ok, true); assert.equal(h.grants[0].metadata.quality, 4); assert.equal(h.grants[0].player, 7);
});

test("cursor grants use a checked floor and split equipment into unit replicas", async () => {
    const h = setup();
    const request = { action: "item", item: "sword", quality: 3, amount: 3, destination: "cursor", position: { x: 2, y: 2, z: 0 } };
    assert.equal((await h.send({ ...request, position: { x: 200, y: 0, z: 0 } })).ok, false);
    assert.equal((await h.send({ ...request, position: { x: NaN, y: 0, z: 0 } })).ok, false);
    assert.equal((await h.send(request)).ok, true);
    assert.equal(h.drops.size, 3);
    for (const row of h.drops.values()) { assert.equal(row.amount, 1); assert.equal(row.properties.quality, 3); assert.equal(row.virtualWorld, 0); }
    h.api.World.resolveGround = async () => ({ answered: false });
    assert.equal((await h.send(request)).ok, false); assert.equal(h.drops.size, 3);
});

test("disconnect and resource stop invalidate an outstanding ground query", async () => {
    for (const event of ["playerDisconnect", "resourceStop"]) {
        const h = setup(); let finish;
        h.api.World.resolveGround = () => new Promise((resolve) => finish = resolve);
        const pending = h.send({ action: "spawn", kind: "npc", role: "guard", follow: true });
        await h.emit(event, event === "resourceStop" ? "kcdc-playground" : h.player);
        finish({ answered: true, hit: { position: { x: 0, y: 3, z: 0 }, normal: { z: 1 } } });
        await pending; assert.equal(h.npcs.size, 0);
    }
});

test("world change invalidates an outstanding cursor grant", async () => {
    const h = setup(); let finish;
    h.api.World.resolveGround = () => new Promise((resolve) => finish = resolve);
    const pending = h.send({ action: "item", item: "sword", quality: 1, amount: 1, destination: "cursor", position: { x: 2, y: 0, z: 0 } });
    h.player.virtualWorld = 8;
    finish({ answered: true, hit: { position: { x: 2, y: 0, z: 0 }, normal: { z: 1 } } });
    await pending; assert.equal(h.drops.size, 0);
});

test("access, readiness, malformed input and concurrent requests cannot mutate", async () => {
    const h = setup();
    for (const invalid of ["{", "x".repeat(8193), null, [], { id: 1, action: "unknown" }]) assert.equal((await h.send(invalid, true)).ok, false);
    h.config.allowed = () => false;
    assert.equal((await h.send({ action: "creator" })).ok, false);
    h.config.allowed = () => true; h.player.ready = false;
    assert.equal((await h.send({ action: "creator" })).ok, false);
    h.player.ready = true; let finish;
    h.api.World.resolveGround = () => new Promise((resolve) => finish = resolve);
    const pending = h.send({ action: "spawn", kind: "npc", role: "guard" });
    assert.equal((await h.send({ action: "item", item: "sword", quality: 1, amount: 1, destination: "inventory" })).ok, false);
    finish({ answered: false }); await pending; assert.equal(h.grants.length, 0);
});

test("teleports accept only server-listed destinations and refuse busy players", async () => {
    const h = setup();
    const init = await h.send({ action: "init" }); assert.equal(init.level, "trosecko"); assert.equal(init.locations.length, 1);
    assert.equal((await h.send({ action: "teleport", location: "poi:9000" })).ok, false);
    assert.equal((await h.send({ action: "teleport", location: "poi:0" })).ok, true);
    assert.equal(h.teleports.length, 1);
    h.player.mounted = true; assert.equal((await h.send({ action: "teleport", location: "poi:0" })).ok, false);
});

test("crafting teaches only recipe knowledge and excludes quest ingredients", async () => {
    const h = setup();
    assert.equal((await h.send({ action: "recipes", kind: "alchemy" })).recipes.length, 1);
    assert.equal((await h.send({ action: "learn", kind: "alchemy", recipe: "all" })).ok, true);
    assert.deepEqual(h.knowledge, [{ id: 42, mask: 536870911 }]);
    await h.send({ action: "learn", kind: "smithing", recipe: "all" }); assert.deepEqual(h.perks, ["recipe-perk"]);
    await h.send({ action: "materials", kind: "smithing", recipe: "all", rounds: 3 });
    assert.equal(h.grants.length, 1); assert.equal(h.grants[0].item, "iron"); assert.equal(h.grants[0].amount, 6);
    assert.equal((await h.send({ action: "materials", kind: "alchemy", recipe: "quest", rounds: 1 })).ok, false);
    assert.equal((await h.send({ action: "materials", kind: "alchemy", recipe: "all", rounds: 0 })).ok, false);
});

test("owned companions are capped and cleanup preserves unrelated entities", async () => {
    const h = setup();
    h.npcs.set(999, { id: 999 });
    for (let i = 0; i < 4; i++) assert.equal((await h.send({ action: "spawn", kind: "npc", role: "guard", follow: true })).ok, true);
    assert.equal((await h.send({ action: "spawn", kind: "npc", role: "guard" })).ok, false);
    assert.equal([...h.npcs.values()].filter((row) => row.followed).length, 4);
    h.player.horse = { id: 55 }; assert.equal((await h.send({ action: "spawn", kind: "horse" })).ok, false); assert.equal(h.horses.size, 0);
    h.player.horse = null; await h.send({ action: "spawn", kind: "horse" }); await h.send({ action: "spawn", kind: "dog" });
    assert.equal(h.horses.size, 1); assert.equal(h.dogs.size, 1);
    await h.send({ action: "clear" }); assert.deepEqual([...h.npcs.keys()], [999]); assert.equal(h.horses.size, 0); assert.equal(h.dogs.size, 0);
});

test("appearance requires a creator session and can only be accepted once", async () => {
    const h = setup();
    const appearance = { gender: "male" };
    assert.equal((await h.send({ action: "appearance", appearance })).ok, false);
    await h.send({ action: "creator" }); assert.equal((await h.send({ action: "appearance", appearance })).ok, true);
    assert.equal((await h.send({ action: "appearance", appearance })).ok, false);
});

test("manifest ships both built entry points and the complete UI", () => {
    const root = path.resolve(__dirname, ".."); const manifest = require("../package.json");
    for (const entry of [...manifest.mafiahub.serverScripts, ...manifest.mafiahub.clientScripts, "ui/index.html", "ui/style.css", "ui/app.js"]) assert.ok(fs.existsSync(path.join(root, entry)), entry);
    assert.deepEqual(manifest.mafiahub.resourceDependencies, ["kcdc-gamemode"]);
});
