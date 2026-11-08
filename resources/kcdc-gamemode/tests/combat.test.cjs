const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const { test } = require("node:test");

function setup(timeout) {
    const handlers = new Map();
    const clients = new Map();
    const cancelled = [];
    let now = 1000;
    let tick;
    const emit = (name, ...args) => { for (const fn of handlers.get(name) ?? []) fn(...args); };
    const context = {
        exports: {}, Date: { now: () => now },
        setInterval: (fn) => { tick = fn; return 1; }, clearInterval: () => { tick = undefined; },
        Events: {
            on: (name, fn) => handlers.set(name, [...(handlers.get(name) ?? []), fn]),
            onClient: (name, fn) => clients.set(name, fn),
        },
    };
    vm.runInNewContext(fs.readFileSync(path.join(__dirname, "../dist/server/combat.js"), "utf8"), context);
    context.exports.installCombat(timeout);
    const player = (id) => ({
        id, nickname: `Player${id}`, ready: true, canAct: true, virtualWorld: 0, state: [],
        emit(_name, data) { this.state = JSON.parse(data); },
        cancelCombat(other) { cancelled.push([id, other.id]); emit("playerCombatCancelled", this, other); return true; },
    });
    const players = [player(1), player(2), player(3)];
    // The native event transport parses JSON before dispatching onClient.
    const vote = (player, offer = player.state[0]) => clients.get("kcdc-gamemode:combat.vote")(player, { opponent: offer.opponent, token: offer.token });
    return { players, cancelled, emit, vote, clients, advance: (ms) => { now += ms; tick?.(); }, stopped: () => !tick };
}

test("45 seconds of inactivity from either player; a missed swing renews the deadline", () => {
    const s = setup(); const [a, b] = s.players;
    s.emit("playerAttack", a, b);
    s.advance(44999); assert.equal(s.cancelled.length, 0);
    s.emit("playerAttack", b, a);
    s.advance(44999); assert.equal(s.cancelled.length, 0);
    s.advance(1); assert.deepEqual(s.cancelled, [[a.id, b.id]]);
    assert.equal(a.state.length, 0); assert.equal(b.state.length, 0);
});

test("zero disables the timer but leaves mutual consent available", () => {
    const s = setup(0); const [a, b] = s.players;
    s.emit("playerAttack", a, b); s.advance(1_000_000);
    assert.equal(s.cancelled.length, 0);
    s.vote(a); s.vote(a);
    assert.equal(s.cancelled.length, 0);
    assert.equal(a.state[0].voted, true); assert.equal(b.state[0].offered, true);
    s.vote(b); assert.equal(s.cancelled.length, 1);
});

test("an attack withdraws the offer and makes in-flight accept requests stale", () => {
    const s = setup(); const [a, b] = s.players;
    s.emit("playerAttack", a, b); s.vote(a);
    const old = b.state[0];
    s.emit("playerAttack", b, a); s.vote(b, old);
    assert.equal(s.cancelled.length, 0);
    assert.equal(a.state[0].voted, false); assert.equal(b.state[0].offered, false);
    s.vote(b); assert.equal(s.cancelled.length, 0);
    s.vote(a); assert.equal(s.cancelled.length, 1);
});

test("a third player cannot vote for a pair and its own fight survives cancellation", () => {
    const s = setup(); const [a, b, c] = s.players;
    s.emit("playerAttack", a, b); s.emit("playerAttack", a, c);
    s.vote(c, { ...a.state.find((fight) => fight.opponent === b.id), opponent: a.id });
    assert.equal(b.state[0].offered, false);
    s.vote(a, a.state.find((fight) => fight.opponent === b.id)); s.vote(b);
    assert.deepEqual(s.cancelled, [[a.id, b.id]]);
    assert.equal(a.state[0].opponent, c.id); assert.equal(c.state[0].opponent, a.id);
});

test("death, disconnect, spawn and world changes retire consent even without a timer", () => {
    for (const event of ["playerDied", "playerDisconnect", "playerSpawning", "world"] ) {
        const s = setup(0); const [a, b] = s.players;
        s.emit("playerAttack", a, b); s.vote(a);
        const old = b.state[0];
        if (event === "world") { a.virtualWorld = 1; s.advance(250); }
        else s.emit(event, a);
        assert.equal(b.state.length, 0);
        s.vote(b, old); assert.equal(s.cancelled.length, 1);
    }
});

