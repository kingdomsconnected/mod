import type { Command, CommandContext } from "../command.js";

/** How many names one `list` reply prints before asking for a filter. */
const PAGE = 20;

export const outfitCommand: Command = {
    name: "outfit",
    summary: "Dresses you in one of the game's own outfits.",
    usage: ["/outfit random", "/outfit <preset>", "/outfit list [filter]"],

    run({ player, args, reply, usage }: CommandContext): void {
        const subcommand = args[0];
        if (subcommand === undefined) {
            usage();
            return;
        }

        // The game will not put a man's clothes on a woman or the reverse.
        const presets = Player.outfitPresets(player.appearance.gender);

        if (subcommand === "list") {
            const filter = (args[1] ?? "").toLowerCase();
            const matches = presets.filter((name) => name.toLowerCase().includes(filter));
            reply.line(`${matches.length} outfit(s)${matches.length > PAGE ? `, first ${PAGE}` : ""}: ${matches.slice(0, PAGE).join(", ")}`);
            return;
        }

        const preset = subcommand === "random" ? (presets[Math.floor(Math.random() * presets.length)] ?? "") : subcommand;
        reply.line(player.setOutfit(preset) ? `Dressed in ${preset}. What you had on is in your inventory.` : `Could not dress you in '${preset}'. See /outfit list.`);
    },
};
