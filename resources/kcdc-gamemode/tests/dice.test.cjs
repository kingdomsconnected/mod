const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const { test } = require("node:test");

function setup() {
    const handlers = new Map();
    const clients = new Map();
    const sessions = new Map();
    const players = [];
    const starts = [];
    const matches = new Map();
    const messages = new Map();
    const areas = [];
    let now = 1000;
    let nextSession = 1;
    let tick;
    let containsCalls = 0;
    let failOpen = false;
    let failStart = false;
    // table1quest is table1 again on a quest layer: same chairs, another GUID.
    // Tables a level edit took out of one world only: world -> ids.
    const removedIn = new Map();
    let tableRows = [{ id: "table1", position: { x: 0, y: 0, z: 0 }, seats: ["seat1", "seat2"] }, { id: "table2", position: { x: 20, y: 0, z: 0 }, seats: ["seat3", "seat4"] }, { id: "table1quest", position: { x: 0.3, y: 0, z: 0 }, seats: ["seat5", "seat6"] }];
    const emit = (name, ...args) => {
        for (const handler of handlers.get(name) ?? []) handler(...args);
    };
    // The server raises areaEnter/areaExit from its own tick, before scripts run.
    const crossings = () => {
        for (const area of areas) {
            for (const player of players) {
                const inside = area.exists && area.enabled && area.contains(player);
                if (inside === area.occupants.has(player)) continue;
                if (inside) area.occupants.add(player); else area.occupants.delete(player);
                emit(inside ? "areaEnter" : "areaExit", area, player, true);
            }
        }
    };
    class Player {
        static all() { return players; }
        static getById(id) { return players.find((player) => player.id === id) ?? null; }
    }
    const context = {
        exports: {},
        console,
        Date: { now: () => now },
        setInterval: (handler) => { tick = () => { crossings(); handler(); }; return 1; },
        clearInterval: () => { tick = undefined; },
        Vector3: class { constructor(x, y, z) { Object.assign(this, { x, y, z }); } },
        Player,
        Messages: { handle: (name, handler) => messages.set(name, handler) },
        Navigation: { ready: false },
        Crafting: { session: () => null },
        World: { entitiesInRadius: async () => ({ answered: true, entities: [{ guid: "table1" }, { guid: "table2" }] }) },
        Chat: { sendToPlayer: (player, line) => player.chat.push(line) },
        Events: {
            on: (name, handler) => handlers.set(name, [...(handlers.get(name) ?? []), handler]),
            onClient: (name, handler) => clients.set(name, handler),
        },
        Area: { create: (definition) => {
            const contains = (player) => Math.hypot(player.position.x - definition.position.x, player.position.y - definition.position.y, player.position.z - definition.position.z) <= definition.radius;
            const area = { ...definition, occupants: new Set(), exists: true, enabled: true, contains, containsPlayer: (player) => { containsCalls++; return contains(player); }, destroy: () => { area.exists = false; for (const player of [...area.occupants]) { area.occupants.delete(player); emit("areaExit", area, player, true); } } };
            areas.push(area);
            return area;
        } },
        Dice: {
            tables: (world) => world === undefined || !removedIn.has(world) ? tableRows : tableRows.filter((row) => !removedIn.get(world).has(row.id)),
            matchOf: (id) => matches.get(id) ?? 0,
            start: (first, second, options) => {
                if (failStart) throw new Error("Native start refused");
                starts.push({ first, second, score: options.targetScore, firstName: options.firstName, secondName: options.secondName });
                matches.set(first, starts.length);
                matches.set(second, starts.length);
                emit("diceMatchStart", starts.length, players.find((p) => p.id === first), players.find((p) => p.id === second));
                return starts.length;
            },
            stop: (match) => {
                for (const [id, current] of matches) if (current === match) matches.delete(id);
                emit("diceMatchEnd", match);
            },
        },
        Dialogue: {
            sessionOf: (id) => [...sessions].find(([, value]) => value.player.id === id)?.[0] ?? 0,
            open: (id, page) => {
                if (failOpen) return 0;
                const session = nextSession++;
                sessions.set(session, { player: players.find((player) => player.id === id), page });
                return session;
            },
            update: (session, page) => {
                if (!sessions.has(session)) return false;
                sessions.get(session).page = page;
                return true;
            },
            close: (session) => {
                const old = sessions.get(session);
                sessions.delete(session);
                if (old) emit("dialogueClosed", session, old.player, 0);
            },
        },
    };
    vm.runInNewContext(fs.readFileSync(path.join(__dirname, "../dist/server/dice.js"), "utf8"), context);
    const lobby = context.exports.diceLobby;
    lobby.install();
    const add = (id, seat = id === 1 ? "seat1" : "seat2", world = 0) => {
        const player = Object.assign(new Player(), { id, nickname: `Player ${id}`, ready: true, alive: true, mounted: false, health: 100, virtualWorld: world, position: { x: seat === "seat3" || seat === "seat4" ? 20 : 0, y: 0, z: 0 }, messages: [], chat: [], emit: (...args) => player.messages.push(args), teleport: (position) => { player.position = position; return true; } });
        players.push(player);
        return player;
    };
    const a = add(1);
    const b = add(2);
    tick();
    const choose = (player, option) => emit("dialogueChoice", context.Dialogue.sessionOf(player.id), player, option);
    return {
        a, b, add, lobby, starts, sessions, areas, emit, choose, context, players,
        freeDice: (player) => { let result; messages.get("playground:freeDice")({ player: player.id }, (value) => result = value); return result; },
        containsCalls: () => containsCalls,
        interactWith: (player, table) => clients.get("kcdc-gamemode:dice.interact")(player, { token: JSON.parse(player.messages.at(-1)[1]).token, table }),
        tick: () => tick?.(),
        advance: (ms) => { now += ms; tick?.(); },
        interact: (player, token = JSON.parse(player.messages.at(-1)[1]).token) => clients.get("kcdc-gamemode:dice.interact")(player, { token, table: JSON.parse(player.messages.at(-1)[1]).tables[0]?.id }),
        pair: () => { clients.get("kcdc-gamemode:dice.interact")(a, { token: JSON.parse(a.messages.at(-1)[1]).token, table: JSON.parse(a.messages.at(-1)[1]).tables[0]?.id }); clients.get("kcdc-gamemode:dice.interact")(b, { token: JSON.parse(b.messages.at(-1)[1]).token, table: JSON.parse(b.messages.at(-1)[1]).tables[0]?.id }); },
        tables: () => tableRows,
        setTables: (rows) => { tableRows = rows; },
        removeIn: (world, id) => { removedIn.set(world, new Set([...(removedIn.get(world) ?? []), id])); },
        failOpen: () => { failOpen = true; },
        failStart: () => { failStart = true; },
    };
}

