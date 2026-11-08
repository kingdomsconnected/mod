// Run as a resource on a disposable server with no connected players.
import assert from "node:assert/strict";
assert.equal(Player.all().length, 0, "Use a disposable server with no players");
// A temporary non-player replica gives the wrapper a valid ID without
// connecting a client. Destroy it to exercise a stale player handle.
const stock = Stash.createVirtual(0xfffffff0);
const player = new Player(stock.id);
stock.destroy();
assert.equal(Object.isFrozen(PlayerStat), true);
assert.equal(Object.isFrozen(DerivedPlayerStat), true);
assert.equal(Object.keys(PlayerStat).length, 4);
assert.equal(Object.keys(DerivedPlayerStat).length, 25);
assert.throws(() => { PlayerStat.Health = "dirtiness"; });
assert.throws(() => { delete DerivedPlayerStat.Dirtiness; });
for (const stat of Object.values(PlayerStat)) {
    assert.equal(player.getStat(stat), 0);
    for (const value of [0, 20, 1000000]) assert.equal(player.setStat(stat, value), false);
    for (const value of [-1, NaN, Infinity, 1000000.001, Number.MIN_VALUE, "50", null]) assert.throws(() => player.setStat(stat, value));
    assert.throws(() => player.getDerivedStat(stat));
}
for (const stat of Object.values(DerivedPlayerStat)) {
    assert.equal(player.getDerivedStat(stat), 0);
    assert.throws(() => player.getStat(stat));
    assert.throws(() => player.setStat(stat, 50), /read-only/);
}
for (const method of ["getStat", "getDerivedStat"]) {
    for (const value of [null, undefined, 0, {}, [], "invalid", "Health"]) assert.throws(() => player[method](value));
    assert.throws(() => player[method]());
    assert.throws(() => player[method](PlayerStat.Health, 50));
    assert.throws(() => player[method].call({}, PlayerStat.Health));
}
assert.throws(() => player.setStat());
assert.throws(() => player.setStat(PlayerStat.Health));
assert.throws(() => player.setStat(PlayerStat.Health, 50, 60));
assert.throws(() => player.setStat(PlayerStat.Health, 1e-6));
assert.throws(() => player.setStat.call({}, PlayerStat.Health, 50));
for (const method of ["setHealth", "setHunger"]) {
    assert.equal(typeof player[method], "function");
    for (const value of [-1, 1000000.0001, 1000001, NaN, Infinity, -Infinity, Number.MIN_VALUE, 1e-100, "50", null, undefined, true, {}, []]) {
        assert.throws(() => player[method](value), /Player resource setter/);
    }
    if (method === "setHealth") {
        for (const value of [1e-6, 1e-5, 1.000000001e-5]) {
            assert.throws(() => player[method](value), /positive health must exceed/);
        }
    }
    assert.throws(() => player[method](), /Player resource setter/);
    assert.throws(() => player[method](50, 60), /Player resource setter/);
    assert.throws(() => player[method].call({}, 50));
    for (const value of [0, -0, 0.000010001, 0.001, 50, 1000000]) {
        assert.equal(player[method](value), false, "A disconnected handle must send nothing");
    }
}
console.log("PLAYER_VITALS_API_SMOKE_PASSED");
