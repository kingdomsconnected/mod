import { readLevelGuid } from "../args.js";
import type { Command, CommandContext, Reply } from "../command.js";
import { NO_POSE_YET, distanceBetween, hasPose, inFrontOf, yawTowards } from "../place.js";

/** How far in front of the caller the container is put; close, to be opened straight away. */
const SPAWN_DISTANCE = 2.5;

/** How far from the caller `/stash near` and the verbs without a GUID look. */
const NEAR_DISTANCE = 5;

/** How far behind a container `/stash goto` stands the caller: inside the game's 2.5 m reach. */
const GOTO_DISTANCE = 1.3;

/** Containers `/stash spawn` builds by name, from models the level already uses for its own. */
const MODELS = new Map<string, string | undefined>([
    ["chest", undefined],
    ["cabinet", "objects/manmade/common_furniture/cabinets/cabinet_rustic_a.cgf"],
    ["fancycabinet", "objects/manmade/common_furniture/cabinets/cabinet_fancy_d.cgf"],
    ["smallchest", "objects/manmade/common_furniture/chests/chest-small-a.cgf"],
    ["royalchest", "objects/characters/assets/chest/chest_royal_a.cdf"],
]);

/** The game's own lockpick labels, by the lowest difficulty each one starts at. */
const DIFFICULTIES = new Map([
    ["veryeasy", 0.1],
    ["easy", 0.3],
    ["average", 0.5],
    ["hard", 0.7],
    ["veryhard", 0.9],
]);

/** One virtual stash per connection, kept across UI opens and world changes. */
const virtualStashes = new Map<number, Stash>();

export function installStashHandlers(): void {
    Events.on("playerDisconnect", (player) => {
        const stash = virtualStashes.get(player.id);
        virtualStashes.delete(player.id);
        stash?.destroy();
    });

    Events.on("stashDestroy", (stash) => {
        for (const [playerId, storage] of virtualStashes) {
            if (storage.id === stash.id) {
                virtualStashes.delete(playerId);
            }
        }
    });

    Events.on("resourceStop", (resourceName) => {
        if (resourceName !== "kcdc-gamemode") {
            return;
        }
        const stashes = [...virtualStashes.values()];
        virtualStashes.clear();
        for (const stash of stashes) {
            stash.destroy();
        }
    });
}

/** Stands the caller behind a container's use axis, looking along it, which is where the game offers it. */
function goTo(player: Player, stash: Stash, reply: Reply): void {
    const facing = stash.useDirection;
    const position = new Vector3(stash.position.x - facing.x * GOTO_DISTANCE, stash.position.y - facing.y * GOTO_DISTANCE, stash.position.z);
    if (!player.spawn(position, yawTowards(facing.x, facing.y))) {
        reply.line("Could not reach your client to move you.");
        return;
    }
    reply.line(`Moving you to ${describe(stash, position)}.`);
}

/** One line about a container: what it is, and how it is locked. */
function describe(stash: Stash, from: Vector3): string {
    const what = `${stash.name === "" ? "unnamed" : stash.name} guid=${stash.guid}${stash.levelGuid === "" ? " (spawned)" : ""}`;
    const lock = stash.locked ? `locked${stash.lockedByServer ? " (by the server)" : ""}` : "unlocked";
    const key = stash.keyItem === "" ? "no key" : `key ${stash.keyItem}`;
    const difficulty = stash.lockDifficulty === null ? "level difficulty" : `difficulty ${stash.lockDifficulty.toFixed(2)}`;
    return `${what} at ${distanceBetween(from, stash.position).toFixed(1)}m: ${lock}, ${key}, ${stash.lockpickable ? "lockpickable" : "not lockpickable"} (${difficulty}), ${stash.itemCount} stack(s)`;
}

/** The container a verb acts on: the one a GUID names, or the one nearest the caller. */
function target(player: Player, guidArg: string | undefined, reply: Reply): Stash | null {
    if (guidArg !== undefined) {
        const guid = readLevelGuid(guidArg);
        if (guid === null) {
            reply.line(`'${guidArg}' is not a container GUID. They are hexadecimal; /stash near prints one.`);
            return null;
        }
        const named = Stash.find(guid, player.virtualWorld);
        if (!named) {
            reply.line("No container has that GUID in this world.");
        }
        return named;
    }

    const nearest = Stash.nearest(player.position, NEAR_DISTANCE, player.virtualWorld);
    if (!nearest) {
        reply.line(`No container within ${NEAR_DISTANCE} m.`);
    }
    return nearest;
}

