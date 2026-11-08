import { readLevelGuid } from "../args.js";
import type { Command, CommandContext, Reply } from "../command.js";

/** How many lines `/door list` fills the caller's chat with. */
const LIST_LIMIT = 10;

/** What an operator asked of a door: nothing, a lock state, or an open state. */
type DoorOrder = { kind: "read" } | { kind: "lock"; locked: boolean } | { kind: "open"; open: boolean };

/** What the level says about a door, for the lines that describe one. */
function describe(door: Door): string {
    const key = door.keyItem === "" ? "no key of its own" : `key ${door.keyItem}`;
    return `${door.startsLocked ? "starts locked" : "starts unlocked"}, ${key}, ${door.lockpickable ? "lockpickable" : "not lockpickable"}`;
}

/** Carries out one order on one door and says what became of it. */
function apply(door: Door, order: DoorOrder, reply: Reply): void {
    if (order.kind === "lock") {
        door.locked = order.locked;
        reply.line(`Door ${door.guid} is now ${door.locked ? "locked" : "unlocked"}.`);
    } else if (order.kind === "open") {
        const changed = door.setOpen(order.open);
        reply.line(changed ? `Door ${door.guid} is now ${order.open ? "open" : "closed"}.` : `Door ${door.guid} is already ${order.open ? "open" : "closed"}.`);
    } else {
        reply.line(`Door ${door.guid}: open=${door.open}, locked=${door.locked}${door.lockedByServer ? " (by the server)" : ""}; ${describe(door)}.`);
    }
}

/** Acts on the door the caller is standing at, which only their own client can identify. */
async function atCaller(player: Player, reply: Reply, order: DoorOrder): Promise<void> {
    const answer = await Door.queryNearest(player);
    if (!answer.found || !answer.door) {
        reply.line(answer.reason);
        return;
    }

    if (order.kind === "read") {
        const shown = answer.name === "" ? "unnamed" : answer.name;
        reply.line(`Nearest door: ${shown} guid=${answer.guid} at ${(answer.distance ?? 0).toFixed(1)}m, open=${answer.open}, locked=${answer.locked}; ${describe(answer.door)}.`);
        return;
    }
    apply(answer.door, order, reply);
}

/** The order a subcommand names, or null for one this command does not know. */
function readOrder(action: string): DoorOrder | null {
    switch (action) {
        case "near":
            return { kind: "read" };
        case "lock":
        case "unlock":
            return { kind: "lock", locked: action === "lock" };
        case "open":
        case "close":
            return { kind: "open", open: action === "open" };
        default:
            return null;
    }
}

export const doorCommand: Command = {
    name: "door",
    summary: "Reads, locks, opens and closes the level's own doors.",
    usage: ["/door [near] [guid]", "/door lock|unlock|open|close [guid]", "/door list"],

    run({ player, args, reply, usage }: CommandContext): Promise<void> | void {
        const action = args[0] ?? "near";
        const world = player.virtualWorld;

        if (action === "list") {
            const doors = Door.all().filter((door) => door.virtualWorld === world && (door.open || door.locked !== door.startsLocked));
            if (doors.length === 0) {
                reply.line("Every door here is as the level built it.");
                return;
            }
            const shown = doors.slice(0, LIST_LIMIT);
            reply.line(`${shown.length} of ${doors.length} door(s) changed from how the level built them:`);
            reply.lines(shown.map((door) => `  ${door.guid} token=${door.stateToken} open=${door.open} locked=${door.locked}`));
            return;
        }

        const order = readOrder(action);
        if (order === null) {
            usage();
            return;
        }

        // An explicit GUID needs no round trip: the server knows every door
        // the level places, whether or not anyone has been near it.
        if (args.length >= 2) {
            const named = readLevelGuid(args[1]);
            if (named === null) {
                reply.line(`'${args[1]}' is not a door GUID. They are hexadecimal; /door near prints one.`);
                return;
            }
            const door = Door.find(named, world);
            if (!door) {
                reply.line("The level places no such door.");
                return;
            }
            apply(door, order, reply);
            return;
        }

        return atCaller(player, reply, order);
    },
};
