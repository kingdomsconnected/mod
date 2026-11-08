import { MAX_ITEM_AMOUNT, readAmount, readEntityId, readQuoted } from "../args.js";
import type { Command, CommandContext } from "../command.js";
import { NO_POSE_YET, hasPose, inFrontOf } from "../place.js";

/** How far in front of the caller a stack is laid. Within reach: it is meant to be picked back up. */
const DROP_DISTANCE = 1.5;

/** Laid slightly above the ground, so the physics has somewhere to settle it from. */
const DROP_LIFT = 0.3;

/** How many lines `/drop list` fills the caller's chat with. */
const LIST_LIMIT = 20;

/** Newest first, which is what somebody who has just dropped things wants. */
function inWorld(virtualWorld: number): GroundItem[] {
    return GroundItem.all()
        .filter((stack) => stack.virtualWorld === virtualWorld)
        .sort((left, right) => right.id - left.id);
}

export const dropCommand: Command = {
    name: "drop",
    summary: "Lays a pickable stack of an item on the ground.",
    usage: ['/drop <GUID|"exact item name"> [amount]', "/drop list", "/drop remove <id>", "/drop clear"],

    run({ player, args, reply, usage }: CommandContext): void {
        // Read before the first argument becomes an item: no item class is
        // called any of these.
        const subcommand = args[0];
        const world = player.virtualWorld;

        if (subcommand === "list") {
            const stacks = inWorld(world);
            if (stacks.length === 0) {
                reply.line("Nothing is lying on the ground in this world.");
                return;
            }
            const shown = stacks.slice(0, LIST_LIMIT);
            reply.line(`${shown.length} of ${stacks.length} stack(s), newest first:`);
            reply.lines(shown.map((stack) => `  ${stack.id}: ${stack.itemClass} x${stack.amount}${stack.resting ? "" : " (falling)"}`));
            return;
        }

        if (subcommand === "clear") {
            reply.line(`Removed ${GroundItem.destroyAll(world)} stack(s).`);
            return;
        }

        if (subcommand === "remove") {
            const id = readEntityId(args[1]);
            if (id === null) {
                reply.line("Usage: /drop remove <id>. /drop list prints the ids.");
                return;
            }
            const stack = GroundItem.getById(id);
            if (!stack || stack.virtualWorld !== world) {
                reply.line(`No stack in this world has id ${id}.`);
                return;
            }
            stack.destroy();
            reply.line(`Removed stack ${id}.`);
            return;
        }

        if (subcommand === undefined) {
            usage();
            return;
        }

        const { value: item, next } = readQuoted(args, 0);
        const amount = next < args.length ? readAmount(args[next]) : 1;
        if (amount === null) {
            reply.line(`'${args[next]}' is not an amount. They run from 1 to ${MAX_ITEM_AMOUNT}.`);
            return;
        }

        if (!hasPose(player)) {
            reply.line(NO_POSE_YET);
            return;
        }

        const { position, rotation } = inFrontOf(player, DROP_DISTANCE, DROP_LIFT);

        // Caught here: a misspelled item is the commonest way this goes wrong,
        // and the dispatcher's wording makes it read like the server broke.
        let stack: GroundItem;
        try {
            stack = GroundItem.spawn(item, position, rotation, amount, world);
        } catch {
            reply.line(`'${item}' is not an item class. Names are exact, and a GUID also works.`);
            return;
        }

        reply.line(`Dropped ${item} x${amount} (id ${stack.id}).`);
    },
};
