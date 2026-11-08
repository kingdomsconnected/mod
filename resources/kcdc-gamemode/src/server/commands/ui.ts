import type { Command, CommandContext } from "../command.js";
import { openPanelFor } from "../panel.js";

export const uiCommand: Command = {
    name: "ui",
    summary: "Opens the debug panel on your own screen.",
    usage: ["/ui"],

    run({ player, reply }: CommandContext): void {
        openPanelFor(player);
        reply.line("Opening the debug panel. F4 opens and closes it; Esc hands input back to the game.");
    },
};
