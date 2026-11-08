const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const { test } = require("node:test");

function setup() {
    const handlers = new Map(), hints = new Map(), started = [];
    let tick, nextId = 1, targets = [], active = null, pending = false, stops = 0;
    const context = {
        exports: {},
        require: () => ({ isInteractionTargetVisible: (position) => position.visible !== false }),
        LocalPlayer: { ready: true, alive: true, position: { x: 0, y: 0, z: 0 } },
        setInterval: (fn) => { tick = fn; return 1; }, clearInterval: () => { tick = undefined; },
        Events: { on: (name, fn) => handlers.set(name, [...(handlers.get(name) ?? []), fn]) },
        Follow: {
            targets: () => targets,
            canStart: (target) => !active && !pending && targets.some((t) => t.id === target.id && t.kind === target.kind && t.eligible !== false),
            start: (target) => { started.push(target); pending = true; return true; },
            stop: () => { stops++; active = null; pending = false; },
            getTarget: () => active, isPending: () => pending,
        },
        ActionHint: {
            create: (options, callback) => { const id = nextId++; hints.set(id, { ...options, callback }); return id; },
            remove: (id) => hints.delete(id), setText: (id, text) => { hints.get(id).text = text; },
        },
    };
    vm.runInNewContext(fs.readFileSync(path.join(__dirname, "../dist/client/follow.js"), "utf8"), context);
    context.exports.installFollow();
    return {
        started, hints, context,
        emit: (name, ...args) => { for (const fn of handlers.get(name) ?? []) fn(...args); },
        tick: () => tick?.(), stopped: () => !tick, stops: () => stops,
        targets: (value) => { targets = value; }, active: (value) => { active = value; pending = false; },
        press: () => hints.values().next().value.callback(), hint: () => hints.values().next().value,
    };
}
const target = (id, kind = "player", x = 2) => ({ id, kind, name: `${kind}${id}`, position: { x, y: 0, z: 0 } });

test("nearest eligible visible player or NPC uses the native Follow control", () => {
    const s = setup();
    s.targets([{ ...target(1), eligible: false }, target(2, "npc", 3), target(3, "player", 5)]);
    s.tick(); assert.equal(s.hint().text, "Follow npc2"); assert.equal(s.hint().key, "chat_focus_follow_init");
    s.press(); assert.equal(s.started[0].id, 2); assert.equal(s.hint().text, "Cancel follow");
});

test("press re-resolves a moved target and refuses it behind cover", () => {
    const s = setup(); s.targets([target(1)]); s.tick();
    s.targets([{ ...target(1), position: { x: 3, y: 0, z: 0, visible: false } }]);
    s.press(); assert.equal(s.started.length, 0); assert.equal(s.hints.size, 0);
});

test("a target disappearing before the press cannot start follow", () => {
    const s = setup(); s.targets([target(1)]); s.tick(); s.targets([]);
    s.press(); assert.equal(s.started.length, 0); assert.equal(s.hints.size, 0);
});

test("pending and active follow can be cancelled while looking away", () => {
    for (const entered of [false, true]) {
        const s = setup(); const t = target(1); s.targets([t]); s.tick(); s.press();
        if (entered) { s.active(t); s.emit("followStarted", t); }
        s.targets([]); s.tick(); assert.match(s.hint().text, entered ? /Stop following/ : /Cancel follow/);
        s.press(); assert.equal(s.stops(), 1); assert.equal(s.hints.size, 0);
    }
});

test("another resource's active follow does not get a misleading stop hint", () => {
    const s = setup(); const t = target(1); s.targets([t]); s.active(t); s.tick();
    assert.equal(s.hints.size, 0); assert.equal(s.stops(), 0);
});

test("resource shutdown clears the timer and hint", () => {
    const s = setup(); s.targets([target(1)]); s.tick(); s.press();
    s.emit("resourceStop", "other"); assert.equal(s.stopped(), false);
    s.emit("resourceStop", "kcdc-gamemode"); assert.equal(s.stopped(), true);
    assert.equal(s.hints.size, 0);
});
