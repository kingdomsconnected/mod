import { join } from "../args.js";
import type { Command, CommandContext } from "../command.js";
import { NO_POSE_YET, distanceBetween, hasPose, inFrontOf } from "../place.js";

/** How far in front of the caller a horse is put. Far enough not to land on them. */
const SPAWN_DISTANCE = 4;

/** How far `/horse name` and `/horse info` look for the horse they are about. */
const REACH = 12;

/** The horse a caller means: the one they are riding, or the nearest within reach. */
function horseInReach(player: Player): Horse | null {
    let nearest: Horse | null = null;
    let nearestDistance = REACH;

    for (const horse of Horse.all()) {
        if (horse.riderId === player.id) {
            return horse;
        }
        const distance = distanceBetween(horse.position, player.position);
        if (distance <= nearestDistance) {
            nearestDistance = distance;
            nearest = horse;
        }
    }
    return nearest;
}

/** The slots `/horse gear off` takes, in the order a horse is dressed. */
const GEAR_SLOTS = ["saddle", "head", "torso", "shoe"] as const;
type GearSlot = (typeof GEAR_SLOTS)[number];

function isGearSlot(value: string | undefined): value is GearSlot {
    return GEAR_SLOTS.some((slot) => slot === value);
}

function describeGear(horse: Horse): string {
    const gear = horse.gear;
    const worn = GEAR_SLOTS.map((slot) => `${slot} ${gear[slot] ?? "-"}`).join(", ");
    return `Horse ${horse.id} wears: ${worn}${horse.gearLocked ? " (locked)" : ""}.`;
}

/** `/horse gear ...` on the horse the caller is at. */
function runGear(horse: Horse, args: readonly string[], reply: CommandContext["reply"]): void {
    const action = args[1];
    if (action === undefined) {
        reply.line(describeGear(horse));
        return;
    }

    if (action === "items") {
        const slot = args[2];
        if (slot !== undefined && !isGearSlot(slot)) {
            reply.line(`No slot '${slot}'. Slots: ${GEAR_SLOTS.join(", ")}.`);
            return;
        }
        const items = Horse.gearItems(slot);
        reply.line(`${items.length} item(s)${slot ? ` for the ${slot}` : ""}, e.g. ${items.slice(0, 8).join(", ")}.`);
        return;
    }

    if (action === "presets") {
        const presets = Horse.gearPresets();
        reply.line(`${presets.length} preset(s), e.g. ${presets.filter((name) => name.startsWith("horse_")).slice(0, 8).join(", ")}.`);
        return;
    }

    if (action === "preset") {
        const name = args[2];
        if (name === undefined) {
            reply.line("Usage: /horse gear preset <name> -- see /horse gear presets");
            return;
        }
        try {
            horse.setGear(name);
            reply.line(describeGear(horse));
        } catch {
            reply.line(`'${name}' is not a horse gear preset. See /horse gear presets.`);
        }
        return;
    }

    if (action === "lock" || action === "unlock") {
        horse.gearLocked = action === "lock";
        reply.line(`Horse ${horse.id}'s gear is ${horse.gearLocked ? "locked: riders' changes are put back" : "unlocked"}.`);
        return;
    }

    if (action === "off") {
        const slot = args[2];
        if (!isGearSlot(slot)) {
            reply.line(`Usage: /horse gear off <${GEAR_SLOTS.join(" | ")}>`);
            return;
        }
        reply.line(horse.removeGear(slot) ? describeGear(horse) : `Horse ${horse.id} has nothing on its ${slot}.`);
        return;
    }

    if (action === "bare") {
        horse.setGear({});
        reply.line(describeGear(horse));
        return;
    }

    reply.line(horse.equipGear(action) ? describeGear(horse) : `'${action}' is not horse gear, or needs a saddle first. See /horse gear items.`);
}

function describe(horse: Horse): string {
    const shown = horse.name === "" ? "<its soul's own>" : horse.name;
    const life = horse.alive ? `health ${horse.health.toFixed(0)}/${horse.maxHealth.toFixed(0)}, stamina ${horse.stamina.toFixed(0)}/${horse.maxStamina.toFixed(0)}` : "dead";
    return `Horse ${horse.id}: name '${shown}', ${life}, capacity ${horse.inventoryCapacity.toFixed(2)}, soul ${horse.soul}, owner ${horse.ownerId}, rider ${horse.riderId}.`;
}

export const horseCommand: Command = {
    name: "horse",
    summary: "Spawns a horse you own by breed, or reads, renames, rides, gives, dresses, kills or revives the one you are at.",
    usage: ["/horse [breed | soul]", "/horse breeds", "/horse name [text]", "/horse info", "/horse ride [instant]", "/horse give [player id]", "/horse kill | /horse revive", "/horse gear [item | off <slot> | bare | preset <name> | presets | lock | unlock | items [slot]]"],

    run({ player, args, reply }: CommandContext): void {
        // Read before the first argument becomes a soul GUID; neither of
        // these is one.
        const subcommand = args[0];
        if (subcommand === "breeds") {
            reply.line(`Breeds: ${Horse.breeds().join(", ")}. Anything else is taken as a soul GUID.`);
            return;
        }

        if (subcommand === "name" || subcommand === "info" || subcommand === "kill" || subcommand === "revive" || subcommand === "gear" || subcommand === "ride" || subcommand === "give") {
            const horse = horseInReach(player);
            if (!horse) {
                reply.line("No horse nearby. Ride one, or stand next to one.");
                return;
            }

            if (subcommand === "info") {
                reply.line(describe(horse));
                reply.line(describeGear(horse));
                return;
            }

            if (subcommand === "ride") {
                const instant = args[1] === "instant";
                reply.line(player.mount(horse, { instant }) ? `${instant ? "Seating you on" : "Climbing onto"} horse ${horse.id}.` : `Horse ${horse.id} is dead or taken.`);
                return;
            }

            if (subcommand === "gear") {
                runGear(horse, args, reply);
                return;
            }

            if (subcommand === "give") {
                // No id is the caller themselves: the way to claim a horse.
                const target = args[1] === undefined ? player : Player.getById(Number(args[1]));
                if (!target) {
                    reply.line("No such player.");
                    return;
                }
                horse.giveTo(target);
                reply.line(`Horse ${horse.id} now belongs to ${target.nickname}.`);
                return;
            }

            if (subcommand === "kill") {
                reply.line(horse.kill() ? `Horse ${horse.id} is dead.` : `Horse ${horse.id} is already dead.`);
                return;
            }

            if (subcommand === "revive") {
                reply.line(horse.revive() ? `Horse ${horse.id} is back on its feet.` : `Horse ${horse.id} is not dead.`);
                return;
            }

            const name = join(args, 1);
            horse.name = name;
            reply.line(name === "" ? `Horse ${horse.id} handed back to its soul's own name.` : `Horse ${horse.id} is now '${name}'.`);
            return;
        }

        if (!hasPose(player)) {
            reply.line(NO_POSE_YET);
            return;
        }

        const { position, rotation } = inFrontOf(player, SPAWN_DISTANCE);
        const horse = Horse.spawn(position, rotation, subcommand);
        // Spawned for the caller, so it is theirs: their own horse is the one the game lets them mount without stealing it.
        horse.giveTo(player);
        reply.line(`Spawned horse ${horse.id} (soul ${horse.soul}), now yours.`);
    },
};