export const stashCommand: Command = {
    name: "stash",
    summary: "Opens virtual storage, spawns containers, and reads and locks the level's own.",
    usage: ["/stash", "/stash virtual", "/stash info", "/stash clear", "/stash near [guid]", "/stash lock|unlock [guid]", "/stash pick on|off [guid]", "/stash key <item guid>|clear [guid]", "/stash difficulty <0-1|veryeasy|easy|average|hard|veryhard|clear> [guid]", "/stash spawn [chest|cabinet|fancycabinet|smallchest|royalchest]", "/stash goto [locked|unlocked|guid]"],

    run({ player, args, reply, usage }: CommandContext): void {
        const subcommand = args[0];
        const world = player.virtualWorld;

        if (subcommand === "virtual") {
            let stash = virtualStashes.get(player.id);
            if (!stash || !Stash.getById(stash.id)) {
                stash = Stash.createVirtual(world);
                virtualStashes.set(player.id, stash);
            }
            stash.setVirtualWorld(world);
            if (!stash.open(player)) {
                reply.line("Your stash could not open. Wait until your inventory is ready and try again.");
            }
            return;
        }

        if (subcommand === "clear") {
            reply.line(`Despawned ${Stash.destroyAll(world)} stash(es).`);
            return;
        }

        if (subcommand === "info") {
            const stashes = Stash.all().filter((stash) => stash.virtualWorld === world);
            const virtual = stashes.filter((stash) => stash.isVirtual).length;
            const spawned = stashes.filter((stash) => !stash.isVirtual && stash.levelGuid === "").length;
            const held = stashes.filter((stash) => stash.holderId !== 0).length;
            reply.line(`${stashes.length} container(s) in this world, ${spawned} spawned chests, ${virtual} virtual stashes, ${held} open right now.`);
            return;
        }

        if (subcommand === "near" || subcommand === "lock" || subcommand === "unlock") {
            const stash = target(player, args[1], reply);
            if (!stash) {
                return;
            }
            if (subcommand !== "near") {
                stash.locked = subcommand === "lock";
            }
            reply.line(describe(stash, player.position));
            return;
        }

        if (subcommand === "goto") {
            const which = args[1] ?? "locked";
            if (which !== "locked" && which !== "unlocked") {
                const named = target(player, which, reply);
                if (named) {
                    goTo(player, named, reply);
                }
                return;
            }
            // The level's own, nearest first, in the state asked for.
            const wanted = which === "locked";
            let best: Stash | null = null;
            let bestDistance = Infinity;
            for (const stash of Stash.all()) {
                if (stash.virtualWorld !== world || stash.levelGuid === "" || stash.locked !== wanted) {
                    continue;
                }
                const distance = distanceBetween(player.position, stash.position);
                if (distance < bestDistance) {
                    bestDistance = distance;
                    best = stash;
                }
            }
            if (!best) {
                reply.line(`No ${which} container in this world.`);
                return;
            }
            goTo(player, best, reply);
            return;
        }

        if (subcommand === "pick") {
            const setting = args[1];
            if (setting !== "on" && setting !== "off") {
                usage();
                return;
            }
            const stash = target(player, args[2], reply);
            if (!stash) {
                return;
            }
            stash.lockpickable = setting === "on";
            reply.line(describe(stash, player.position));
            return;
        }

        if (subcommand === "difficulty") {
            const setting = args[1];
            const value = setting === undefined ? NaN : setting === "clear" ? null : (DIFFICULTIES.get(setting) ?? Number(setting));
            if (value !== null && !(value >= 0 && value <= 1)) {
                usage();
                return;
            }
            const stash = target(player, args[2], reply);
            if (!stash) {
                return;
            }
            stash.lockDifficulty = value;
            reply.line(describe(stash, player.position));
            return;
        }

        if (subcommand === "key") {
            const key = args[1];
            if (key === undefined) {
                usage();
                return;
            }
            const stash = target(player, args[2], reply);
            if (!stash) {
                return;
            }
            const wanted = key === "clear" ? "" : key.toLowerCase();
            stash.keyItem = wanted;
            // The server refuses a class it does not know and keeps the old key.
            if (wanted !== "" && stash.keyItem !== wanted) {
                reply.line(`'${key}' is not an item class GUID this server knows; the key is unchanged.`);
            }
            reply.line(describe(stash, player.position));
            return;
        }

        const kind = subcommand === "spawn" ? (args[1] ?? "chest") : undefined;
        if ((subcommand !== undefined && subcommand !== "spawn") || (kind !== undefined && !MODELS.has(kind))) {
            usage();
            return;
        }

        if (!hasPose(player)) {
            reply.line(NO_POSE_YET);
            return;
        }

        const { position, rotation } = inFrontOf(player, SPAWN_DISTANCE);
        const model = MODELS.get(kind ?? "chest");
        const stash = Stash.spawn(position, rotation, world, model === undefined ? undefined : { model });
        reply.line(`Spawned stash ${stash.id} guid=${stash.guid}. Put something in it with /give and a trip to the container; the other /stash verbs take its guid.`);
    },
};