test("contacts with a different victim start their own pair; missile hits do not", () => {
    const s = setup(); const [a, b, c] = s.players;
    s.emit("playerAttack", a, b);
    s.emit("playerHit", c, a, { source: "missile" }); assert.equal(c.state.length, 0);
    s.emit("playerHit", c, a, { source: "melee" }); assert.equal(c.state[0].opponent, a.id);
});

test("manual API cancellation and resource stop clear hints and timers", () => {
    const s = setup(); const [a, b, c] = s.players;
    s.emit("playerAttack", a, b); a.cancelCombat(b);
    assert.equal(b.state.length, 0);
    s.emit("playerAttack", a, c); s.emit("resourceStop", "another-resource");
    assert.equal(s.stopped(), false);
    s.emit("resourceStop", "kcdc-gamemode");
    assert.equal(s.stopped(), true); assert.equal(c.state.length, 0);
});

test("malformed requests cannot create fights or consent", () => {
    const s = setup(); const [a, b] = s.players;
    s.emit("playerAttack", a, b);
    for (const payload of ["{", "null", "[]", '{"opponent":2,"token":"1"}', "x".repeat(129)]) s.clients.get("kcdc-gamemode:combat.vote")(a, payload);
    assert.equal(a.state[0].voted, false); assert.equal(s.cancelled.length, 0);
});

test("hold U displays the vote and an old hold cannot accept a new opponent", () => {
    const handlers = new Map(); const hints = new Map(); const requests = [];
    let tick; let nextHint = 1;
    const players = new Map([[1, { position: { x: 0, y: 0, z: 0 }, canAct: true }], [2, { ready: true, position: { x: 2, y: 0, z: 0 } }], [3, { ready: true, position: { x: 1, y: 0, z: 0 } }]]);
    const context = {
        exports: {}, LocalPlayer: players.get(1), Player: function(id) { return players.get(id); },
        Events: { on: (name, fn) => handlers.set(name, fn), emitServer: (...args) => requests.push(args) },
        setInterval: (fn) => { tick = fn; return 1; }, clearInterval() {},
        ActionHint: {
            create: (options, fn) => { const id = nextHint++; hints.set(id, { options, fn }); return id; },
            remove: (id) => hints.delete(id), setText: (id, text) => { hints.get(id).options.text = text; },
            setEnabled: (id, enabled) => { hints.get(id).options.enabled = enabled; },
        },
    };
    vm.runInNewContext(fs.readFileSync(path.join(__dirname, "../dist/client/combat.js"), "utf8"), context);
    context.exports.installCombat();
    const state = (fights) => handlers.get("kcdc-gamemode:combat.state")(JSON.stringify(fights));
    const fight = { opponent: 2, name: "Player2", token: 1, voted: false, offered: false };
    state([fight]); const old = [...hints.values()][0];
    assert.equal(old.options.key, "kcdc_end_combat"); assert.equal(old.options.holdDuration, 1);
    old.fn(); assert.deepEqual(JSON.parse(JSON.stringify(requests.at(-1)[1])), { opponent: 2, token: 1 });
    state([{ ...fight, voted: true }]);
    assert.match([...hints.values()][0].options.text, /waiting \(1\/2\)/);
    assert.equal([...hints.values()][0].options.enabled, false);
    state([{ ...fight, offered: true }]); assert.match([...hints.values()][0].options.text, /Accept peace/);
    state([{ ...fight, opponent: 3, token: 2 }]); const count = requests.length;
    old.fn(); assert.equal(requests.length, count);
    state([]); tick(); assert.equal(hints.size, 0);
});

test("NPC hits do not open a player truce offer", () => {
    const s = setup(); const [victim] = s.players;
    s.emit("playerHit", victim, { id: 40, name: "Hunter" }, { source: "melee" });
    assert.equal(victim.state.length, 0);
    s.advance(20000);
    assert.equal(s.cancelled.length, 0);
});
