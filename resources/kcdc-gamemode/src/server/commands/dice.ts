import type { Command, CommandContext } from "../command.js";
import { diceLobby } from "../dice.js";
import { findPlayer } from "../players.js";

export const diceCommand: Command = {
    name: "dice",
    summary: "Invites a nearby opponent to choose a dice score, or leaves the table lobby.",
    usage: ["/dice <player> [target score]", "/dice accept", "/dice decline", "/dice cancel"],

    async run({ player, args, reply, usage }: CommandContext): Promise<void> {
        const word = args[0]?.toLowerCase();
        if (!word) {
            usage();
            return;
        }
        if (["accept", "decline", "cancel"].includes(word)) {
            if (args.length !== 1) {
                usage();
                return;
            }
            if (word === "accept") diceLobby.accept(player);
            else diceLobby.cancelFor(player);
            return;
        }
        if (args.length > 2) {
            usage();
            return;
        }
        const found = findPlayer(args[0]!);
        if ("error" in found) {
            reply.line(found.error);
            return;
        }
        await diceLobby.invite(player, found.player, args[1] === undefined ? 2000 : Number(args[1]));
    },
};

export function installDiceHandlers(): void {
    diceLobby.install();
    Events.on("diceMatchEnd", (match, first, second, winner, reason, firstScore, secondScore) => {
        console.log(`diceMatchEnd: match ${match} ${reason}, seat ${winner} won, ${firstScore} : ${secondScore}`);
        const players = [first, second];
        const winning = winner === 0 || winner === 1 ? players[winner] : null;
        // The second seat can be an NPC: it has no nickname and no chat to write to.
        const winnerName = winning instanceof Player ? winning.nickname : "The opponent";
        for (const player of players) {
            if (!(player instanceof Player)) continue;
            const line = winning ? `${winnerName} won the game of dice, ${firstScore} : ${secondScore}.` : `The game of dice ended without a winner (${reason}).`;
            Chat.sendToPlayer(player, line);
        }
    });
}
