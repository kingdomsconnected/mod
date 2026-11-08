import type { Command } from "../command.js";

export const builderCommand: Command = {
    name: "builder",
    summary: "Enable World Builder access for yourself; F7 opens it.",
    usage: ["/builder [on|off]"],
    run({ player, args, reply, usage }) {
        const mode = args[0]?.toLowerCase() ?? "on";
        if (args.length > 1 || (mode !== "on" && mode !== "off")) {
            usage();
            return;
        }
        const enabled = mode === "on";
        if (!player.setWorldBuilderEnabled(enabled)) {
            reply.line("World Builder access could not be changed.");
            return;
        }
        reply.line(enabled ? "World Builder enabled. Press F7 to open it." : "World Builder disabled.");
    },
};