test("approaching alone does not join; leaving range revokes an unacknowledged interaction", () => {
    const h = setup();
    assert.equal(h.sessions.size, 0);
    assert.equal(JSON.parse(h.a.messages.at(-1)[1]).joined, false);
    const token = JSON.parse(h.a.messages.at(-1)[1]).token;
    h.a.position.y = 4;
    h.interact(h.a, token);
    assert.equal(JSON.parse(h.a.messages.at(-1)[1]).tables.length, 0);
    h.interact(h.b);
    assert.equal(h.sessions.size, 0);
});

test("standing players join by interaction, can leave, and both must agree before being seated", () => {
    const h = setup();
    h.interact(h.b);
    h.interact(h.b);
    h.interact(h.a);
    assert.equal(h.sessions.size, 0);
    h.interact(h.b);
    assert.equal(h.sessions.size, 2);
    h.choose(h.b, "agree"); // Waiting player cannot agree or edit.
    h.choose(h.b, "raise");
    assert.equal(h.starts.length, 0);
    h.choose(h.a, "agree");
    assert.equal(h.starts.length, 0);
    h.choose(h.b, "agree");
    assert.deepEqual(h.starts, [{ first: 1, second: 2, score: 2000, firstName: "Player 1", secondName: "Player 2" }]);
    assert.equal(h.sessions.size, 0);
});

test("a counteroffer invalidates prior consent and returns to the other player", () => {
    const h = setup();
    h.pair();
    h.choose(h.a, "agree");
    h.choose(h.b, "raise");
    h.choose(h.b, "agree");
    assert.equal(h.starts.length, 0);
    h.choose(h.a, "lower");
    h.choose(h.a, "agree");
    assert.equal(h.starts.length, 0);
    h.choose(h.b, "agree");
    assert.equal(h.starts[0].score, 2000);
});

