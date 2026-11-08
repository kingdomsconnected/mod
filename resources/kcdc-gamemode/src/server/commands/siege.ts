import { readEntityId, readNumber } from "../args.js";
import { beginLadder, isBuilding } from "../build.js";
import type { Command, CommandContext, Reply } from "../command.js";
import { NO_POSE_YET, distanceBetween, formatPosition, groundForward, hasPose, yawTowards } from "../place.js";

/** How far in front of the caller an engine is built: clear of the trebuchet's arm. */
const SPAWN_DISTANCE = 14;

/** How far in front of the caller a stone pile is put: inside the 0.3-1.8 m the game offers its grab from. */
const PILE_DISTANCE = 1.2;

/** How far a bare `/siege load` or `/siege fire` looks for the engine meant. */
const REACH = 40;

/** How far a bare `/siege load` or `/siege fire` looks for an engine that can do what was asked before settling for the nearest one. */
const PREFERRED_REACH = 200;

/** How far a bare ladder verb looks for the ladder meant. */
const LADDER_REACH = 25;

/** How many lines `/siege list` fills the caller's chat with. */
const LIST_LIMIT = 20;

/** Newest first, which is what somebody who has just built things wants. */
function inWorld(virtualWorld: number): SiegeEngine[] {
    return SiegeEngine.all(virtualWorld).sort((left, right) => right.id - left.id);
}

/** What `pick` needs to know about the kind of thing a line names. */
interface PickKind<T extends { readonly virtualWorld: number; readonly position: Vector3 }> {
    getById(id: number): T | null | undefined;
    all(virtualWorld: number): T[];
    reach: number;
    unknownId: (raw: string) => string;
    noneInReach: string;
}

/**
 * The thing a line names by id, or the nearest one within reach; null after saying why there is none.
 * Without an id, `prefer` picks among those within `preferredReach` first.
 */
function pick<T extends { readonly virtualWorld: number; readonly position: Vector3 }>(kind: PickKind<T>, raw: string | undefined, player: Player, reply: Reply, prefer?: (candidate: T) => boolean, preferredReach = kind.reach): T | null {
    if (raw !== undefined) {
        const id = readEntityId(raw);
        const found = id === null ? null : kind.getById(id);
        if (!found || found.virtualWorld !== player.virtualWorld) {
            reply.line(kind.unknownId(raw));
            return null;
        }
        return found;
    }

    const candidates = kind.all(player.virtualWorld);
    const inReach = candidates.filter((candidate) => distanceBetween(candidate.position, player.position) <= kind.reach);
    const preferred = prefer ? candidates.filter((candidate) => prefer(candidate) && distanceBetween(candidate.position, player.position) <= preferredReach) : [];
    let nearest: T | null = null;
    let nearestDistance = Infinity;
    for (const candidate of preferred.length > 0 ? preferred : inReach) {
        const distance = distanceBetween(candidate.position, player.position);
        if (distance < nearestDistance) {
            nearestDistance = distance;
            nearest = candidate;
        }
    }
    if (!nearest) {
        reply.line(kind.noneInReach);
    }
    return nearest;
}

const ENGINES: PickKind<SiegeEngine> = {
    getById: (id) => SiegeEngine.getById(id),
    all: inWorld,
    reach: REACH,
    unknownId: (raw) => `No siege engine in this world has id ${raw}. /siege list prints the ids.`,
    noneInReach: `No siege engine within ${REACH}m. /siege trebuchet or /siege cannon builds one.`,
};

const LADDERS: PickKind<SiegeLadder> = {
    getById: (id) => SiegeLadder.getById(id),
    all: (virtualWorld) => SiegeLadder.all(virtualWorld),
    reach: LADDER_REACH,
    unknownId: (raw) => `No siege ladder in this world has id ${raw}.`,
    noneInReach: `No siege ladder within ${LADDER_REACH}m. /siege ladder [siege|tall|short|low] stands one against the wall in front of you.`,
};

