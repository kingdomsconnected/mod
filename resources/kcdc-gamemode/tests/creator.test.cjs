const { test } = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

test("sample creator only accepts and closes stages it opened", () => {
    const events = new Map(), accepted = [];
    let closed = 0;
    const context = {
        exports: {},
        Events: { on: (name, fn) => events.set(name, fn), emitServer: (name, value) => accepted.push({ name, value }) },
        CharacterCreator: { open: () => true, close: () => closed++ },
        Hud: { showInfoText() {} },
    };
    vm.runInNewContext(fs.readFileSync(path.join(__dirname, "../dist/client/creator.js"), "utf8"), context);
    context.exports.installCreator();
    events.get("characterCreatorClosed")({ gender: "female" }, "accepted");
    events.get("kcdc-gamemode:creator.close")();
    assert.equal(accepted.length, 0); assert.equal(closed, 0);
    events.get("kcdc-gamemode:creator.open")({ genders: "both" });
    events.get("characterCreatorClosed")({ gender: "male" }, "accepted");
    assert.equal(accepted.length, 1);
    events.get("characterCreatorClosed")({ gender: "female" }, "accepted");
    events.get("kcdc-gamemode:creator.close")();
    assert.equal(accepted.length, 1); assert.equal(closed, 0);
});
