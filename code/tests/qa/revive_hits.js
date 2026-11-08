"use strict";

// Loaded only into the isolated QA runtime by start_revive_hits.ps1.
module.exports = function installReviveHitsQA(commands) {
    const bandit = "d5b503bb-b0f4-4f40-9ef1-52d7f9997c7d";
    const sword = "c164f346-0463-4116-b790-094b11274e5e";
    let fighter = null;
    let life = 1;

    const ours = npc => npc !== null && typeof npc === "object" && !("nickname" in npc) && npc.id === fighter;
    const name = actor => actor === null ? "nobody" : "nickname" in actor ? actor.nickname : `npc ${actor.id}`;

    function report(line) {
        console.log(`[reviveqa] life=${life} ${line}`);
        for (const player of Player.all()) Chat.sendToPlayer(player, `[reviveqa] life ${life}: ${line}`);
    }

    function current() {
        const npc = fighter === null ? null : Npc.getById(fighter);
        if (!npc) throw new Error("Spawn the QA fighter first: /reviveqa spawn");
        return npc;
    }

    function attack(npc, player) {
        if (!npc.attack(player, { weapon: "melee" })) throw new Error("The attack order was refused; stand within range on open ground.");
    }

    commands.add({
        name: "reviveqa",
        summary: "A revived NPC can be hurt and its blows raise events.",
        usage: ["/reviveqa spawn|kill|revive|attack|hold|status|reset"],
        run({ player, args, reply, usage }) {
            const action = args[0] || "status";
            if (action === "spawn") {
                Npc.getById(fighter ?? -1)?.remove();
                const facing = player.rotation.rotateVector(new Vector3(0, 1, 0));
                const length = Math.hypot(facing.x, facing.y);
                if (!player.ready || length < 0.001) throw new Error("Wait until your character is ready.");
                const position = new Vector3(player.position.x + facing.x / length * 5, player.position.y + facing.y / length * 5, player.position.z);
                const npc = require("./npc-spawn.js").spawnNpc({ soul: "bandit", outfit: bandit, position, name: "Revive QA", nametag: true, virtualWorld: player.virtualWorld });
                if (!npc.wear([...npc.wearing, sword])) { npc.remove(); throw new Error("The sword was refused."); }
                fighter = npc.id;
                life = 1;
                attack(npc, player);
                reply.line(`Spawned ${npc.id} with a sword, attacking you. Trade blows, then /reviveqa kill and /reviveqa revive.`);
                return;
            }
            if (action === "reset") { Npc.getById(fighter ?? -1)?.remove(); fighter = null; reply.line("Removed the QA fighter."); return; }
            const npc = current();
            if (action === "kill") { npc.kill(player); reply.line("Killed."); return; }
            if (action === "revive") {
                // The reported repro: revive, then order the attack again straight away.
                npc.revive();
                attack(npc, player);
                reply.line("Revived and ordered to attack you again. Its blows and yours must still report below.");
                return;
            }
            if (action === "attack") { attack(npc, player); reply.line("Attacking you."); return; }
            if (action === "hold") { npc.hold(); reply.line("Holding."); return; }
            if (action === "status") {
                report(`status npc=${npc.id} alive=${npc.alive} health=${npc.health.toFixed(1)}/${npc.maxHealth} intent=${npc.intent} status=${npc.status} simulator=${name(npc.simulator)}`);
                return;
            }
            usage();
        }
    });

    // Every line below is one of the two symptoms: its blows (playerHit,
    // playerDamage) and blows on it (npcDamage, npcDeath).
    Events.on("playerHit", (victim, attacker, hit) => { if (ours(attacker)) report(`playerHit: npc -> ${victim.nickname}, damage=${hit.damage.toFixed(1)}`); });
    Events.on("playerDamage", (victim, attacker, amount) => {
        if (!ours(attacker)) return;
        report(`playerDamage: npc -> ${victim.nickname}, amount=${amount.toFixed(1)}`);
        // Healed after the blow rather than softened in playerHit, so the
        // tester can go on fighting whether or not hit edits reach the ruling.
        victim.heal();
    });
    Events.on("npcDamage", (npc, attacker, amount) => { if (ours(npc)) report(`npcDamage: ${name(attacker)} -> npc, amount=${amount.toFixed(1)}, health=${npc.health.toFixed(1)}`); });
    Events.on("npcDeath", (npc, attacker) => { if (ours(npc)) report(`npcDeath by ${name(attacker)}`); });
    Events.on("npcRevive", npc => {
        if (!ours(npc)) return;
        life += 1;
        report(`npcRevive: health=${npc.health.toFixed(1)}`);
    });
    Events.on("npcSimulatorChange", (npc, player) => { if (ours(npc)) report(`simulator is now ${name(player)}`); });
    Events.on("playerReady", player => Chat.sendToPlayer(player, "Revive QA ready: /reviveqa spawn, trade blows, /reviveqa kill, /reviveqa revive, trade blows again."));
    console.log("[reviveqa] Installed. Start with /reviveqa spawn.");
};