/**
 * The engine a line names by id, or the nearest one within reach; null after saying why there is none.
 * Without an id, `prefer` picks among the engines in reach first -- `fire` wants a loaded one, `load` an idle one --
 * so a cannon standing a step closer does not take the order meant for the trebuchet behind it.
 * A loaded engine a little further off still beats an unloaded one at hand.
 */
function target(raw: string | undefined, player: Player, reply: Reply, prefer?: (engine: SiegeEngine) => boolean): SiegeEngine | null {
    return pick(ENGINES, raw, player, reply, prefer, PREFERRED_REACH);
}

/** The ladder a line names by id, or the nearest one within reach; null after saying why there is none. */
function targetLadder(raw: string | undefined, player: Player, reply: Reply): SiegeLadder | null {
    return pick(LADDERS, raw, player, reply);
}

/** The navigation mesh's floor under a point, or `fallback` off the mesh: the server has no terrain of its own. */
function floorUnder(x: number, y: number, fallback: number): number {
    return Navigation.floorAt(new Vector3(x, y, fallback), { searchRadius: 4, searchHeight: 60 }) ?? fallback;
}

function describe(engine: SiegeEngine): string {
    return `${engine.kind} ${engine.id}`;
}

export const siegeCommand: Command = {
    name: "siege",
    summary: "Builds, loads and fires trebuchets and cannons.",
    usage: [
        "/siege trebuchet|cannon",
        "/siege load [id]",
        "/siege fire [distance] [id]",
        "/siege fire at <player id> [id]",
        "/siege speed <multiplier> [id]",
        "/siege health <hit points> [id]",
        "/siege ammo <shots> [id]",
        "/siege crew <winches> [id]",
        "/siege repair [id]",
        "/siege guide on|off [id]",
        "/siege ladder [siege|tall|short|low]",
        "/siege ladder push|raise|break|repair [id]",
        "/siege stones [count]",
        "/siege list",
        "/siege remove <id>",
        "/siege clear",
    ],

    run({ player, args, reply, usage }: CommandContext): void {
        const subcommand = args[0];
        const world = player.virtualWorld;

        if (subcommand === "list") {
            const engines = inWorld(world);
            if (engines.length === 0) {
                reply.line("No siege engine has been built in this world yet.");
                return;
            }
            const shown = engines.slice(0, LIST_LIMIT);
            reply.line(`${shown.length} of ${engines.length} engine(s), newest first:`);
            reply.lines(
                shown.map((engine) => {
                    const health = engine.maxHealth > 0 ? `, ${Math.round(engine.health)}/${engine.maxHealth} hp` : "";
                    const ammo = engine.ammo >= 0 ? `, ${engine.ammo} shot(s)` : "";
                    const operator = engine.operator ? `, worked by ${engine.operator.nickname}` : "";
                    return `  ${engine.id}: ${engine.kind} ${engine.cycle} at ${formatPosition(engine.position)} (x${engine.speed.toFixed(1)}${health}${ammo}${operator})`;
                }),
            );
            return;
        }

        if (subcommand === "clear") {
            reply.line(`Removed ${SiegeEngine.destroyAll(world)} siege engine(s), ${SiegeLadder.destroyAll(world)} ladder(s), and ${StonePile.destroyAll(world)} stone pile(s).`);
            return;
        }

        if (subcommand === "ladder") {
            const verb = args[1];
            if (verb === "push" || verb === "raise" || verb === "break" || verb === "repair") {
                const ladder = targetLadder(args[2], player, reply);
                if (!ladder) {
                    return;
                }
                if (verb === "break" || verb === "repair") {
                    if (verb === "break") {
                        ladder.break();
                    }
                    else {
                        ladder.repair();
                    }
                    reply.line(`Ladder ${ladder.id} is ${ladder.pose}.`);
                    return;
                }
                const result = verb === "push" ? ladder.push(player) : ladder.raise(player);
                reply.line(result.accepted ? `Ladder ${ladder.id}: ${verb} (${result.seconds.toFixed(1)}s).` : `Ladder ${ladder.id} ${result.reason}.`);
                return;
            }
            if (!hasPose(player)) {
                reply.line(NO_POSE_YET);
                return;
            }
            const kind = verb ?? "siege";
            if (isBuilding(player)) {
                reply.line("You are already placing something. Right-click to stop first.");
                return;
            }
            if (!beginLadder(player, kind)) {
                reply.line(`${kind} is not a siege ladder; expected siege, tall, short or low.`);
                return;
            }
            reply.line(`Placing a ${kind} ladder: aim at the foot of the wall and click. Right-click to cancel.`);
            return;
        }

        if (subcommand === "stones") {
            if (!hasPose(player)) {
                reply.line(NO_POSE_YET);
                return;
            }
            const read = args[1] === undefined ? -1 : readNumber(args[1]);
            if (read === null) {
                reply.line("Usage: /siege stones [count], -1 for no limit.");
                return;
            }
            // In front of the caller, not under them: its stones would hold
            // them fast, and the game offers the grab from behind the pile.
            const ahead = groundForward(player.rotation as Quaternion);
            const from = player.position;
            const x = from.x + ahead.x * PILE_DISTANCE;
            const y = from.y + ahead.y * PILE_DISTANCE;
            const pile = StonePile.spawn(new Vector3(x, y, floorUnder(x, y, from.z)), yawTowards(ahead.x, ahead.y), world);
            // Any negative count means no limit, which the pile spells as -1.
            pile.stones = read < 0 ? -1 : Math.trunc(read);
            reply.line(`Put stone pile ${pile.id} in front of you, throwing the way you face (${pile.stones < 0 ? "no limit" : `${pile.stones} stone(s)`}). Use it from where you stand.`);
            return;
        }

        if (subcommand === "remove") {
            const engine = args[1] === undefined ? null : target(args[1], player, reply);
            if (!engine) {
                if (args[1] === undefined) {
                    reply.line("Usage: /siege remove <id>. /siege list prints the ids.");
                }
                return;
            }
            engine.destroy();
            reply.line(`Removed ${describe(engine)}.`);
            return;
        }

        if (subcommand === "trebuchet" || subcommand === "cannon") {
            if (!hasPose(player)) {
                reply.line(NO_POSE_YET);
                return;
            }
            // In front of the caller and facing the same way, so the first
            // shot goes where they are looking.
            const ahead = groundForward(player.rotation as Quaternion);
            const from = player.position;
            const x = from.x + ahead.x * SPAWN_DISTANCE;
            const y = from.y + ahead.y * SPAWN_DISTANCE;
            const position = new Vector3(x, y, floorUnder(x, y, from.z));
            const engine = SiegeEngine.spawn(subcommand, position, yawTowards(ahead.x, ahead.y), world);
            // Anyone can work it with the game's own prompt: walk up and take its place.
            engine.usable = true;
            reply.line(`Built ${describe(engine)} at ${formatPosition(position)}. Walk up to it and take its place: hold block to aim, torch to load, attack to fire. Or /siege load and /siege fire.`);
            return;
        }

        if (subcommand === "health" || subcommand === "ammo" || subcommand === "crew") {
            const value = readNumber(args[1]);
            if (value === null) {
                reply.line(`Usage: /siege ${subcommand} <${subcommand === "health" ? "hit points, 0 for unharmable" : subcommand === "ammo" ? "shots, -1 for no limit" : "winches to man, 0 to 2"}> [id].`);
                return;
            }
            const engine = target(args[2], player, reply);
            if (!engine) {
                return;
            }
            if (subcommand === "health") {
                engine.maxHealth = value;
                reply.line(engine.maxHealth > 0 ? `${describe(engine)} can take ${engine.maxHealth} before it is wrecked.` : `Nothing harms ${describe(engine)}.`);
            }
            else if (subcommand === "ammo") {
                engine.ammo = Math.trunc(value);
                reply.line(engine.ammo < 0 ? `${describe(engine)} never runs out.` : `${describe(engine)} has ${engine.ammo} shot(s) left.`);
            }
            else {
                engine.crewRequired = Math.max(0, Math.trunc(value));
                reply.line(engine.crewPlaces === 0 ? `${describe(engine)} has no winches.` : `${describe(engine)} needs ${engine.crewRequired} of its ${engine.crewPlaces} winch(es) manned to wind.`);
            }
            return;
        }

        if (subcommand === "guide") {
            if (args[1] !== "on" && args[1] !== "off") {
                reply.line("Usage: /siege guide on|off [id]: the red beam showing the operator where a shot lands.");
                return;
            }
            const engine = target(args[2], player, reply);
            if (!engine) {
                return;
            }
            engine.setAimGuide(args[1] === "on");
            reply.line(`${describe(engine)}'s aim guide is ${engine.getAimGuide() ? "on" : "off"}.`);
            return;
        }

        if (subcommand === "repair") {
            const engine = target(args[1], player, reply);
            if (!engine) {
                return;
            }
            engine.repair();
            reply.line(`${describe(engine)} is whole again.`);
            return;
        }

        if (subcommand === "load") {
            const engine = target(args[1], player, reply, (candidate) => candidate.cycle === "idle");
            if (!engine) {
                return;
            }
            const result = engine.load();
            reply.line(result.accepted ? `${describe(engine)} is loading (${result.seconds.toFixed(1)}s).` : `${describe(engine)} ${result.reason}.`);
            return;
        }

        if (subcommand === "speed") {
            const multiplier = readNumber(args[1]);
            if (multiplier === null || multiplier < 0.1 || multiplier > 20) {
                reply.line("Usage: /siege speed <multiplier> [id], from 0.1 to 20; 1 is the game's own pace.");
                return;
            }
            const engine = target(args[2], player, reply, (candidate) => candidate.loaded);
            if (!engine) {
                return;
            }
            engine.speed = multiplier;
            reply.line(`${describe(engine)} now runs at x${engine.speed.toFixed(1)}; a load takes ${engine.loadDuration.toFixed(1)}s.`);
            return;
        }

        if (subcommand === "fire") {
            if (!hasPose(player)) {
                reply.line(NO_POSE_YET);
                return;
            }

            let aimed: Vector3;
            let engine: SiegeEngine | null;
            if (args[1] === "at") {
                const victimId = readEntityId(args[2]);
                const victim = victimId === null ? null : Player.getById(victimId);
                if (!victim) {
                    reply.line("Usage: /siege fire at <player id> [id].");
                    return;
                }
                engine = target(args[3], player, reply, (candidate) => candidate.loaded);
                aimed = victim.position;
            }
            else {
                const distance = args[1] === undefined ? null : readNumber(args[1]);
                if (args[1] !== undefined && distance === null) {
                    reply.line(`'${args[1]}' is not a distance in metres.`);
                    return;
                }
                engine = target(args[2], player, reply, (candidate) => candidate.loaded);
                if (!engine) {
                    return;
                }
                // Along the caller's facing, from the engine, onto the floor
                // there; the stone still comes down wherever the world stops it.
                const ahead = groundForward(player.rotation as Quaternion);
                const range = distance ?? engine.range;
                const x = engine.position.x + ahead.x * range;
                const y = engine.position.y + ahead.y * range;
                aimed = new Vector3(x, y, floorUnder(x, y, engine.position.z));
            }
            if (!engine) {
                return;
            }

            const result = engine.fire(aimed, player);
            reply.line(result.accepted ? `${describe(engine)} fires at ${formatPosition(aimed)}; the shot leaves in ${result.seconds.toFixed(1)}s.` : `${describe(engine)} ${result.reason}.`);
            return;
        }

        usage();
    },
};

/** Says where each shot came down, to whoever fired it. */
export function installSiegeHandlers(): void {
    Events.on("siegeImpact", (engine, position, attacker) => {
        if (attacker) {
            Chat.sendToPlayer(attacker, `${engine ? describe(engine) : "A siege engine"}'s shot came down at ${formatPosition(position)}.`);
        }
    });
    Events.on("siegeEngineReady", (engine) => {
        console.log(`siegeEngineReady: ${describe(engine)} is loaded`);
    });
    Events.on("siegeEngineWreck", (engine, attacker) => {
        if (attacker) {
            Chat.sendToPlayer(attacker, `You wrecked ${describe(engine)}.`);
        }
    });
}