for (const action of ["quit", "escape", "disconnect", "death", "leave area", "world", "timeout", "replacement"]) {
    test(`waiting player can cancel through ${action}; stale choices cannot start a match`, () => {
        const h = setup();
        h.pair();
        const oldSession = h.context.Dialogue.sessionOf(h.a.id);
        if (action === "quit") h.choose(h.b, "quit");
        if (action === "escape" || action === "replacement") h.context.Dialogue.close(h.context.Dialogue.sessionOf(h.b.id));
        if (action === "disconnect") { h.emit("playerDisconnect", h.b); h.players.splice(1, 1); }
        if (action === "death") h.emit("playerDied", h.b);
        if (action === "leave area") h.b.position.x = 50;
        if (action === "world") h.b.virtualWorld = 1;
        if (action === "timeout") h.advance(60_001);
        h.tick();
        h.emit("dialogueChoice", oldSession, h.a, "agree");
        assert.equal(h.starts.length, 0);
        assert.equal(h.sessions.size, 0);
        h.tick();
        assert.equal(h.sessions.size, 0);
    });
}

test("different tables and worlds do not share a lobby", () => {
    for (const change of [(p) => { p.virtualWorld = 1; }, (p) => { p.position.x = 20; }]) {
        const h = setup();
        change(h.b);
        h.tick();
        h.pair();
        assert.equal(h.sessions.size, 0);
        assert.equal(h.starts.length, 0);
    }
});

test("a third nearby player cannot join or cancel another pair's negotiation", () => {
    const h = setup();
    const c = h.add(3, "seat1");
    h.tick();
    h.pair();
    h.interact(c);
    h.lobby.cancelFor(c);
    h.tick();
    assert.equal(h.sessions.size, 2);
    h.choose(h.a, "agree");
    h.choose(h.b, "agree");
    assert.equal(h.starts.length, 1);
});

test("old interaction tokens cannot join after walking away or reopen cancelled setup", () => {
    const h = setup();
    const oldToken = JSON.parse(h.a.messages.at(-1)[1]).token;
    h.a.position.x = 50;
    h.tick();
    h.interact(h.a, oldToken);
    h.a.position.x = 0;
    h.tick();
    h.interact(h.a, oldToken);
    h.interact(h.b);
    assert.equal(h.sessions.size, 0);
    const current = JSON.parse(h.a.messages.at(-1)[1]).token;
    h.interact(h.a);
    h.choose(h.b, "quit");
    h.interact(h.a, current);
    h.tick();
    assert.equal(h.sessions.size, 0);
});

test("one table on two quest layers is one lobby, whichever row each player joins through", () => {
    const h = setup();
    h.interactWith(h.a, "table1");
    h.interactWith(h.b, "table1quest");
    assert.equal(h.sessions.size, 2);
    h.choose(h.a, "agree");
    h.choose(h.b, "agree");
    assert.equal(h.starts.length, 1);
    const c = h.add(3);
    const d = h.add(4);
    h.tick();
    h.interactWith(c, "table1quest");
    h.interactWith(d, "table1");
    assert.equal(h.starts.length, 1);
    assert.equal(h.sessions.size, 0);
});

test("an unanswered invitation does not hold the table, and the tick skips players away from tables", async () => {
    const h = setup();
    await h.lobby.invite(h.a, h.b, 2000);
    const c = h.add(3);
    const d = h.add(4);
    h.tick();
    h.interact(c);
    h.interact(d);
    assert.equal(h.sessions.size, 2);
    assert.throws(() => h.lobby.accept(h.b), /occupied|finish/);
    // Crossings are the engine's; the lobby only asks about players at a table.
    const ticks = () => { const before = h.containsCalls(); for (let i = 0; i < 10; i++) h.tick(); return h.containsCalls() - before; };
    const baseline = ticks();
    for (let id = 10; id < 30; id++) h.add(id).position.x = 500;
    h.tick();
    assert.equal(ticks(), baseline);
});

test("invitations reserve players, expire, and acceptance only opens negotiation", async () => {
    const h = setup();
    await h.lobby.invite(h.a, h.b, 4000);
    await assert.rejects(h.lobby.invite(h.a, h.b, 5000), /already/);
    h.interact(h.a);
    assert.equal(h.sessions.size, 0);
    h.advance(60_001);
    assert.throws(() => h.lobby.accept(h.b), /Nobody/);
    await h.lobby.invite(h.a, h.b, 4000);
    h.lobby.accept(h.b);
    assert.equal(h.starts.length, 0);
    h.choose(h.a, "agree");
    h.choose(h.b, "agree");
    assert.equal(h.starts[0].score, 4000);
});

