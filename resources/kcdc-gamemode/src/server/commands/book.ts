import type { Command, CommandContext } from "../command.js";

/** Answered by the client half in `src/client/book.ts`. */
export const BOOK_EVENT = "kcdc-gamemode:book";

const STYLES = ["book", "plainBook", "letter"] as const;

export const bookCommand: Command = {
    name: "book",
    summary: "Opens a demo book in your hands: the chronicle of Kingdoms Connected, a herald's letter, a re-bound copy.",
    usage: [
        "/book [book|plainBook|letter]  -- the chronicle of Kingdoms Connected, in that binding",
        "/book letter                   -- a herald's letter, on folded parchment",
        "/book parchment                -- the chronicle re-bound in the resource's own parchment texture",
        "/book fade                     -- the chronicle starting illegible, its ink coming clear a tenth at a time",
        "/book legibility <0..1>        -- changes how legible the open book is",
        "/book close                    -- puts the book down",
        "/book status                   -- whether a book is open",
    ],

    run({ player, args, reply, usage }: CommandContext): void {
        const first = args[0] ?? "book";

        // A binding name opens the chronicle in it.
        const style = STYLES.find((name) => name.toLowerCase() === first.toLowerCase());
        if (style !== undefined && style !== "letter") {
            player.emit(BOOK_EVENT, JSON.stringify({ action: "chronicle", style }));
            reply.line("Opening the Chronicle of the Kingdoms Connected.");
            return;
        }

        switch (first.toLowerCase()) {
            case "letter":
                player.emit(BOOK_EVENT, JSON.stringify({ action: "letter" }));
                reply.line("A herald's letter.");
                return;

            case "parchment":
                player.emit(BOOK_EVENT, JSON.stringify({ action: "parchment" }));
                reply.line("The chronicle, bound in parchment.");
                return;

            case "fade":
                player.emit(BOOK_EVENT, JSON.stringify({ action: "fade" }));
                reply.line("The chronicle, its ink still wet.");
                return;

            case "legibility": {
                const value = Number(args[1]);
                if (args[1] === undefined || !Number.isFinite(value) || value < 0 || value > 1) {
                    reply.line("Usage: /book legibility <0..1> -- 0 is fully scrambled, 1 fully legible.");
                    return;
                }
                player.emit(BOOK_EVENT, JSON.stringify({ action: "legibility", value }));
                return;
            }

            case "close":
            case "status":
                // The book is the client's own; the answer comes back as a notification.
                player.emit(BOOK_EVENT, JSON.stringify({ action: first.toLowerCase() }));
                return;

            default:
                usage();
        }
    },
};
