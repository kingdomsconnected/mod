import { MAX_ITEM_AMOUNT, MAX_ITEM_QUALITY, readAmount, readQuoted } from "../args.js";
import type { Command, CommandContext } from "../command.js";

/** Why the server turned a grant down, in the caller's terms. */
function refusal(item: string, quality: number, code: string): string {
    switch (code) {
        case "invalidItem":
        case "unknownItemClass":
            return `'${item}' is not an item class. Names are exact, and a GUID also works.`;
        case "invalidQuality":
            return `${item} is not made in quality ${quality}.`;
        case "questItem":
            return `${item} is a quest item; only its quest hands one out.`;
        case "inventoryCapacity":
            return "Your inventory has no room for that.";
        default:
            return `Could not grant ${item}: ${code}.`;
    }
}

export const giveCommand: Command = {
    name: "give",
    summary: "Puts items into your own inventory.",
    usage: [`/give <GUID|"exact item name"> [amount] [quality 1-${MAX_ITEM_QUALITY}]`],

    run({ player, args, reply, usage }: CommandContext): void {
        if (args.length === 0) {
            usage();
            return;
        }

        const { value: item, next } = readQuoted(args, 0);
        const amount = next < args.length ? readAmount(args[next]) : 1;
        if (amount === null) {
            reply.line(`'${args[next]}' is not an amount. They run from 1 to ${MAX_ITEM_AMOUNT}.`);
            return;
        }

        // The tier the game marks I to IV; left out, the lowest.
        const quality = next + 1 < args.length ? readAmount(args[next + 1], MAX_ITEM_QUALITY) : 1;
        if (quality === null) {
            reply.line(`'${args[next + 1]}' is not a quality. They run from 1 to ${MAX_ITEM_QUALITY}.`);
            return;
        }

        // The server holds the inventory; the caller's game shows the rows a tick later.
        const result = Inventory.add(player, { item, amount, metadata: { quality } });
        if (!result.ok) {
            reply.line(refusal(item, quality, result.code));
            return;
        }

        reply.line(`Granted ${amount} x ${item} (quality ${quality}).`);
    },
};
