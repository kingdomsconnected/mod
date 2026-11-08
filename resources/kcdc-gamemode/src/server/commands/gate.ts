import { readLevelGuid } from "../args.js";
import type { Command, CommandContext, Reply } from "../command.js";
import { formatPosition } from "../place.js";

/** How far a bare `/gate` looks. Castle gates are visible from much further than a door. */
const REACH = 60;

/** How many lines `/gate list` fills the caller's chat with. */
const LIST_LIMIT = 20;

/** The gate a line names by GUID, or the nearest one; null after saying why there is none. */
function target(args: readonly string[], player: Player, reply: Reply): Gate | null {
    const world = player.virtualWorld;
    if (args.length >= 2) {
        const named = readLevelGuid(args[1]);
        if (named === null) {
            reply.line(`'${args[1]}' is not a gate GUID. They are hexadecimal; /gate list prints them.`);
            return null;
        }
        const gate = Gate.find(named, world);
        if (!gate) {
            reply.line(`The level places no gate ${named}. /gate list shows the ones it does.`);
        }
        return gate;
    }

    const gate = Gate.nearest(player.position, REACH, world);
    if (!gate) {
        reply.line(`No gate within ${REACH}m. /gate list shows the ones the level places.`);
    }
    return gate;
}

export const gateCommand: Command = {
    name: "gate",
    summary: "Opens and closes the level's castle gates: drawbridges, portcullises and fortress gates.",
    usage: ["/gate [toggle] [guid]", "/gate open|close [guid]", "/gate list"],

    run({ player, args, reply, usage }: CommandContext): void {
        const action = args[0] ?? "toggle";

        if (action === "list") {
            const gates = Gate.all().filter((gate) => gate.virtualWorld === player.virtualWorld);
            if (gates.length === 0) {
                reply.line("No gate has been built in this world yet. /gate at one builds it.");
                return;
            }
            const shown = gates.slice(0, LIST_LIMIT);
            reply.line(`${shown.length} of ${gates.length} gate(s):`);
            reply.lines(shown.map((gate) => `  ${gate.guid} ${gate.kind} ${gate.cycle} at ${formatPosition(gate.position)} (open ${gate.openDuration.toFixed(1)}s, close ${gate.closeDuration.toFixed(1)}s)`));
            return;
        }

        if (action !== "toggle" && action !== "open" && action !== "close") {
            usage();
            return;
        }

        const gate = target(args, player, reply);
        if (!gate) {
            return;
        }

        // Omitted rather than passed: a bare `/gate` heads away from whichever
        // end the gate is at, reversing one already moving.
        const result = action === "toggle" ? gate.toggle() : gate.toggle(action === "open");
        if (!result.accepted) {
            reply.line(`${gate.kind} ${gate.guid} ${result.reason}.`);
            return;
        }
        reply.line(`${gate.kind} ${gate.guid} is ${result.opening ? "opening" : "closing"} (${result.seconds.toFixed(1)}s).`);
    },
};
