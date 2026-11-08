"use strict";

// Loaded only into the isolated QA runtime by start_npc_inventory.ps1.
module.exports = function installNpcInventoryQA(commands) {
    const bodies = new Set();
    let target = null;
    const sword = "ce1953df-9f10-4138-8fb1-393a11474553";
    const sage = "b9de1d84-a0c1-4b81-9f60-8d7fbb3cb9d4";
    const money = "5ef63059-322e-4e1b-abe8-926e100c770e";
    const hare = "b19979de-14fe-4fbb-ac06-893366032cf3";

    function checked(result) {
        if (!result.ok) throw new Error(`Inventory refused QA setup: ${result.code}`);
        return result;
    }

    function snapshot(npc) {
        const state = Inventory.get(npc);
        console.log(`[lootqa] ${JSON.stringify({ npc: npc.id, name: npc.name, alive: npc.alive, ...state })}`);
        return state;
    }

    function reset() {
        for (const id of bodies) Npc.getById(id)?.remove();
        bodies.clear();
        target = null;
    }

    function create(player, kind) {
        const facing = player.rotation.rotateVector(new Vector3(0, 1, 0));
        const length = Math.hypot(facing.x, facing.y);
        if (!player.ready || length < 0.001) throw new Error("Wait until your character is ready.");
        const position = new Vector3(player.position.x + facing.x / length * 2, player.position.y + facing.y / length * 2, player.position.z);
        const animal = kind === "animal";
        const npc = require("./npc-spawn.js").spawnNpc({ soul: animal ? hare : "bandit", position, name: `QA ${kind}`, nametag: true, lootable: kind !== "blocked", virtualWorld: player.virtualWorld,
            ...(kind === "human" ? { outfit: "d5b503bb-b0f4-4f40-9ef1-52d7f9997c7d" } : {}) });
        bodies.add(npc.id);
        target = npc.id;
        if (kind === "human") {
            const stock = Inventory.get(npc);
            checked(Inventory.set(npc, { items: [...stock.items,
                { item: sword, amount: 2, metadata: { condition: 0.5 } },
                { item: sage, amount: 7 }, { item: money, amount: 23 }], harvested: false }));
        } else if (kind !== "native" && kind !== "animal") {
            checked(Inventory.set(npc, { items: kind === "blocked" ? [{ item: sage, amount: 7 }] : [], harvested: false }));
        }
        npc.kill();
        snapshot(npc);
        return npc;
    }

    commands.add({
        name: "lootqa",
        summary: "Isolated NPC inventory acceptance scenarios.",
        usage: ["/lootqa human|animal|native|empty|blocked", "/lootqa status|reset|perks|visit", "/lootqa remove|revive|kill|move|lock|restore [delaySeconds]"],
        run({ player, args, reply, usage }) {
            const action = args[0] || "human";
            if (["human", "animal", "native", "empty", "blocked"].includes(action)) {
                const npc = create(player, action);
                reply.line(`Created ${npc.name} (${npc.id}) in front of you. Loot normally; /lootqa status reads server stock.`);
                return;
            }
            if (action === "reset") { reset(); reply.line("Removed QA bodies. Your collected items remain."); return; }
            if (action === "perks") {
                player.setLevel("survival", 30);
                player.addPerk("Harvester");
                reply.line("Requested Survival 30 and Harvester. Wait for the perk/level update, then create a fresh animal.");
                return;
            }
            const npc = target === null ? null : Npc.getById(target);
            if (!npc) { reply.line("Create a QA body first."); return; }
            if (action === "visit") {
                player.virtualWorld = npc.virtualWorld;
                player.teleport(new Vector3(npc.position.x + 2, npc.position.y, npc.position.z));
                reply.line(`Moved next to ${npc.name} (${npc.id}). Both clients can inspect this same body.`);
                return;
            }
            if (action === "status") {
                const state = snapshot(npc);
                reply.line(`${npc.name}: ready=${state.ready}, harvested=${state.harvested}, ${state.items.length} rows, ${state.items.reduce((n, item) => n + item.amount, 0)} units.`);
                for (const item of state.items) reply.line(`${item.amount} x ${item.name}, condition=${item.condition}, equipped=${item.equipped}`);
                return;
            }
            if (!["remove", "revive", "kill", "move", "lock", "restore"].includes(action)) { usage(); return; }
            const delay = Number(args[1] || 0);
            if (!Number.isFinite(delay) || delay < 0 || delay > 60) { reply.line("Delay must be 0-60 seconds."); return; }
            const id = npc.id;
            const saved = Inventory.get(npc);
            const run = () => {
                const body = Npc.getById(id);
                if (!body) return;
                if (action === "remove") { body.remove(); bodies.delete(id); }
                else if (action === "revive") body.revive();
                else if (action === "kill") body.kill();
                else if (action === "lock") body.lootable = false;
                else if (action === "restore") checked(Inventory.set(body, saved));
                else body.teleport(new Vector3(body.position.x + 15, body.position.y, body.position.z));
                console.log(`[lootqa] ${action} applied to ${id}`);
            };
            if (delay) setTimeout(run, delay * 1000); else run();
            reply.line(`${action} ${delay ? `scheduled in ${delay}s; open the loot screen now` : "applied"}.`);
        }
    });

    Events.on("npcInventoryReady", npc => { if (bodies.has(npc.id)) snapshot(npc); });
    Events.on("npcInventoryChanged", (npc, change) => {
        if (bodies.has(npc.id)) console.log(`[lootqa] ${JSON.stringify({ npc: npc.id, change })}`);
    });
    Events.on("npcHarvested", (npc, player) => { if (bodies.has(npc.id)) { console.log(`[lootqa] harvested ${npc.id} by ${player.nickname}`); snapshot(npc); } });
    Events.on("playerInventoryChanged", (player, change) => console.log(`[lootqa] ${JSON.stringify({ player: player.nickname, change })}`));
    Events.on("playerReady", player => Chat.sendToPlayer(player, "NPC loot QA ready. /lootqa human, /lootqa animal, /lootqa status. /lootqa reset clears test bodies."));
    console.log("[lootqa] Installed. Start with /lootqa human.");
};
