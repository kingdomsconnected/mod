import { readNumber } from "../args.js";
import { beginBuild, endBuild, isBuilding } from "../build.js";
import type { Command, CommandContext } from "../command.js";
import { NO_POSE_YET, hasPose } from "../place.js";

/** What `Prop.spawn` takes for its physics argument. */
const PHYSICS_KINDS = ["rigid", "static", "none"] as const;

/** The binding clamps to these; refusing here answers a typo instead of squashing it. */
const MIN_SCALE = 0.01;
const MAX_SCALE = 100;

export const buildCommand: Command = {
    name: "build",
    summary: "Places a mesh where you aim, with a live preview, instead of dropping it in front of you.",
    usage: ["/build <model> [scale] [rigid|static|none]", "/build stop"],

    run({ player, args, reply, usage }: CommandContext): void {
        if (args[0] === "stop") {
            if (!isBuilding(player)) {
                reply.line("You are not building anything.");
                return;
            }
            const placed = endBuild(player, true);
            reply.line(placed > 0 ? `Done. You placed ${placed}.` : "Stopped building.");
            return;
        }

        const model = args[0];
        if (!model) {
            usage();
            return;
        }

        if (!hasPose(player)) {
            reply.line(NO_POSE_YET);
            return;
        }

        if (isBuilding(player)) {
            reply.line("You are already building. /build stop first, or right-click.");
            return;
        }

        let scale = 1;
        if (args[1] !== undefined) {
            const asked = readNumber(args[1]);
            if (asked === null || asked < MIN_SCALE || asked > MAX_SCALE) {
                reply.line(`Scale must be a number between ${MIN_SCALE} and ${MAX_SCALE}.`);
                return;
            }
            scale = asked;
        }

        let physics: string = "static";
        if (args[2] !== undefined) {
            if (!(PHYSICS_KINDS as readonly string[]).includes(args[2])) {
                reply.line(`Physics must be one of ${PHYSICS_KINDS.join(", ")}.`);
                return;
            }
            physics = args[2];
        }

        // Not checked here: the client refuses an unknown mesh before it
        // draws one, and `Prop.spawn` refuses it again on the confirm.
        beginBuild(player, model, scale, physics);
        reply.line(`Building ${model}. Look where you want it, left-click to place, right-click when you are done.`);
    },
};
