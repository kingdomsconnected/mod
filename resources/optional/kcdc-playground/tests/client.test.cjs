const { test } = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

function setup() {
    const events = new Map(), page = new Map(), hints = [], requests = [], emissions = [], focus = [], removed = [];
    let now = 1000, tick, destroyed = 0, creatorOpen = false;
    const api = {
        Date: { now: () => now },
        LocalPlayer: { ready: true, alive: true, appearance: { gender: "male" } },
        Events: { on: (name, fn) => events.set(name, fn), emitServer: (name, body) => requests.push({ name, ...JSON.parse(body) }) },
        Web: {
            getScreenSize: () => ({ width: 1600, height: 900 }), createView: () => 1,
            on: (_id, name, fn) => page.set(name, fn), showView() {}, hideView() {},
            focusView: (_id, value) => focus.push(value), destroyView: () => destroyed++,
            emit: (_id, name, value) => emissions.push({ name, value }),
        },
        Camera: { screenRay: () => ({ origin: {}, target: {} }) },
        World: { raycast: () => ({ position: { x: 1, y: 2, z: 3 } }) },
        CharacterCreator: { isOpen: () => creatorOpen, open: () => { creatorOpen = true; return true; }, close: () => { creatorOpen = false; } },
        Hud: { showInfoText() {} },
        ActionHint: { create: (options, callback) => { hints.push({ options, callback }); return hints.length; }, setEnabled() {}, remove: (id) => removed.push(id) },
        setInterval: (fn) => { tick = fn; return 1; }, clearInterval: () => { tick = undefined; },
    };
    vm.runInNewContext(fs.readFileSync(path.join(__dirname, "../dist/client/index.js"), "utf8"), api);
    return { api, hints, requests, emissions, focus, removed,
        emit: (name, ...args) => events.get(name)?.(...args),
        page: (name, ...args) => page.get(name)?.(...args),
        advance: (ms) => { now += ms; tick?.(); },
        destroyed: () => destroyed, creatorOpen: () => creatorOpen,
    };
}

test("one playground hint opens the panel and only focuses a ready page", () => {
    const h = setup();
    assert.equal(h.hints.length, 1);
    assert.equal(h.hints[0].options.key, "kcdc_playground");
    h.hints[0].callback(); assert.equal(h.focus.length, 0);
    h.page("playground:ready"); assert.equal(h.focus.at(-1), true);
    assert.equal(h.emissions.at(-1).value.cursor, true);
    h.page("playground:close"); assert.equal(h.focus.at(-1), false);
    h.emit("resourceStop", "kcdc-playground");
    assert.deepEqual(h.removed, [1]); assert.equal(h.destroyed(), 1);
});

test("cursor placement uses the game ray and accepts only the matching response", () => {
    const h = setup(); h.hints[0].callback(); h.page("playground:ready");
    h.page("playground:request", JSON.stringify({ action: "item", destination: "cursor", position: { x: 999 } }));
    assert.deepEqual(h.requests[0].position, { x: 1, y: 2, z: 3 });
    h.emit("kcdc-playground:response", JSON.stringify({ id: 99, ok: true, creator: true }));
    assert.equal(h.creatorOpen(), false);
    h.page("playground:request", { action: "creator" }); assert.equal(h.requests.length, 1);
    h.advance(11000);
    assert.equal(h.emissions.at(-1).value.ok, false);
    h.page("playground:request", { action: "creator" }); assert.equal(h.requests.length, 2);
});

test("failed page loading releases focus, and unrelated creators are left alone", () => {
    const h = setup(); h.hints[0].callback();
    h.emit("browserLoadingFailed", { viewId: 1, isMainFrame: true, description: "missing file" });
    assert.equal(h.destroyed(), 1); assert.equal(h.focus.at(-1), false);
    h.emit("characterCreatorClosed", { gender: "female" }); assert.equal(h.requests.length, 0);
    h.hints[0].callback(); h.page("playground:ready");
    h.page("playground:request", { action: "creator" });
    h.emit("kcdc-playground:response", JSON.stringify({ id: 1, ok: true, creator: true }));
    assert.equal(h.creatorOpen(), true); assert.equal(h.focus.at(-1), false);
    h.emit("characterCreatorClosed", { gender: "female" });
    assert.equal(h.requests.at(-1).action, "appearance");
    h.emit("characterCreatorClosed", null); assert.equal(h.requests.length, 2);
});
