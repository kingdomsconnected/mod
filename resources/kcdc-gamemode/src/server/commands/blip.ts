import { join, readEntityId, readNumber } from "../args.js";
import type { Command, CommandContext } from "../command.js";
import { NO_POSE_YET, hasPose } from "../place.js";

/** How many lines `/blip list` and `/blip types` fill the caller's chat with. */
const LIST_LIMIT = 20;

/** Newest first, which is what somebody who has just placed things wants. */
function inWorld(virtualWorld: number): Blip[] {
    return Blip.all(virtualWorld).sort((left, right) => right.id - left.id);
}

/** The icon a name means, matched without regard to case, since nobody types `HuntingSpotBoar` exactly. */
function findType(name: string): string | undefined {
    const wanted = name.toLowerCase();
    return Blip.types().find((type) => type.toLowerCase() === wanted);
}

export const blipCommand: Command = {
    name: "blip",
    summary: "Puts a point on everybody's compass and map screen, or on yours alone.",
    usage: [
        "/blip [type] [label]",
        "/blip me [type] [label]",
        "/blip set <id> type|label|state|compass|map <value>",
        "/blip move <id>",
        "/blip types [prefix]",
        "/blip list",
        "/blip remove <id>",
        "/blip clear",
    ],

    run({ player, args, reply }: CommandContext): void {
        const subcommand = args[0];
        const world = player.virtualWorld;

        if (subcommand === "types") {
            const prefix = (args[1] ?? "").toLowerCase();
            const types = Blip.types().filter((type) => type.toLowerCase().startsWith(prefix));
            if (types.length === 0) {
                reply.line(`No blip type starts with '${args[1] ?? ""}'.`);
                return;
            }
            const shown = types.slice(0, LIST_LIMIT);
            reply.line(`${shown.length} of ${types.length} type(s): ${shown.join(", ")}`);
            return;
        }

        if (subcommand === "list") {
            const blips = inWorld(world);
            if (blips.length === 0) {
                reply.line("No blip has been placed in this world yet.");
                return;
            }
            const shown = blips.slice(0, LIST_LIMIT);
            reply.line(`${shown.length} of ${blips.length} blip(s), newest first:`);
            reply.lines(shown.map((blip) => `  ${blip.id}: ${blip.type}${blip.label ? ` '${blip.label}'` : ""}`));
            return;
        }

        if (subcommand === "clear") {
            reply.line(`Removed ${Blip.removeAll(world)} blip(s).`);
            return;
        }

        if (subcommand === "set" || subcommand === "move") {
            const id = readEntityId(args[1]);
            const blip = id !== null ? Blip.getById(id) : null;
            if (!blip || blip.virtualWorld !== world) {
                reply.line(`No blip in this world has id ${args[1] ?? "?"}. /blip list prints the ids.`);
                return;
            }

            if (subcommand === "move") {
                if (!hasPose(player)) {
                    reply.line(NO_POSE_YET);
                    return;
                }
                blip.position = player.position;
                reply.line(`Moved blip ${blip.id} to you.`);
                return;
            }

            const field = args[2];
            const value = join(args, 3);
            switch (field) {
                case "type": {
                    const type = findType(value);
                    if (type === undefined) {
                        reply.line(`'${value}' is not a blip type. /blip types lists them.`);
                        return;
                    }
                    blip.type = type;
                    break;
                }
                case "label":
                    blip.label = value;
                    break;
                case "state": {
                    const state = readNumber(value);
                    if (state === null) {
                        reply.line(`'${value}' is not a number.`);
                        return;
                    }
                    blip.markState = state;
                    break;
                }
                case "compass":
                case "map": {
                    if (value !== "on" && value !== "off") {
                        reply.line(`Use on or off for ${field}.`);
                        return;
                    }
                    if (field === "compass") {
                        blip.compass = value === "on";
                    } else {
                        blip.map = value === "on";
                    }
                    break;
                }
                default:
                    reply.line("Usage: /blip set <id> type|label|state|compass|map <value>");
                    return;
            }
            reply.line(`Blip ${blip.id}: ${blip.type}${blip.label ? ` '${blip.label}'` : ""}, state ${blip.markState}, compass ${blip.compass ? "on" : "off"}, map ${blip.map ? "on" : "off"}.`);
            return;
        }

        if (subcommand === "remove") {
            const id = readEntityId(args[1]);
            if (id === null) {
                reply.line("Usage: /blip remove <id>. /blip list prints the ids.");
                return;
            }
            const blip = Blip.getById(id);
            if (!blip || blip.virtualWorld !== world) {
                reply.line(`No blip in this world has id ${id}.`);
                return;
            }
            blip.remove();
            reply.line(`Removed blip ${id}.`);
            return;
        }

        // Everything else places one: `me` first makes it the caller's own,
        // then an optional type, then the rest of the line is its label.
        const mine = subcommand === "me";
        let next = mine ? 1 : 0;

        let type = "GeneralPoi";
        const word = args[next];
        const named = word !== undefined ? findType(word) : undefined;
        if (named !== undefined) {
            type = named;
            next += 1;
        }
        const label = join(args, next);

        if (!hasPose(player)) {
            reply.line(NO_POSE_YET);
            return;
        }

        const blip = Blip.create({ position: player.position, type, label, virtualWorld: world, player: mine ? player : undefined });
        const audience = mine ? "your compass and map" : "every compass and map in this world";
        reply.line(`Placed blip ${blip.id} (${blip.type}${label ? ` '${label}'` : ""}) on ${audience}. Remove it with /blip remove ${blip.id}.`);
    },
};
