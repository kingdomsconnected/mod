import { readEntityId, readNumber } from "../args.js";
import type { Command, CommandContext } from "../command.js";
import { NO_POSE_YET, hasPose, inFrontOf } from "../place.js";

/** How far in front of the caller a mesh is put. Wider than a stash: some of the catalog is architecture. */
const SPAWN_DISTANCE = 5;

/** How many lines `/prop list` fills the caller's chat with. */
const LIST_LIMIT = 20;

/** What `Prop.spawn` takes for its physics argument. */
const PHYSICS_KINDS = ["rigid", "static", "none"] as const;

/** The binding clamps to these; refusing here answers a typo instead of squashing it. */
const MIN_SCALE = 0.01;
const MAX_SCALE = 100;

/** Newest first, which is what somebody who has just spawned things wants. */
function inWorld(virtualWorld: number): Prop[] {
    return Prop.all()
        .filter((prop) => prop.virtualWorld === virtualWorld)
        .sort((left, right) => right.id - left.id);
}

export const propCommand: Command = {
    name: "prop",
    summary: "Puts a mesh from the game's own object catalog into the world.",
    usage: ["/prop <model> [scale] [rigid|static|none]", "/prop list", "/prop remove <id>", "/prop clear"],

    run({ player, args, reply, usage }: CommandContext): void {
        // Read before the first argument becomes a model: a catalog stem is a
        // mesh file name, and none of these is.
        const subcommand = args[0];
        const world = player.virtualWorld;

        if (subcommand === "list") {
            const props = inWorld(world);
            if (props.length === 0) {
                reply.line("No prop has been spawned in this world yet.");
                return;
            }
            const shown = props.slice(0, LIST_LIMIT);
            reply.line(`${shown.length} of ${props.length} prop(s), newest first:`);
            reply.lines(shown.map((prop) => `  ${prop.id}: ${prop.modelName} (${prop.physics}, ${prop.scale.toFixed(2)}x) in ${prop.modelGroup}`));
            return;
        }

        if (subcommand === "clear") {
            reply.line(`Despawned ${Prop.destroyAll(world)} prop(s).`);
            return;
        }

        if (subcommand === "remove") {
            const id = readEntityId(args[1]);
            if (id === null) {
                reply.line("Usage: /prop remove <id>. /prop list prints the ids.");
                return;
            }
            const prop = Prop.getById(id);
            if (!prop || prop.virtualWorld !== world) {
                reply.line(`No prop in this world has id ${id}.`);
                return;
            }
            prop.destroy();
            reply.line(`Despawned prop ${id}.`);
            return;
        }

        if (subcommand === undefined) {
            usage();
            return;
        }

        let scale = 1;
        if (args.length >= 2) {
            const parsed = readNumber(args[1]);
            if (parsed === null || parsed < MIN_SCALE || parsed > MAX_SCALE) {
                reply.line(`'${args[1]}' is not a scale. They run from ${MIN_SCALE.toFixed(2)} to ${MAX_SCALE}.`);
                return;
            }
            scale = parsed;
        }

        const physics = args[2] ?? "static";
        if (!PHYSICS_KINDS.includes(physics as (typeof PHYSICS_KINDS)[number])) {
            reply.line(`'${physics}' is not a physics kind. They are ${PHYSICS_KINDS.join(", ")}.`);
            return;
        }

        if (!hasPose(player)) {
            reply.line(NO_POSE_YET);
            return;
        }

        const { position, rotation } = inFrontOf(player, SPAWN_DISTANCE);

        // Caught for the same reason `/drop` catches its item.
        let prop: Prop;
        try {
            prop = Prop.spawn(subcommand, position, rotation, scale, physics, world);
        } catch {
            reply.line(`'${subcommand}' is not a mesh in the catalog. A full path or a file stem both work.`);
            return;
        }

        reply.line(`Spawned prop ${prop.id} ('${prop.modelName}', ${prop.physics}, ${prop.scale.toFixed(2)}x). Remove it with /prop remove ${prop.id}.`);
    },
};
