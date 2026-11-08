import type { Command, CommandContext } from "../command.js";

/** Answered by the client half in `src/client/screen.ts`. */
export const SCREEN_EVENT = "kcdc-gamemode:screenfx";

const RAW_EFFECTS = ["blur", "ghosting", "screenBlood"];

function readLevel(text: string | undefined, fallback: number): number | undefined {
    if (text === undefined) {
        return fallback;
    }
    const level = Number(text);
    return Number.isFinite(level) && level >= 0 && level <= 1 ? level : undefined;
}

export const screenFxCommand: Command = {
    name: "screenfx",
    summary: "Puts screen effects on your own picture: the intoxication and injury examples, or one raw effect.",
    usage: [
        "/screenfx drunk [0-1]              -- the intoxication example at that level, 0 to sober up",
        "/screenfx injured [0-1]            -- the injury example at that level, 0 to heal",
        "/screenfx hit                      -- one flash of blood, as a hit would",
        "/screenfx grey [0-2]               -- a colour grade at that saturation, 0 being grey",
        "/screenfx raw <blur|ghosting|screenBlood> [0-1]",
        "/screenfx clear                    -- takes every effect from this command off",
        "/screenfx list                     -- every effect and its parameters, in the client console",
    ],

    run({ player, args, reply, usage }: CommandContext): void {
        const kind = args[0];
        switch (kind) {
            case "drunk":
            case "injured": {
                const level = readLevel(args[1], 0.6);
                if (level === undefined) {
                    usage();
                    return;
                }
                player.emit(SCREEN_EVENT, JSON.stringify({ kind, level }));
                break;
            }
            case "grey": {
                const amount = args[1] === undefined ? 0 : Number(args[1]);
                if (!Number.isFinite(amount) || amount < 0 || amount > 2) {
                    usage();
                    return;
                }
                player.emit(SCREEN_EVENT, JSON.stringify({ kind, amount }));
                break;
            }
            case "raw": {
                const effect = args[1];
                const amount = readLevel(args[2], 0.5);
                if (effect === undefined || !RAW_EFFECTS.includes(effect) || amount === undefined) {
                    usage();
                    return;
                }
                player.emit(SCREEN_EVENT, JSON.stringify({ kind, effect, amount }));
                break;
            }
            case "hit":
            case "clear":
            case "list":
                player.emit(SCREEN_EVENT, JSON.stringify({ kind }));
                break;
            default:
                usage();
                return;
        }

        // The picture is the client's own, so the server only carries the ask.
        reply.line(`Screen effect: ${args.join(" ")}`);
    },
};
