import { MAX_ITEM_AMOUNT, readAmount, readQuoted } from "../args.js";
import type { Command, CommandContext } from "../command.js";

export const takeCommand: Command = {
    name: "take",
    summary: "Takes items back out of your own inventory.",
    usage: ['/take <GUID|"exact item name"> [amount]'],

    async run({ player, args, reply, usage }: CommandContext): Promise<void> {
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

        // The server holds no inventory, so only the caller's own client can
        // say whether they had these. That is why this waits and `/give` does not.
        const removal = await player.takeItem(item, amount);
        if (removal.reason !== "") {
            reply.line(`Could not take ${amount} x '${item}'. ${removal.reason}`);
            return;
        }

        if (removal.removed === 0) {
            reply.line(`You are not carrying any '${item}'.`);
            return;
        }

        if (!removal.ok) {
            reply.line(`Took ${removal.removed} x ${item}. That is all you were carrying.`);
            return;
        }

        reply.line(`Took ${removal.removed} x ${item}.`);
    },
};
