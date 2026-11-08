import { readEntityId, readNumber } from "../args.js";
import type { Command, CommandContext } from "../command.js";
import { NO_POSE_YET, hasPose, inFrontOf } from "../place.js";

/** How far in front of the caller a marker is put. Far enough to see the whole thing rather than stand in the middle of it. */
const PLACE_DISTANCE = 4;

/** How many lines `/marker names` fills the caller's chat with. */
const LIST_LIMIT = 20;

/** The binding clamps to these; refusing here answers a typo instead of squashing it. */
const MIN_SIZE = 0.25;
const MAX_SIZE = 64;
const MAX_SPIN = 720;
const MAX_BOB = 4;

/** Every decal material name carries this, so a bare word can be completed into one. */
const MATERIAL_PREFIX = "materials/decals/";

/** Short names for the mesh palette: the real ones say what the engine uses them for, not what colour they are. */
const COLOURS: Record<string, string> = {
    red: "materials/special/collision_proxy_material",
    green: "materials/special/collision_proxy_counterfeiters_barrier",
    blue: "materials/special/collision_proxy_deep_water_barrier",
    orange: "materials/special/collision_proxy_level_barrier",
    magenta: "materials/special/collision_proxy_horseonly",
    purple: "materials/special/collision_proxy_entitiesonly",
    cyan: "materials/special/collision_proxy_window",
    pink: "materials/special/raycast_proxy",
    grey: "materials/special/shadow_proxy",
    white: "materials/filters/fade_short_70",
};

/** The mesh shapes, which are also the words that can lead the command instead of a material. */
const MESH_SHAPES = ["cylinder", "sphere", "chevron", "cube", "plane"] as const;

/** Words that mean a subcommand rather than a material, so a material is never mistaken for one. */
const SUBCOMMANDS = ["names", "colours", "colors", "list", "clear", "remove", "resize", "spin", "bob", "trigger", "tint"] as const;

type MeshShape = (typeof MESH_SHAPES)[number];

function isMeshShape(word: string | undefined): word is MeshShape {
    return MESH_SHAPES.includes(word as MeshShape);
}

/** Newest first, which is what somebody who has just placed things wants. */
function inWorld(virtualWorld: number): Marker[] {
    return Marker.all(virtualWorld).sort((left, right) => right.id - left.id);
}

/** Completes a bare word: a colour first, else the decal folder. A name with a slash is already a path. */
function qualify(name: string): string {
    if (name.includes("/")) {
        return name;
    }
    return COLOURS[name] ?? `${MATERIAL_PREFIX}${name}`;
}

/** The marker an id argument names, or null once it has answered the caller itself. */
function resolve(id: string | undefined, world: number, reply: CommandContext["reply"]): Marker | null {
    const parsed = readEntityId(id);
    if (parsed === null) {
        reply.line("That is not an id. /marker list prints them.");
        return null;
    }
    const marker = Marker.getById(parsed);
    if (!marker || marker.virtualWorld !== world) {
        reply.line(`No marker in this world has id ${parsed}.`);
        return null;
    }
    return marker;
}

