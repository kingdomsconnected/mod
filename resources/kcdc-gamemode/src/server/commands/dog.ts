import { join } from "../args.js";
import type { Command, CommandContext } from "../command.js";
import { NO_POSE_YET, hasPose, inFrontOf } from "../place.js";

/** How far in front of the caller a dog is put. Far enough not to land on them. */
const SPAWN_DISTANCE = 2;

/**
 * The companion modes, in the game's own order. `E_DogCompanionMode` has
 * eight values and the engine refuses anything else, so this list is the
 * whole vocabulary rather than a selection from it.
 */
const MODES = ["wait", "follow", "free", "aggressive", "search", "hunt", "guard", "ambush"];

/**
 * The objectives worth naming back to a player. `E_DogObjective` has 26
 * values; the rest are internal states nobody asked a dog to be in.
 */
const OBJECTIVES: Record<number, string> = {
    0: "waiting",
    1: "marking",
    2: "barking",
    3: "digging",
    4: "following",
    5: "at heel",
    6: "following a rider",
    7: "searching",
    8: "fighting",
    9: "fetching",
    10: "hunting",
    11: "running away",
    12: "raising the alarm",
    19: "eating",
    21: "distracting",
    22: "being petted",
    23: "searching for an item",
    25: "idle",
};

function describe(dog: Dog): string {
    const shown = dog.name === "" ? "<its soul's own>" : dog.name;
    const doing = OBJECTIVES[dog.objective] ?? `objective ${dog.objective}`;
    return `Dog ${dog.id}: name '${shown}', owner ${dog.ownerId}, mode ${MODES[dog.mode] ?? dog.mode}, ${doing}, morale ${dog.morale.toFixed(2)}, soul ${dog.soul}.`;
}

export const dogCommand: Command = {
    name: "dog",
    summary: "Gives you a dog, or reads, renames and commands the one you have.",
    usage: ["/dog [soul]", "/dog info", "/dog name [text]", `/dog mode <${MODES.join("|")}>`, "/dog give <player id>", "/dog remove"],

    run({ player, args, reply }: CommandContext): void {
        // Read before the first argument becomes a soul GUID; none of these
        // is one.
        const subcommand = args[0];

        if (subcommand === "info" || subcommand === "name" || subcommand === "mode" || subcommand === "give" || subcommand === "remove") {
            const dog = player.dog;
            if (!dog) {
                reply.line("You have no dog. `/dog` gives you one.");
                return;
            }

            if (subcommand === "info") {
                reply.line(describe(dog));
                return;
            }

            if (subcommand === "name") {
                const name = join(args, 1);
                dog.name = name;
                reply.line(name === "" ? `Dog ${dog.id} handed back to its soul's own name.` : `Dog ${dog.id} is now '${name}'.`);
                return;
            }

            if (subcommand === "mode") {
                const wanted = (args[1] ?? "").toLowerCase();
                const mode = MODES.indexOf(wanted);
                if (mode < 0) {
                    reply.line(`Modes: ${MODES.join(", ")}.`);
                    return;
                }
                dog.mode = mode;
                reply.line(`Dog ${dog.id} is on ${wanted}.`);
                return;
            }

            if (subcommand === "give") {
                const target = Player.getById(Number(args[1]));
                if (!target) {
                    reply.line("No such player.");
                    return;
                }
                dog.giveTo(target);
                reply.line(`Dog ${dog.id} now belongs to ${target.nickname}.`);
                return;
            }

            dog.destroy();
            reply.line("Your dog is gone.");
            return;
        }

        if (player.dog) {
            reply.line("You already have a dog. `/dog info` reads it, `/dog remove` gets rid of it.");
            return;
        }

        if (!hasPose(player)) {
            reply.line(NO_POSE_YET);
            return;
        }

        const { position, rotation } = inFrontOf(player, SPAWN_DISTANCE);
        const dog = Dog.spawn(player, position, rotation, subcommand);
        reply.line(`Spawned dog ${dog.id} (soul ${dog.soul}). It is yours: your client runs it, everyone else watches it.`);
    },
};
