import { readNumber } from "../args.js";
import type { Command, CommandContext } from "../command.js";

function describe(): string {
    const range = Gathering.respawnTime;
    if (range === null) {
        return "Herbs use species defaults, 5x faster than single player (48-144 real minutes at world scale 15).";
    }
    if (range.maxHours === 0) {
        return "Timed herb regrowth is disabled. /herbs respawn or respawn-all still works.";
    }
    const realTime = World.timeScale > 0
        ? ` About ${(range.minHours * 3600 / World.timeScale).toFixed(2)}-${(range.maxHours * 3600 / World.timeScale).toFixed(2)} real seconds at the current world speed.`
        : " The world clock is paused.";
    return `Herbs regrow in ${range.minHours.toFixed(4)}-${range.maxHours.toFixed(4)} game hours.${realTime}`;
}

export const herbsCommand: Command = {
    name: "herbs",
    summary: "Changes herb regrowth time or respawns harvested herbs.",
    usage: [
        "/herbs [info]",
        "/herbs time <minSeconds> [maxSeconds]  (0 0 disables timed regrowth)",
        "/herbs defaults",
        "/herbs respawn [radiusMetres]  (default 10, in your virtual world)",
        "/herbs respawn-all  (in your virtual world)",
    ],

    run({ player, args, reply, usage }: CommandContext): void {
        const subcommand = args[0];
        if (subcommand === undefined || subcommand === "info") {
            reply.line(describe());
            return;
        }
        if (subcommand === "time") {
            const min = readNumber(args[1]);
            const max = args[2] === undefined ? min : readNumber(args[2]);
            if (args.length > 3 || min === null || max === null || min < 0 || max < min) {
                usage();
                return;
            }
            if (max > 0 && World.timeScale === 0) {
                reply.line("The world clock is paused. Set /world scale above zero before choosing a duration in real seconds.");
                return;
            }
            // The API uses game hours. Keep this command in real seconds at
            // the current speed so short regrowth tests are easy to repeat.
            const hoursPerSecond = World.timeScale / 3600;
            if (!Gathering.setRespawnTime(min * hoursPerSecond, max * hoursPerSecond)) {
                reply.line("Refused: the range must fit within 87600 game hours and a positive upper bound must reach one game millisecond.");
                return;
            }
            reply.line(`${describe()} Existing timers restart now in every virtual world.`);
            return;
        }
        if (subcommand === "defaults" && args.length === 1) {
            Gathering.resetRespawnTime();
            reply.line(`${describe()} Existing timers restart now in every virtual world.`);
            return;
        }
        if (subcommand === "respawn" || subcommand === "respawn-all") {
            if (!player.ready) {
                reply.line("Wait until you are in the world.");
                return;
            }
            const radius = args[1] === undefined ? 10 : readNumber(args[1]);
            if (args.length > (subcommand === "respawn" ? 2 : 1) || radius === null || radius < 0 || radius > 100000) {
                usage();
                return;
            }
            const count = subcommand === "respawn-all"
                ? Gathering.respawnAll(player.virtualWorld)
                : Gathering.respawn(player.position, radius, player.virtualWorld);
            reply.line(`Respawned ${count} harvested herb sources in your virtual world${subcommand === "respawn" ? ` within ${radius} metres` : ""}.`);
            return;
        }
        usage();
    },
};