export const markerCommand: Command = {
    name: "marker",
    summary: "Puts a marker in the world: a decal projected on the ground, or a mesh standing in it.",
    usage: [
        "/marker <material> [size]",
        "/marker <cylinder|sphere|chevron|cube|plane> <colour> [size] [height]",
        "/marker colours",
        "/marker names [prefix]",
        "/marker list",
        "/marker resize <id> <size> [height]",
        "/marker spin <id> <degreesPerSecond>",
        "/marker bob <id> <metres>",
        "/marker trigger <id> on|off",
        "/marker tint <id> <r> <g> <b> [a]",
        "/marker remove <id>",
        "/marker clear",
    ],

    run({ player, args, reply, usage }: CommandContext): void {
        const world = player.virtualWorld;
        const head = args[0];

        if (head === "colours" || head === "colors") {
            reply.line(`Mesh shapes take a colour: ${Object.keys(COLOURS).join(", ")}.`);
            reply.line("Projected markers take a decal name instead -- /marker names lists those.");
            return;
        }

        if (head === "names") {
            const names = Marker.list(args[1] === undefined ? "" : qualify(args[1]));
            if (names.length === 0) {
                reply.line(`No material name starts with '${args[1] ?? ""}'.`);
                return;
            }
            const shown = names.slice(0, LIST_LIMIT);
            reply.line(`${shown.length} of ${names.length} material name(s):`);
            reply.lines(shown.map((name) => `  ${name.startsWith(MATERIAL_PREFIX) ? name.slice(MATERIAL_PREFIX.length) : name}`));
            return;
        }

        if (head === "list") {
            const markers = inWorld(world);
            if (markers.length === 0) {
                reply.line("No marker has been placed in this world yet.");
                return;
            }
            const shown = markers.slice(0, LIST_LIMIT);
            reply.line(`${shown.length} of ${markers.length} marker(s), newest first:`);
            reply.lines(shown.map((marker) => `  ${marker.id}: ${marker.shape} ${marker.material} (${marker.size.toFixed(2)} m)`));
            return;
        }

        if (head === "clear") {
            reply.line(`Removed ${Marker.removeAll(world)} marker(s).`);
            return;
        }

        if (head === "remove") {
            const marker = resolve(args[1], world, reply);
            if (marker) {
                marker.remove();
                reply.line(`Removed marker ${marker.id}.`);
            }
            return;
        }

        if (head === "resize" || head === "spin" || head === "bob" || head === "trigger" || head === "tint") {
            const marker = resolve(args[1], world, reply);
            if (!marker) {
                return;
            }

            if (head === "tint") {
                const red = readNumber(args[2]);
                const green = readNumber(args[3]);
                const blue = readNumber(args[4]);
                if (red === null || green === null || blue === null || [red, green, blue].some((c) => c < 0 || c > 255)) {
                    reply.line("Usage: /marker tint <id> <r> <g> <b> [a], each 0-255.");
                    return;
                }
                const alpha = args[5] === undefined ? 255 : readNumber(args[5]);
                if (alpha === null || alpha < 0 || alpha > 255) {
                    reply.line(`'${args[5]}' is not an alpha. They run from 0 to 255.`);
                    return;
                }
                // Packed 0xRRGGBBAA, and 0 would mean "as it ships" rather than
                // a transparent black, so a fully clear tint keeps one alpha bit.
                marker.color = red * 0x1000000 + green * 0x10000 + blue * 0x100 + Math.max(alpha, 1);
                reply.line(`Marker ${marker.id} tinted ${red},${green},${blue} at alpha ${alpha}.`);
                return;
            }

            if (head === "trigger") {
                const on = args[2];
                if (on !== "on" && on !== "off") {
                    reply.line("Usage: /marker trigger <id> on|off.");
                    return;
                }
                marker.trigger = on === "on";
                reply.line(`Marker ${marker.id} ${marker.trigger ? "now reports" : "no longer reports"} players crossing it.`);
                return;
            }

            const value = readNumber(args[2]);
            if (head === "spin") {
                if (value === null || Math.abs(value) > MAX_SPIN) {
                    reply.line(`'${args[2] ?? ""}' is not a spin. They run from -${MAX_SPIN} to ${MAX_SPIN} degrees per second.`);
                    return;
                }
                marker.spin = value;
                reply.line(`Marker ${marker.id} now turns at ${marker.spin.toFixed(0)} deg/s.`);
                return;
            }

            if (head === "bob") {
                if (value === null || value < 0 || value > MAX_BOB) {
                    reply.line(`'${args[2] ?? ""}' is not a bob. They run from 0 to ${MAX_BOB} metres.`);
                    return;
                }
                marker.bob = value;
                reply.line(`Marker ${marker.id} now bobs ${marker.bob.toFixed(2)} m.`);
                return;
            }

            if (value === null || value < MIN_SIZE || value > MAX_SIZE) {
                reply.line(`'${args[2] ?? ""}' is not a size. They run from ${MIN_SIZE} to ${MAX_SIZE} metres.`);
                return;
            }
            marker.size = value;

            const tall = readNumber(args[3]);
            if (tall !== null) {
                if (tall < MIN_SIZE || tall > MAX_SIZE) {
                    reply.line(`'${args[3]}' is not a height. They run from ${MIN_SIZE} to ${MAX_SIZE} metres.`);
                    return;
                }
                marker.height = tall;
            }
            reply.line(`Marker ${marker.id} is now ${marker.size.toFixed(2)} m across and ${marker.height.toFixed(2)} m tall.`);
            return;
        }

        // A leading shape word shifts everything along by one; without it the
        // marker is the projected kind, which is what most of them are.
        const shape = isMeshShape(head) ? head : "decal";
        const nameArgument = isMeshShape(head) ? args[1] : head;
        const sizeArgument = isMeshShape(head) ? args[2] : args[1];
        const heightArgument = isMeshShape(head) ? args[3] : undefined;

        if (nameArgument === undefined || SUBCOMMANDS.includes(nameArgument as (typeof SUBCOMMANDS)[number])) {
            usage();
            return;
        }

        let size = 4;
        if (sizeArgument !== undefined) {
            const parsed = readNumber(sizeArgument);
            if (parsed === null || parsed < MIN_SIZE || parsed > MAX_SIZE) {
                reply.line(`'${sizeArgument}' is not a size. They run from ${MIN_SIZE} to ${MAX_SIZE} metres.`);
                return;
            }
            size = parsed;
        }

        // A standing shape with no height asked for is made as tall as it is
        // wide, which reads as a marker rather than as a disc or a pillar.
        let height = size;
        if (heightArgument !== undefined) {
            const parsed = readNumber(heightArgument);
            if (parsed === null || parsed < MIN_SIZE || parsed > MAX_SIZE) {
                reply.line(`'${heightArgument}' is not a height. They run from ${MIN_SIZE} to ${MAX_SIZE} metres.`);
                return;
            }
            height = parsed;
        }

        if (!hasPose(player)) {
            reply.line(NO_POSE_YET);
            return;
        }

        // Yaw only, so a decal's texture is oriented rather than tipped off
        // the ground, and a mesh shape faces whoever asked for it.
        const { position, rotation } = inFrontOf(player, PLACE_DISTANCE);

        let marker: Marker;
        try {
            // An unknown name is a typo to answer, not a stack trace to print.
            marker = Marker.place(qualify(nameArgument), position, { shape, rotation, size, height }, world);
        } catch {
            // Both an unknown name and a known one of the wrong kind arrive
            // here; the second is likelier, so the hint names it.
            const hint = shape === "decal" ? "Projected markers take a decal name -- /marker names <prefix> lists them." : "Mesh shapes take a colour, not a decal name -- /marker colours lists them.";
            reply.line(`'${nameArgument}' cannot be drawn as a ${shape}. ${hint}`);
            return;
        }

        reply.line(`Placed ${marker.shape} marker ${marker.id} ('${marker.material}', ${marker.size.toFixed(2)} m). Remove it with /marker remove ${marker.id}.`);
    },
};
