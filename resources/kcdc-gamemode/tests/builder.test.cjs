const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const { test } = require("node:test");

function setup() {
    const context = { exports: {} };
    vm.runInNewContext(fs.readFileSync(path.join(__dirname, "../dist/server/commands/builder.js"), "utf8"), context);
    const messages = [];
    const grants = [];
    let usages = 0;
    const run = (player, ...args) => context.exports.builderCommand.run({
        player,
        args,
        reply: { line: (text) => messages.push(text) },
        usage: () => ++usages,
    });
    const player = (id, succeeds = true) => ({
        setWorldBuilderEnabled(enabled) {
            grants.push({ id, enabled });
            return succeeds;
        },
    });
    return { run, player, grants, messages, usages: () => usages };
}

test("builder grants and revokes only the caller", () => {
    const s = setup();
    const first = s.player(1);
    const second = s.player(2);
    s.run(first);
    s.run(second, "on");
    s.run(first, "off");
    assert.deepEqual(s.grants, [{ id: 1, enabled: true }, { id: 2, enabled: true }, { id: 1, enabled: false }]);
    assert.match(s.messages[0], /F7/);
    assert.match(s.messages[2], /disabled/);
});

test("builder rejects invalid arguments without changing permission", () => {
    const s = setup();
    s.run(s.player(1), "maybe");
    s.run(s.player(1), "on", "other-player");
    assert.equal(s.usages(), 2);
    assert.deepEqual(s.grants, []);
});

test("builder reports a failed grant without claiming access", () => {
    const s = setup();
    s.run(s.player(1, false));
    assert.deepEqual(s.messages, ["World Builder access could not be changed."]);
});
