import { readEntityId, readNumber } from "../args.js";
import type { Command, CommandContext } from "../command.js";
import { NO_POSE_YET, hasPose, inFrontOf } from "../place.js";

/** How far in front of the caller an effect is placed. Close enough to see it, far enough not to stand inside it. */
const SPAWN_DISTANCE = 3;

/** How many lines `/vfx list` fills the caller's chat with. */
const LIST_LIMIT = 20;

/** The binding clamps to these; refusing here answers a typo instead of squashing it. */
const MIN_SCALE = 0.01;
const MAX_SCALE = 32;

/** Newest first, which is what somebody who has just placed things wants. */
function inWorld(virtualWorld: number): Vfx[] {
    return Vfx.all(virtualWorld).sort((left, right) => right.id - left.id);
}

export const vfxCommand: Command = {
    name: "vfx",
    summary: "Places a particle effect from the game's own libraries, or fires one off.",
    usage: ["/vfx <effect> [scale]", "/vfx burst <effect> [scale]", "/vfx names [prefix]", "/vfx list", "/vfx remove <id>", "/vfx clear"],

    run({ player, args, reply, usage }: CommandContext): void {
        // Read before the first argument becomes an effect name: an effect name
        // always carries a dot, and none of these does.
        const subcommand = args[0];
        const world = player.virtualWorld;

        if (subcommand === "names") {
            const names = Vfx.list(args[1] ?? "");
            if (names.length === 0) {
                reply.line(`No effect name starts with '${args[1] ?? ""}'.`);
                return;
            }
            const shown = names.slice(0, LIST_LIMIT);
            reply.line(`${shown.length} of ${names.length} effect name(s):`);
            reply.lines(shown.map((name) => `  ${name}`));
            return;
        }

        if (subcommand === "list") {
            const effects = inWorld(world);
            if (effects.length === 0) {
                reply.line("No effect has been placed in this world yet.");
                return;
            }
            const shown = effects.slice(0, LIST_LIMIT);
            reply.line(`${shown.length} of ${effects.length} effect(s), newest first:`);
            reply.lines(shown.map((effect) => `  ${effect.id}: ${effect.effect} (${effect.scale.toFixed(2)}x)`));
            return;
        }

        if (subcommand === "clear") {
            reply.line(`Stopped ${Vfx.destroyAll(world)} effect(s).`);
            return;
        }

        if (subcommand === "remove") {
            const id = readEntityId(args[1]);
            if (id === null) {
                reply.line("Usage: /vfx remove <id>. /vfx list prints the ids.");
                return;
            }
            const effect = Vfx.getById(id);
            if (!effect || effect.virtualWorld !== world) {
                reply.line(`No effect in this world has id ${id}.`);
                return;
            }
            effect.destroy();
            reply.line(`Stopped effect ${id}.`);
            return;
        }

        const bursting = subcommand === "burst";
        const name = bursting ? args[1] : subcommand;
        if (name === undefined) {
            usage();
            return;
        }

        const scaleArgument = bursting ? args[2] : args[1];
        let scale = 1;
        if (scaleArgument !== undefined) {
            const parsed = readNumber(scaleArgument);
            if (parsed === null || parsed < MIN_SCALE || parsed > MAX_SCALE) {
                reply.line(`'${scaleArgument}' is not a scale. They run from ${MIN_SCALE.toFixed(2)} to ${MAX_SCALE}.`);
                return;
            }
            scale = parsed;
        }

        if (!hasPose(player)) {
            reply.line(NO_POSE_YET);
            return;
        }

        const { position, rotation } = inFrontOf(player, SPAWN_DISTANCE);

        if (bursting) {
            // Caught for the same reason `/prop` catches its model: an unknown
            // name is a typo to answer, not a stack trace to print.
            let told: number;
            try {
                told = Vfx.burst(name, position, { rotation, scale }, world);
            } catch {
                reply.line(`'${name}' is not an effect. /vfx names <prefix> lists what there is.`);
                return;
            }
            reply.line(`Burst '${name}' for ${told} player(s).`);
            return;
        }

        let effect: Vfx;
        try {
            // Primed, because most of what is worth placing is a fire or a
            // plume, and an unprimed one visibly grows from nothing.
            effect = Vfx.spawn(name, position, { rotation, scale, prime: true }, world);
        } catch {
            reply.line(`'${name}' is not an effect. /vfx names <prefix> lists what there is.`);
            return;
        }

        reply.line(`Placed effect ${effect.id} ('${effect.effect}', ${effect.scale.toFixed(2)}x). Stop it with /vfx remove ${effect.id}.`);
    },
};
