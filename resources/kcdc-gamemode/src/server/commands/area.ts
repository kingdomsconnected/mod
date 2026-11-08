import type { Command, CommandContext, Reply } from "../command.js";

/** How many lines `/area near` fills the caller's chat with. */
const LIST_LIMIT = 10;
const DEFAULT_RADIUS = 30;

/** One line per area: its id, what it is, and what the level says about it. */
function describe(area: Area): string {
    const labels = area.labels.length === 0 ? "no labels" : area.labels.join(", ");
    const where = area.location === "" ? "" : ` in ${area.location}`;
    return `${area.id} ${area.type}${where}: ${labels}`;
}

function listDoors(area: Area, reply: Reply): void {
    const doors = Door.getForArea(area.id);
    if (doors.length === 0) {
        reply.line(`${area.id} links no doors.`);
        return;
    }
    reply.line(`${area.id} links ${doors.length} door(s):`);
    reply.lines(doors.map((door) => `  ${door.link} ${door.guid}${door.name === "" ? "" : ` ${door.name}`}`));
}

export const areaCommand: Command = {
    name: "area",
    summary: "Lists the level's areas and the script areas around you, and the doors an area links.",
    usage: ["/area [here]", "/area near [radius]", "/area doors [id]"],

    run({ player, args, reply, usage }: CommandContext): void {
        const action = args[0] ?? "here";

        if (action === "here") {
            const areas = Area.getAtPlayer(player);
            if (areas.length === 0) {
                reply.line("You are in no area.");
                return;
            }
            reply.line(`You are in ${areas.length} area(s):`);
            reply.lines(areas.map((area) => `  ${describe(area)}`));
            return;
        }

        if (action === "near") {
            const radius = args.length >= 2 ? Number(args[1]) : DEFAULT_RADIUS;
            if (!Number.isFinite(radius) || radius <= 0) {
                usage();
                return;
            }
            const areas = Area.nearby(player.position, radius, player.virtualWorld);
            if (areas.length === 0) {
                reply.line(`No area within ${radius} m.`);
                return;
            }
            const shown = areas.slice(0, LIST_LIMIT);
            reply.line(`${shown.length} of ${areas.length} area(s) within ${radius} m, nearest first:`);
            reply.lines(shown.map((area) => `  ${describe(area)}`));
            return;
        }

        if (action === "doors") {
            const id = args[1];
            if (id !== undefined) {
                const area = Area.getById(id);
                if (!area) {
                    reply.line(`No area '${id}'. /area prints the ids around you.`);
                    return;
                }
                listDoors(area, reply);
                return;
            }
            const linked = Area.getAtPlayer(player).filter((area) => area.doors.length > 0);
            if (linked.length === 0) {
                reply.line("No area you are in links a door.");
                return;
            }
            for (const area of linked) {
                listDoors(area, reply);
            }
            return;
        }

        usage();
    },
};
