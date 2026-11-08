import { join } from "../args.js";
import type { Command, CommandContext } from "../command.js";
import { formatPosition, groundForward } from "../place.js";
import { findPlayer } from "../players.js";

/** How far in front of the target the caller lands. Two bodies in one spot are shoved apart in no particular direction. */
const ARRIVAL_DISTANCE = 2;

export const teleportCommand: Command = {
    name: "tp",
    summary: "Puts you next to another player, or at a map position.",
    usage: ["/tp <player|slot>", "/tp <x> <y> [z]", "/tp list"],

    run({ player, args, reply, usage }: CommandContext): void {
        if (args[0] === "list") {
            const players = Player.all();
            const line = players.map((other) => `${other.nickname} (#${other.playerIndex}${other.id === player.id ? ", you" : ""})`).join(", ");
            reply.line(`${players.length} player(s): ${line}`);
            return;
        }

        if (args.length === 0) {
            usage();
            return;
        }

        // Two or three numbers are a map position; the client grounds the
        // body itself, so a missing height is only a starting guess.
        const coordinates = args.map((value) => Number(value));
        if ((args.length === 2 || args.length === 3) && coordinates.every((value) => Number.isFinite(value))) {
            const destination = new Vector3(coordinates[0] ?? 0, coordinates[1] ?? 0, coordinates[2] ?? player.position.z);
            reply.line(player.teleport(destination, formatPosition(destination)) ? `Teleporting you to ${formatPosition(destination)}.` : "Could not reach your client to move you.");
            return;
        }

        const found = findPlayer(join(args, 0));
        if ("error" in found) {
            reply.line(`${found.error} /tp list names everyone here.`);
            return;
        }

        const target = found.player;
        if (target.id === player.id) {
            reply.line("You are already there.");
            return;
        }
        if (!target.ready) {
            reply.line(`${target.nickname}'s position has not reached the server yet; try again in a moment.`);
            return;
        }

        const ahead = groundForward(target.rotation as Quaternion);
        const destination = new Vector3(target.position.x + ahead.x * ARRIVAL_DISTANCE, target.position.y + ahead.y * ARRIVAL_DISTANCE, target.position.z);

        if (!player.teleport(destination, target.nickname)) {
            reply.line("Could not reach your client to move you.");
            return;
        }

        reply.line(`Teleporting you to ${target.nickname} (${formatPosition(destination)}).`);
    },
};
