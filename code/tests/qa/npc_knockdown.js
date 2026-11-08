"use strict";

// Loaded only into the isolated QA runtime. A sword bandit attacks every
// player shortly after they are ready, and every blow, buff and stat change
// on that player is printed with [knockqa], so a knockdown that never ends
// shows what the server saw of it.
module.exports = function installNpcKnockdownQA(commands) {
    const bandit = "d5b503bb-b0f4-4f40-9ef1-52d7f9997c7d";
    const sword = "c164f346-0463-4116-b790-094b11274e5e";
    const fighters = new Map();
    // The health the tester is held at. Low enough that the bandit's own
    // lethal dry run says a finisher would kill, which is what puts a
    // player into the finisher that never ends.
    const health = Number(process.env.KNOCKQA_HEALTH || 100);

    function report(player, line) {
        console.log(`[knockqa] ${player.nickname}: ${line}`);
    }

    function spawn(player) {
        Npc.getById(fighters.get(player.id) ?? -1)?.remove();
        // The tester holds the same sword, as in the fights the bug was reported from.
        if (player.alive) player.setStat(PlayerStat.Health, health);
        report(player, `equip sword=${JSON.stringify(Inventory.set(player, { items: [{ item: sword, amount: 1, equipped: 1 }] }))}`);
        const facing = player.rotation.rotateVector(new Vector3(0, 1, 0));
        const length = Math.hypot(facing.x, facing.y) || 1;
        const position = new Vector3(player.position.x + facing.x / length * 4, player.position.y + facing.y / length * 4, player.position.z);
        const npc = require("./npc-spawn.js").spawnNpc({ soul: "bandit", outfit: bandit, position, name: "Knockdown QA", nametag: true, virtualWorld: player.virtualWorld });
        if (!npc.wear([...npc.wearing, sword])) {
            npc.remove();
            throw new Error("The sword was refused.");
        }
        fighters.set(player.id, npc.id);
        report(player, `spawned npc ${npc.id}`);
        // Ordered once the sword is on the body: combat draws the weapon set
        // the NPC has when it closes in.
        setTimeout(() => { if (player.ready && npc.alive) report(player, `attack=${npc.attack(player, { weapon: "melee" })}`); }, 3000);
        return npc;
    }

    commands.add({
        name: "knockqa",
        summary: "A sword bandit attacks you until it knocks you down.",
        usage: ["/knockqa spawn|hold|status"],
        run({ player, args, reply }) {
            const action = args[0] || "status";
            const npc = Npc.getById(fighters.get(player.id) ?? -1);
            if (action === "spawn") { spawn(player); reply.line("Spawned."); return; }
            if (action === "hold") { npc?.hold(); reply.line("Holding."); return; }
            reply.line(`npc=${npc?.id} intent=${npc?.intent} health=${player.health}`);
        }
    });

    Events.on("playerReady", player => {
        setTimeout(() => { if (player.ready) spawn(player); }, 8000);
    });
    Events.on("playerHit", (victim, attacker, hit) => report(victim, `playerHit damage=${hit.damage.toFixed(1)} from ${attacker && "nickname" in attacker ? attacker.nickname : `npc ${attacker?.id}`}`));
    // Health is put back after every blow so the fight lasts until a combo
    // knocks the player down. setStat leaves the knockdown's buffs alone,
    // where heal() would cure them.
    Events.on("playerDamage", (victim, attacker, amount) => {
        report(victim, `playerDamage amount=${amount.toFixed(1)} health=${victim.health.toFixed(1)}`);
        if (victim.alive && amount > 0) setTimeout(() => { if (victim.ready && victim.alive) victim.setStat(PlayerStat.Health, health); }, 200);
    });
    Events.on("playerBuffAdded", (player, buff) => report(player, `buffAdded ${buff}`));
    Events.on("playerBuffRemoved", (player, buff) => report(player, `buffRemoved ${buff}`));
    Events.on("playerDied", player => report(player, "died"));
    setInterval(() => {
        for (const player of Player.all()) {
            if (!player.ready) continue;
            const npc = Npc.getById(fighters.get(player.id) ?? -1);
            report(player, `status health=${player.health.toFixed(1)} alive=${player.alive} unconscious=${player.hasBuff("unconscious")} consciousness=${player.getDerivedStat(DerivedPlayerStat.Consciousness)} npcIntent=${npc?.intent}`);
        }
    }, 2000);
    console.log("[knockqa] Installed.");
};
