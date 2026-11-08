import { readNumber } from "../args.js";
import type { Command, CommandContext } from "../command.js";

/** What the World binding refuses outside of, spelled here so a typo is answered with the range. */
const MAX_TIME_SCALE = 200;
const MAX_BLEND_SECONDS = 21600;
const MAX_WIND_SPEED = 50;

/** The clock and the sky as one line. */
function describe(): string {
    const blending = World.weatherBlending ? `, blending from ${World.previousWeather} (${World.weatherRemaining.toFixed(0)}s left)` : "";
    const wind = World.wind;
    return `World day ${World.day}, hour ${World.hour.toFixed(2)}, scale ${World.timeScale.toFixed(2)}, weather ${World.weather}${blending}, rain ${World.rainIntensity.toFixed(2)} (wetness ${World.wetness.toFixed(2)}, puddles ${World.puddles.toFixed(2)}), wind ${World.windSpeed.toFixed(2)} m/s [${wind.x.toFixed(2)}, ${wind.y.toFixed(2)}, ${wind.z.toFixed(2)}].`;
}

/** Every write is refused rather than clamped, so one wording covers them all. */
const REFUSED = "Refused: a rewind, a value out of range, or a weather blend still running.";

export const worldCommand: Command = {
    name: "world",
    summary: "Reads and sets the shared clock and weather.",
    usage: [
        "/world [info]",
        `/world scale <0..${MAX_TIME_SCALE}>`,
        "/world hour <0..24>",
        "/world day <day> <hour>",
        `/world weather <preset> [seconds 0..${MAX_BLEND_SECONDS}]`,
        "/world rain <0..1> [amount 0..1]",
        `/world wind <x> <y> [z]  (metres per second, up to ${MAX_WIND_SPEED})`,
    ],

    run({ args, reply, usage }: CommandContext): void {
        const subcommand = args[0];

        if (subcommand === undefined || subcommand === "info") {
            reply.line(describe());
            return;
        }

        if (subcommand === "help") {
            usage();
            return;
        }

        if (subcommand === "scale") {
            const scale = readNumber(args[1]);
            reply.line(scale !== null && World.setTimeScale(scale) ? describe() : REFUSED);
            return;
        }

        if (subcommand === "hour") {
            const hour = readNumber(args[1]);
            reply.line(hour !== null && World.setHour(hour) ? describe() : REFUSED);
            return;
        }

        if (subcommand === "day") {
            const day = readNumber(args[1]);
            const hour = readNumber(args[2]);
            reply.line(day !== null && hour !== null && World.setTime(day, hour) ? describe() : REFUSED);
            return;
        }

        if (subcommand === "weather") {
            const preset = args[1];
            if (preset === undefined) {
                usage();
                return;
            }
            // Only a client can tell whether a preset exists, so a name the
            // game does not know is taken and leaves every sky where it is.
            const seconds = args.length >= 3 ? readNumber(args[2]) : 0;
            reply.line(seconds !== null && World.setWeather(preset, seconds) ? describe() : REFUSED);
            return;
        }

        if (subcommand === "rain") {
            const intensity = readNumber(args[1]);
            const amount = args.length >= 3 ? readNumber(args[2]) : 1;
            reply.line(intensity !== null && amount !== null && World.setRain(intensity, amount) ? describe() : REFUSED);
            return;
        }

        if (subcommand === "wind") {
            const x = readNumber(args[1]);
            const y = readNumber(args[2]);
            const z = args.length >= 4 ? readNumber(args[3]) : 0;
            if (x === null || y === null || z === null) {
                usage();
                return;
            }
            reply.line(World.setWind({ x, y, z }) ? describe() : REFUSED);
            return;
        }

        usage();
    },
};
