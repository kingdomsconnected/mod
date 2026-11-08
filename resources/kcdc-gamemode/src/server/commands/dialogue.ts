import type { Command, CommandContext } from "../command.js";

/**
 * A worked example of a conversation, and the shape every other one takes.
 *
 * A conversation is attached to nothing. `Dialogue.open` takes a player, not an
 * entity, so what this one is *about* -- here, a merchant who is not there --
 * lives entirely in the closure below. Bind it to an NPC, a notice board or a
 * timer and none of the code changes.
 *
 * The pages are a plain lookup rather than a graph. That is the whole trick:
 * `dialogueChoice` hands back the option's own `id`, so advancing a conversation
 * is choosing the next page by name. Nothing has to remember where the player
 * was, because the server already knows which page it last sent.
 */

/**
 * The sample's pages, keyed by the option id that leads to each.
 *
 * Typed as the contract's own `DialoguePage` rather than a local shape, so a
 * page that would be refused at runtime is refused by the compiler instead.
 */
const PAGES: Record<string, DialoguePage> = {
    start: {
        line: "Good morrow. You look like a man with coin and no idea what to spend it on.",
        onRight: false,
        options: [
            { id: "wares", text: "Show me what you have.", enabled: true },
            { id: "news", text: "Heard anything worth hearing?", enabled: true },
            { id: "haggle", text: "Your prices are robbery.", enabled: true },
            { id: "leave", text: "Nothing today. Farewell.", enabled: true },
        ],
    },
    wares: {
        line: "Blades, mostly. A few that have even been swung in anger.",
        onRight: false,
        options: [
            { id: "news", text: "Later. What of the news?", enabled: true },
            // Greyed rather than hidden, to show what a gated row looks like.
            // The server still re-checks it when an answer arrives: `enabled` is
            // what the row is drawn as, never what the choice is allowed by.
            { id: "locked", text: "The good stuff, out the back.", enabled: false },
            { id: "leave", text: "I have seen enough.", enabled: true },
        ],
    },
    news: {
        line: "Cumans on the road north. Two carts taken this week, and nobody minded to stop them.",
        onRight: false,
        options: [
            { id: "wares", text: "Then I had better be armed.", enabled: true },
            { id: "leave", text: "Thank you. I will be careful.", enabled: true },
        ],
    },
    haggle: {
        line: "Robbery? Sir, robbery is what waits for you on the north road. This is commerce.",
        onRight: false,
        options: [
            { id: "start", text: "Fair enough. Start again.", enabled: true },
            { id: "leave", text: "Good day to you.", enabled: true },
        ],
    },
};

/** Which session belongs to this sample, so its handlers ignore everybody else's. */
const sessions = new Map<number, number>();

function show(player: Player, pageId: string): void {
    const page = PAGES[pageId];
    if (!page) {
        return;
    }
    const existing = sessions.get(player.id);
    // `update` keeps the same session, which is what makes it one conversation
    // rather than a burst of unrelated ones. A new page bumps the generation, so
    // an answer to the page it replaces is dropped rather than misread.
    const session = existing !== undefined ? (Dialogue.update(existing, page) ? existing : 0) : Dialogue.open(player.id, page);
    if (session === 0) {
        sessions.delete(player.id);
        console.log(`dialogue: could not show '${pageId}' to ${player.nickname}`);
        return;
    }
    sessions.set(player.id, session);
    console.log(`dialogue: session ${session} showing '${pageId}' to ${player.nickname}`);
}

export function installDialogueSample(): void {
    Events.on("dialogueChoice", (session, player, optionId) => {
        console.log(`dialogueChoice: session ${session}, ${player.nickname} picked '${optionId}'`);
        if (sessions.get(player.id) !== session) {
            // Another resource's conversation. Leaving it alone is the whole of
            // sharing this API with the rest of the server.
            return;
        }
        if (optionId === "leave") {
            Dialogue.close(session);
            return;
        }
        show(player, optionId);
    });

    Events.on("dialogueClosed", (session, player, reason) => {
        const why = ["completed", "cancelled by the player", "replaced by another conversation", "interrupted"][reason] ?? `reason ${reason}`;
        console.log(`dialogueClosed: session ${session} with ${player.nickname} ended -- ${why}`);
        if (sessions.get(player.id) === session) {
            sessions.delete(player.id);
        }
    });
}

export const dialogueCommand: Command = {
    name: "dialogue",
    summary: "Opens the sample conversation in the game's own dialogue list.",
    usage: ["/dialogue", "/dialogue stop"],
    run(context: CommandContext): void {
        const { player, args, reply } = context;
        if (args[0] === "stop") {
            const session = sessions.get(player.id);
            if (session === undefined) {
                reply.line("You are not in a conversation.");
                return;
            }
            Dialogue.close(session);
            reply.line("Closed.");
            return;
        }
        if (args.length > 0) {
            context.usage();
            return;
        }
        show(player, "start");
        reply.line("W and S move, E chooses, Esc backs out. Every choice is logged on the server.");
    },
};