test("invitations reject unloaded tables and recheck player state after the client query", async () => {
    const h = setup();
    h.context.World.entitiesInRadius = async () => ({ answered: true, entities: [] });
    await assert.rejects(h.lobby.invite(h.a, h.b, 2000), /loaded dice table/);
    h.context.World.entitiesInRadius = async () => ({ answered: false, entities: [] });
    await assert.rejects(h.lobby.invite(h.a, h.b, 2000), /Try again/);
    h.context.World.entitiesInRadius = async () => {
        h.b.virtualWorld = 1;
        return { answered: true, entities: [{ guid: "table1" }] };
    };
    await assert.rejects(h.lobby.invite(h.a, h.b, 2000), /loaded dice table/);
    assert.equal(h.sessions.size, 0);
    assert.throws(() => h.lobby.accept(h.b), /Nobody/);
});

test("score boundaries, dialogue failure, and native start failure leave no pending setup", async () => {
    for (const [score, option] of [[100, "lower"], [100000, "raise"]]) {
        const h = setup();
        await h.lobby.invite(h.a, h.b, score);
        h.lobby.accept(h.b);
        h.choose(h.a, option);
        h.choose(h.a, "agree");
        h.choose(h.b, "agree");
        assert.equal(h.starts[0].score, score);
    }
    const failedDialogue = setup();
    failedDialogue.failOpen();
    failedDialogue.pair();
    assert.equal(failedDialogue.sessions.size, 0);
    const failedStart = setup();
    failedStart.failStart();
    failedStart.pair();
    failedStart.choose(failedStart.a, "agree");
    failedStart.choose(failedStart.b, "agree");
    assert.equal(failedStart.sessions.size, 0);
    assert.equal(failedStart.starts.length, 0);
    assert.match(failedStart.b.chat.at(-1), /Native start refused/);
});

test("match end requires fresh readiness and resource stop removes areas and dialogues", () => {
    const h = setup();
    h.pair();
    h.choose(h.a, "agree");
    h.choose(h.b, "agree");
    h.context.Dice.stop(1);
    h.tick();
    assert.equal(h.sessions.size, 0);
    h.pair();
    h.emit("resourceStop", "kcdc-gamemode");
    assert.equal(h.sessions.size, 0);
    assert.ok(h.areas.every((area) => !area.exists));
});

test("client Interact hint requires proximity, aim and a loaded station; joining needs no Space toggle", () => {
    const handlers = new Map();
    const hints = new Map();
    const requests = [];
    let nextHint = 1;
    let tick;
    let loaded = true;
    let hit = null;
    let raycasts = 0;
    let direction = { x: 0, y: 1, z: 0 };
    const context = {
        exports: {},
        setInterval: (handler) => { tick = handler; return 1; },
        clearInterval: () => { tick = undefined; },
        Vector3: class { constructor(x, y, z) { Object.assign(this, { x, y, z }); } },
        LocalPlayer: { ready: true, alive: true, virtualWorld: 0, position: { x: 0, y: -2, z: 0 } },
        Camera: { screenRay: () => ({ origin: { x: 0, y: -2, z: 1 }, direction }) },
        World: { raycast: () => { raycasts++; return hit; }, entitiesInRadius: () => loaded ? [{ guid: "table1" }, { guid: "farther" }] : [] },
        Events: { on: (name, handler) => handlers.set(name, handler), emitServer: (...args) => requests.push(args) },
        ActionHint: {
            create: (options, handler) => { const id = nextHint++; hints.set(id, { ...options, handler }); return id; },
            remove: (id) => hints.delete(id),
            setText: (id, text) => { hints.get(id).text = text; },
        },
    };
    const interaction = {};
    vm.runInNewContext(fs.readFileSync(path.join(__dirname, "../dist/client/interaction.js"), "utf8"), { ...context, exports: interaction });
    context.require = (name) => { assert.equal(name, "./interaction.js"); return interaction; };
    vm.runInNewContext(fs.readFileSync(path.join(__dirname, "../dist/client/dice.js"), "utf8"), context);
    context.exports.installDice();
    assert.equal(requests[0][0], "kcdc-gamemode:dice.sync");
    // The owner's client world can differ from the server's default-world sentinel.
    const receive = (token, joined, available = true) => handlers.get("kcdc-gamemode:dice.state")(JSON.stringify({ token, joined, world: 4294967295, tables: available ? [{ id: "farther", position: { x: 0, y: 0.5, z: 0 } }, { id: "inactive", position: { x: 0, y: -0.5, z: 0 } }, { id: "table1", position: { x: 0, y: 0, z: 0 } }] : [] }));
    receive(1, false);
    assert.equal(raycasts, 2); // Stop at the nearest valid table; do not trace the farther one.
    let hint = [...hints.values()][0];
    assert.equal(hint.key, "primary_action");
    assert.match(hint.text, /join lobby/);
    direction = { x: 0, y: -1, z: 0 };
    raycasts = 0;
    hint.handler(); // Recheck the current aim even before the next tick.
    assert.equal(requests.length, 1);
    tick();
    assert.equal(hints.size, 0);
    assert.equal(raycasts, 0); // Looking away never reaches the physics query.
    direction = { x: 0, y: 1, z: 0 };
    tick();
    hint = [...hints.values()][0];
    hint.handler();
    assert.equal(hints.size, 0);
    assert.equal(requests.at(-1)[0], "kcdc-gamemode:dice.interact");
    assert.equal(requests.at(-1)[1].token, 1);
    assert.equal(requests.at(-1)[1].table, "table1");
    hint.handler();
    assert.equal(requests.length, 2);
    receive(2, true);
    assert.match([...hints.values()][0].text, /Leave dice lobby/);
    for (const [invalidate, restore] of [
        [() => { loaded = false; }, () => { loaded = true; }],
        [() => { hit = { distance: 0.2 }; }, () => { hit = null; }],
        [() => { context.LocalPlayer.position.y = -4; }, () => { context.LocalPlayer.position.y = -2; }],
    ]) {
        invalidate(); tick(); assert.equal(hints.size, 0);
        restore(); tick(); assert.equal(hints.size, 1);
    }
    receive(3, true, false);
    assert.equal(hints.size, 0);
    context.LocalPlayer.position.y = -4;
    raycasts = 0;
    receive(4, false);
    tick();
    assert.equal(raycasts, 0); // Distant targets never reach the physics query either.
    context.LocalPlayer.position.y = -2;
    receive(4, false);
    handlers.get("resourceStop")("kcdc-gamemode");
    assert.equal(hints.size, 0);
    assert.equal(tick, undefined);
});

