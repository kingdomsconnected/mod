import { spawnNpc } from "../npc-spawn.js";
import { join } from "../args.js";
import type { Command, CommandContext } from "../command.js";
import { NO_POSE_YET, hasPose, inFrontOf, npcInReach } from "../place.js";

/** How far in front of the caller an NPC is put. Far enough not to land on them. */
const SPAWN_DISTANCE = 3;

/** Where a `/npc patrol` leg reaches, in metres either side of the caller. */
const PATROL_LEG = 12;

function describe(npc: Npc): string {
    const shown = npc.name === "" ? "<its soul's own>" : npc.name;
    const simulator = npc.simulator ? `${npc.simulator.nickname} (${npc.simulator.id})` : "nobody -- dormant";
    return `NPC ${npc.id}: '${shown}', ${npc.alive ? "alive" : "dead"} ${npc.health.toFixed(0)}/${npc.maxHealth.toFixed(0)}, intent ${npc.intent} (${npc.status}), locomotion ${npc.locomotion}, run by ${simulator}.`;
}

export const npcCommand: Command = {
    name: "npc",
    summary: "Spawns server-owned NPCs and orders them about.",
    usage: [
        "/npc [role|soulGuid]",
        "/npc roles",
        "/npc list",
        "/npc info",
        "/npc name [text]",
        "/npc follow",
        "/npc patrol",
        "/npc come",
        "/npc hold",
        "/npc say <text>",
        "/npc native | /npc kinematic",
        "/npc kill | /npc revive",
        "/npc pin | /npc unpin",
        "/npc remove | /npc clear",
    ],

    run({ player, args, reply }: CommandContext): void {
        const subcommand = args[0];

        if (subcommand === "roles") {
            reply.line(`Roles: ${Npc.roles().join(", ")}. Anything else is taken as a soul GUID.`);
            return;
        }

        if (subcommand === "list") {
            const all = Npc.all();
            if (all.length === 0) {
                reply.line("No NPCs.");
                return;
            }
            reply.line(`${all.length} NPC(s):`);
            for (const npc of all) {
                reply.line(`  ${describe(npc)}`);
            }
            return;
        }

        if (subcommand === "clear") {
            reply.line(`Removed ${Npc.removeAll()} NPC(s).`);
            return;
        }

        // Everything below acts on the NPC the caller is standing at.
        const acting =
            subcommand === "info" ||
            subcommand === "name" ||
            subcommand === "follow" ||
            subcommand === "patrol" ||
            subcommand === "come" ||
            subcommand === "hold" ||
            subcommand === "say" ||
            subcommand === "native" ||
            subcommand === "kinematic" ||
            subcommand === "kill" ||
            subcommand === "revive" ||
            subcommand === "pin" ||
            subcommand === "unpin" ||
            subcommand === "remove";

        if (acting) {
            const npc = npcInReach(player);
            if (!npc) {
                reply.line("No NPC nearby. Spawn one with /npc, or stand next to one.");
                return;
            }

            switch (subcommand) {
                case "info":
                    reply.line(describe(npc));
                    return;

                case "name": {
                    const name = join(args, 1);
                    npc.name = name;
                    reply.line(name === "" ? `NPC ${npc.id} handed back to its soul's own name.` : `NPC ${npc.id} is now '${name}'.`);
                    return;
                }

                case "follow":
                    npc.follow(player, { speed: "jog", radius: 3 });
                    reply.line(`NPC ${npc.id} is following you.`);
                    return;

                case "patrol": {
                    // Two waypoints either side of the caller, so one command is
                    // enough to watch a route being walked.
                    const there = inFrontOf(player, PATROL_LEG);
                    const back = inFrontOf(player, -PATROL_LEG);
                    const ok = npc.patrol([there.position, back.position], { loop: true, speed: "walk", waitSeconds: 3 });
                    reply.line(ok ? `NPC ${npc.id} is patrolling a ${PATROL_LEG * 2}m line.` : `NPC ${npc.id} refused the route.`);
                    return;
                }

                case "come":
                    npc.moveTo(player.position, { speed: "jog", radius: 2 });
                    reply.line(`NPC ${npc.id} is on its way.`);
                    return;

                case "hold":
                    npc.hold();
                    reply.line(`NPC ${npc.id} is holding.`);
                    return;

                case "say": {
                    const text = join(args, 1);
                    if (text === "") {
                        reply.line("Say what? /npc say <text>");
                        return;
                    }
                    reply.line(npc.say(text) ? `NPC ${npc.id} said it.` : `NPC ${npc.id} could not say that.`);
                    return;
                }

                case "native":
                case "kinematic":
                    npc.locomotion = subcommand === "native" ? "native" : "kinematic";
                    reply.line(`NPC ${npc.id} now moves ${npc.locomotion}ally.`);
                    return;

                case "kill":
                    npc.kill(player);
                    reply.line(`NPC ${npc.id} is dead.`);
                    return;

                case "revive":
                    npc.revive();
                    reply.line(`NPC ${npc.id} is back.`);
                    return;

                case "pin":
                    npc.pin(player);
                    reply.line(`NPC ${npc.id} is pinned to your client, whatever the distances say.`);
                    return;

                case "unpin":
                    npc.pin(null);
                    reply.line(`NPC ${npc.id} is back in the election.`);
                    return;

                case "remove":
                    reply.line(`Removed NPC ${npc.id}.`);
                    npc.remove();
                    return;
            }
        }

        // No subcommand: spawn one. The first argument is a role or a soul GUID.
        if (!hasPose(player)) {
            reply.line(NO_POSE_YET);
            return;
        }

        const { position, rotation } = inFrontOf(player, SPAWN_DISTANCE);
        const npc = spawnNpc({
            soul: subcommand ?? "guard",
            position,
            rotation,
            name: "",
            interactable: true,
            nametag: true,
        });
        reply.line(`Spawned NPC ${npc.id} (soul ${npc.soul}). It is ${npc.simulator ? "being run by " + npc.simulator.nickname : "dormant until somebody is near it"}.`);
    },
};
