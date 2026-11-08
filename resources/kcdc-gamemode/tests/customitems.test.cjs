const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const { test } = require("node:test");

function setup() {
    const registrations = [], grants = [], updates = [], stock = [], opens = [], emitted = [];
    const handlers = new Map();
    const messages = [];
    let stockOk = true;
    let vendorCount = 0;
    const context = {
        exports: {},
        require: () => ({ BOOK_EVENT: "book-demo" }),
        console: { log() {} },
        Events: { on: (name, callback) => handlers.set(name, callback) },
        Items: { register: (id, definition) => { registrations.push({ id, definition }); return id; } },
        Inventory: {
            add: (player, request) => { grants.push({ player, request }); return { ok: true }; },
            get: () => ({ revision: 7, items: [{ id: "letter-row", item: "kcdc-demo:letter" }] }),
            updateCustom: (player, request) => { updates.push({ player, request }); return { ok: true }; },
        },
        Vendor: {
            create: () => ++vendorCount,
            setStock: (id, rows) => { stock.push({ id, rows }); return stockOk; },
            setBuyPrices() {},
            open: (vendor, id) => { opens.push({ vendor, id }); return 1; },
        },
    };
    vm.runInNewContext(fs.readFileSync(path.join(__dirname, "../dist/server/commands/customitems.js"), "utf8"), context);
    context.exports.installCustomItemDemo();
    const player = { id: 5, ready: true, emit: (name, payload) => emitted.push({ name, payload: JSON.parse(payload) }) };
    const run = (...args) => context.exports.customItemsCommand.run({ player, args, reply: { line: (text) => messages.push(text) }, usage: () => messages.push("usage") });
    return { run, player, registrations, grants, updates, stock, opens, emitted, handlers, messages, vendorCount: () => vendorCount, rejectStock: () => { stockOk = false; } };
}

test("custom demo rejects bad input and unspawned players before registration", () => {
    const s = setup();
    s.run("unknown");
    s.run("grant", "extra");
    s.player.ready = false;
    s.run();
    assert.equal(s.registrations.length, 0);
    assert.equal(s.grants.length, 0);
});

test("late custom registration grants a stack and two independent private letters", () => {
    const s = setup();
    assert.equal(s.registrations.length, 0);
    s.run();
    assert.equal(s.registrations.length, 2);
    assert.equal(s.grants.length, 3);
    assert.equal(s.grants[0].request.amount, 10);
    assert.equal(s.grants[1].request.metadata.custom.name, "Letter from Anna");
    assert.equal(s.grants[2].request.metadata.custom.data.sender, "Hans");
    assert.equal(s.registrations[1].definition.stackable, false);
});

test("custom Read uses server data, filters other types and never consumes letters", () => {
    const s = setup();
    s.run();
    const read = s.handlers.get("playerCustomItemUse");
    for (const [item, sender] of [["vanilla", "Anna"], ["kcdc-demo:letter", "unknown"]])
        read(s.player, { item, metadata: { custom: { data: { sender } } } }, 7);
    assert.equal(s.emitted.length, 0);
    read(s.player, { id: "a", item: "kcdc-demo:letter", metadata: { custom: { data: { sender: "Anna" } } } }, 7);
    read(s.player, { id: "h", item: "kcdc-demo:letter", metadata: { custom: { data: { sender: "Hans" } } } }, 7);
    assert.equal(s.emitted.length, 2);
    assert.match(s.emitted[0].payload.pages[0], /mill/);
    assert.match(s.emitted[1].payload.pages[0], /timber/);
    assert.equal(s.emitted[0].payload.data, undefined);
    assert.equal(s.grants.length, 3);
});

test("rename and default reset use current revision and leave private data intact", () => {
    const s = setup();
    s.run("rename");
    s.run("defaults");
    assert.equal(s.updates[0].request.id, "letter-row");
    assert.equal(s.updates[0].request.revision, 7);
    assert.equal(s.updates[0].request.name, "Opened letter");
    assert.equal(s.updates[1].request.name, null);
    assert.equal(s.updates[1].request.description, null);
    assert.equal(Object.hasOwn(s.updates[1].request, "data"), false);
});

test("repeated stock sessions reuse their vendor and a refused stock does not open", () => {
    const s = setup();
    s.run("vendor");
    s.run("vendor");
    assert.equal(s.vendorCount(), 1);
    assert.equal(s.stock.length, 2);
    assert.equal(s.opens.length, 2);
    assert.equal(s.stock[0].rows[1].metadata.custom.data.sender, "Hans");
    assert.ok(s.registrations.find(({ id }) => id === s.stock[0].rows[1].item).definition.price > 0, "native vendors hide types with a zero base price before applying the stock price");
    s.rejectStock();
    s.run("vendor");
    assert.equal(s.opens.length, 2);
    assert.match(s.messages.at(-1), /could not be prepared/);
});
