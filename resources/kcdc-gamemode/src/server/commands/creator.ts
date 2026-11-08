import type { Command, CommandContext } from "../command.js";

/**
 * The server half of the character creator sample.
 *
 * The creator itself is client-only: the client script opens it and hears
 * what the player accepted. This half only asks a client to open one and
 * puts on whatever look comes back -- through `player.setAppearance`, which
 * checks it against the catalog like any other look a script asks for. A real
 * gamemode would charge for it or store it here first.
 */

const RESOURCE = "kcdc-gamemode";
export const OPEN_EVENT = `${RESOURCE}:creator.open`;
export const CLOSE_EVENT = `${RESOURCE}:creator.close`;
export const ACCEPTED_EVENT = `${RESOURCE}:creator.accepted`;

/** The players this sample asked to open one, so a look nobody asked for is not put on. */
const asked = new Set<number>();

export function installCreatorSample(): void {
    Events.onClient(ACCEPTED_EVENT, (sender, payload) => {
        const player = sender as Player;
        if (!asked.delete(player.id)) {
            return;
        }

        let look: unknown;
        try {
            look = typeof payload === "string" ? JSON.parse(payload) : payload;
        } catch {
            look = undefined;
        }
        if (typeof look !== "object" || look === null || !player.setAppearance(look as Partial<Appearance>)) {
            Chat.sendToPlayer(player, "That look could not be put on.");
            return;
        }
        Chat.sendToPlayer(player, "You are wearing your new look.");
    });

    Events.on("playerDisconnect", (player) => {
        asked.delete(player.id);
    });
}

export const creatorCommand: Command = {
    name: "creator",
    summary: "Opens the character creator on the game's own character stage.",
    usage: ["/creator [male|female|both]", "/creator stop"],
    run(context: CommandContext): void {
        const { player, args } = context;
        if (args[0] === "stop") {
            asked.delete(player.id);
            player.emit(CLOSE_EVENT);
            return;
        }

        const genders = args[0] ?? "both";
        if (args.length > 1 || (genders !== "male" && genders !== "female" && genders !== "both")) {
            context.usage();
            return;
        }

        asked.add(player.id);
        player.emit(OPEN_EVENT, JSON.stringify({ genders }));
    },
};