test("playground skips occupied physical tables and quest-layer duplicates", () => {
    const h = setup();
    h.a.position.x = 8;
    const result = h.freeDice(h.a);
    assert.equal(result.error, undefined);
    assert.equal(h.a.position.x, 22);
});

test("playground excludes waiting lobbies and keeps virtual worlds separate", () => {
    const h = setup();
    h.interact(h.b);
    h.a.position.x = 8;
    h.b.position.x = 100; // Lobby still owns its place until the next tick.
    assert.equal(h.freeDice(h.a).error, undefined);
    assert.equal(h.a.position.x, 22);
    const other = h.add(3, "seat1", 9);
    other.position.x = 8;
    assert.equal(h.freeDice(other).error, undefined);
    assert.equal(other.position.x, 2);
});

test("playground refuses movement while mounted or with every table occupied", () => {
    const h = setup();
    h.a.position.x = 8;
    h.a.mounted = true;
    assert.match(h.freeDice(h.a).error, /activity/);
    h.a.mounted = false;
    h.add(3, "seat3");
    assert.match(h.freeDice(h.a).error, /unoccupied/);
    assert.equal(h.a.position.x, 8);
});

test("tables placed or taken away after start are followed, and a setup at a vanished table ends", () => {
    const h = setup();
    h.setTables([...h.tables(), { id: "placed1", position: { x: 40, y: 0, z: 0 }, seats: ["seat7", "seat8"], placed: true }]);
    h.advance(2000);
    const area = h.areas.find((candidate) => candidate.id === "kcdc-gamemode:dice:placed1");
    assert.ok(area && area.exists);
    const c = h.add(3, "seat7");
    c.position = { x: 40, y: 0, z: 0 };
    h.tick();
    assert.equal(JSON.parse(c.messages.at(-1)[1]).tables[0]?.id, "placed1");
    h.interact(c);
    assert.equal(JSON.parse(c.messages.at(-1)[1]).joined, true);

    h.setTables(h.tables().filter((row) => row.id !== "placed1"));
    h.advance(2000);
    assert.equal(area.exists, false);
    assert.ok(c.chat.includes("The dice table is gone."));
    const state = JSON.parse(c.messages.at(-1)[1]);
    assert.equal(state.joined, false);
    assert.equal(state.tables.length, 0);
    // The level's own tables are untouched.
    assert.ok(h.areas.filter((candidate) => candidate.exists).length >= 3);
});

test("a table taken out of one world is not offered there, and still is in another", () => {
    const h = setup();
    h.removeIn(0, "table1");
    h.removeIn(0, "table1quest");
    h.advance(2000);
    assert.equal(JSON.parse(h.a.messages.at(-1)[1]).tables.length, 0);
    const c = h.add(3, "seat1", 7);
    h.tick();
    assert.equal(JSON.parse(c.messages.at(-1)[1]).tables[0]?.id, "table1");
});
