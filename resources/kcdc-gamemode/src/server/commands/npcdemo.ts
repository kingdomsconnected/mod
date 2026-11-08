import { spawnNpc } from "../npc-spawn.js";
import type { Command, CommandContext } from "../command.js";
import { NO_POSE_YET, hasPose, inFrontOf, yawTowards, distanceBetween } from "../place.js";

/**
 * Worked examples of the server-owned NPC API, one per subcommand.
 *
 * `/npc` is the verb-by-verb command: one NPC, one order, read it back. This is
 * the other half -- the arrangements those verbs are actually for, and the event
 * handlers that make them work.
 *
 * Route demos advance through npcIntentDone. The hunter additionally scans
 * nearby players twice a second to choose who it should attack.
 */

/** What this demo made, so `reset` can clear its own NPCs and nobody else's. */
const spawned = new Set<number>();

/** One hunter per caller; its target is retained until lost or out of range. */
const HUNTER_LEASH = 60;
const HUNTER_RETURN_RADIUS = 2;
const hunters = new Map<number, { id: number; target: number | null; home: Vector3; returning: boolean; retryAt: number; bow: boolean }>();
const copyPosition = (position: Vector3): Vector3 => new Vector3(position.x, position.y, position.z);

function spawnHunter({ player, args, reply, usage }: CommandContext): void {
    if (args.length > 2 || (args[1] !== undefined && args[1] !== "bow")) { usage(); return; }
    const bow = args[1] === "bow";
    const previous = hunters.get(player.id);
    if (previous) Npc.getById(previous.id)?.remove();
    const npc = track(spawnNpc({ soul: "bandit", outfit: "d5b503bb-b0f4-4f40-9ef1-52d7f9997c7d", ...inFrontOf(player, 8), name: bow ? "Bow hunter" : "Hunter", nametag: true, virtualWorld: player.virtualWorld }));
    // Keep the bandit clothes and equip either the sword or bow and arrows.
    if (!npc.wear([...npc.wearing, ...(bow ? ["5c23394a-3300-4570-a8b7-ef1c11519047", "710e3706-8974-404b-b23a-6f51670ef1ed"] : ["c164f346-0463-4116-b790-094b11274e5e"])])) {
        npc.remove();
        reply.line("The hunter's equipment was refused.");
        return;
    }
    hunters.set(player.id, { id: npc.id, target: null, home: copyPosition(npc.position), returning: false, retryAt: 0, bow });
    reply.line(`Hunter ${npc.id} seeks players within 30m and returns if it strays 60m from its last position outside combat. /npcdemo reset removes it.`);
}

function updateHunters(): void {
    const players = Player.all();
    for (const [owner, hunter] of hunters) {
        const npc = Npc.getById(hunter.id);
        if (!npc || !npc.alive) {
            hunters.delete(owner);
            continue;
        }
        // Keep the anchor fixed throughout pursuit and return. Otherwise a
        // moving hunter would drag its own leash along behind it.
        if (hunter.target !== null && distanceBetween(npc.position, hunter.home) > HUNTER_LEASH) {
            npc.hold();
            hunter.target = null;
            hunter.returning = true;
            hunter.retryAt = 0;
        }
        if (hunter.returning && hunter.target === null) {
            if (distanceBetween(npc.position, hunter.home) <= HUNTER_RETURN_RADIUS) {
                npc.hold();
                hunter.returning = false;
                hunter.home = copyPosition(npc.position);
            } else if (Date.now() >= hunter.retryAt && (npc.intent !== "moveTo" || npc.status === "failed" || npc.status === "blocked" || npc.status === "reached")) {
                npc.moveTo(hunter.home, { speed: "run", radius: HUNTER_RETURN_RADIUS });
                hunter.retryAt = Date.now() + 2000;
            }
        }
        const eligible = (player: Player, range: number): boolean => player.ready && player.canAct && player.virtualWorld === npc.virtualWorld && distanceBetween(player.position, npc.position) <= range && (!hunter.returning || (distanceBetween(npc.position, hunter.home) <= HUNTER_LEASH && distanceBetween(player.position, hunter.home) <= HUNTER_LEASH));
        const current = players.find((player) => player.id === hunter.target);
        if (current && eligible(current, 60) && npc.intent === "attack" && npc.status !== "failed" && npc.status !== "blocked") continue;
        if (hunter.target !== null) {
            npc.hold();
            hunter.target = null;
        }
        // No active fight: an idle move establishes a new return point.
        if (!hunter.returning) hunter.home = copyPosition(npc.position);
        const target = players.filter((player) => eligible(player, 30)).sort((a, b) => distanceBetween(a.position, npc.position) - distanceBetween(b.position, npc.position))[0];
        if (target && npc.attack(target, { weapon: hunter.bow ? "bow" : "melee" })) {
            hunter.target = target.id;
        }
    }
}

/** The guard post: two guards who react to each other being hit. */
interface GuardPost {
    readonly left: number;
    readonly right: number;
}
let post: GuardPost | null = null;

/** One player's escort, and whether it is following, standing, or walking back to rejoin them. */
interface Escort {
    id: number;
    state: "following" | "standing" | "catchingUp";
}

/** The escort each player has asked for, by player id. */
const escorts = new Map<number, Escort>();

/** The pinned two-actor scene, and how far through it we are. */
interface Scene {
    readonly actor: number;
    readonly partner: number;
    readonly home: Vector3;
    step: number;
}
let scene: Scene | null = null;

function track(npc: Npc): Npc {
    spawned.add(npc.id);
    return npc;
}

/** The NPC an id names, or null once it has gone. */
function resolve(id: number | undefined): Npc | null {
    return id === undefined ? null : Npc.getById(id);
}

/** A point on a circle around `centre`, on the ground plane. */
function onRing(centre: Vector3, radius: number, angle: number): Vector3 {
    return new Vector3(centre.x + Math.cos(angle) * radius, centre.y + Math.sin(angle) * radius, centre.z);
}

// --- The demos ---------------------------------------------------------------

/**
 * Two guards facing each other across a gateway.
 *
 * Shows the parts of an NPC that are not movement: a name, an outfit from the
 * game's own preset table by way of the role, a faction, and the interaction
 * flag that makes `npcInteract` fire. The pair is remembered so the handlers
 * below can make one react to the other.
 */
function guardPost({ player, reply }: CommandContext): void {
    const { position } = inFrontOf(player, 6);
    const left = track(
        spawnNpc({
            soul: "guard",
            position: new Vector3(position.x - 1.5, position.y, position.z),
            rotation: yawTowards(1, 0),
            name: "Guard Radim",
            faction: 3,
            interactable: true,
            nametag: true,
        }),
    );
    const right = track(
        spawnNpc({
            soul: "guard_alt",
            position: new Vector3(position.x + 1.5, position.y, position.z),
            rotation: yawTowards(-1, 0),
            name: "Guard Vaclav",
            faction: 3,
            interactable: true,
            nametag: true,
        }),
    );

    // They watch each other, which is what a post looks like from outside.
    left.lookAt(right);
    right.lookAt(left);

    post = { left: left.id, right: right.id };
    reply.line(`Guard post up: ${left.name} (${left.id}) and ${right.name} (${right.id}). Press T at one of them, or hit one and watch the other.`);
}

/**
 * A square patrol around the caller.
 *
 * The route lives on the server and only the leg being walked is ever
 * replicated, so this is also the demo for dormancy: walk away and the guard
 * keeps stepping its square on the server's own reckoning, and the client that
 * comes back finds it where it should be rather than where it stopped.
 */
function patrolSquare({ player, args, reply }: CommandContext): void {
    const size = Math.min(Math.max(Number(args[1] ?? 10) || 10, 4), 60);
    const centre = player.position;
    const corners = [onRing(centre, size, 0), onRing(centre, size, Math.PI / 2), onRing(centre, size, Math.PI), onRing(centre, size, (3 * Math.PI) / 2)];

    const walker = track(
        spawnNpc({
            soul: "guard",
            position: corners[0] ?? centre,
            rotation: yawTowards(0, 1),
            name: "Watchman",
            interactable: true,
            nametag: true,
        }),
    );

    // `waitSeconds` is the pause at each corner, which is what makes a patrol
    // read as a patrol rather than a lap.
    const accepted = walker.patrol(corners, { loop: true, speed: "walk", waitSeconds: 4 });
    reply.line(accepted ? `Watchman ${walker.id} is walking a ${size}m square. Walk off and come back -- it keeps its place.` : "The route was refused.");
}

/**
 * One NPC walked by the engine instead of by the server.
 *
 * `native` locomotion gives the engine's movement controller a target without
 * obstacle avoidance. The simulating client checks whether the NPC moves and
 * logs the result. If it makes no progress for three seconds, the client falls
 * back to kinematic movement so the NPC still walks.
 */
function nativeWalk({ player, reply }: CommandContext): void {
    const start = inFrontOf(player, 4).position;
    const target = inFrontOf(player, 24).position;

    const walker = track(
        spawnNpc({
            soul: "guard",
            position: start,
            rotation: yawTowards(target.x - start.x, target.y - start.y),
            name: "Native Walker",
            locomotion: "native",
            nametag: true,
        }),
    );

    const accepted = walker.moveTo(target, { speed: "walk", radius: 1.5 });
    reply.line(accepted ? `Walker ${walker.id} was told to walk 20m on native locomotion. The client log says whether the engine moved it or it fell back.` : "The move was refused.");
}

/** Removes the player's escort; false when they had none left to remove. */
function dismissEscort(playerId: number): boolean {
    const guard = resolve(escorts.get(playerId)?.id);
    escorts.delete(playerId);
    if (!guard) {
        return false;
    }
    guard.remove();
    return true;
}

/** Follow tracks the player's position. Asking again dismisses the escort. */
function escort({ player, reply }: CommandContext): void {
    if (dismissEscort(player.id)) {
        reply.line("Your escort has been dismissed.");
        return;
    }
    if (!hasPose(player)) {
        reply.line(NO_POSE_YET);
        return;
    }

    const { position, rotation } = inFrontOf(player, 2);
    const guard = track(
        spawnNpc({
            soul: "knight",
            position,
            rotation,
            name: `${player.nickname}'s escort`,
            interactable: true,
            nametag: true,
            // The game handles movement, without guaranteeing a route around
            // obstacles.
            locomotion: "native",
        }),
    );
    guard.follow(player, { speed: "jog", radius: 3 });
    escorts.set(player.id, { id: guard.id, state: "following" });
    reply.line(`${guard.name} (${guard.id}) is with you. /npcdemo escort again to dismiss.`);
}

/** Stop the escort, or route it back before resuming its follow order. */
function stand({ player, args, reply, usage }: CommandContext): void {
    const pathfinding = args[1] ?? "auto";
    if (args.length > 2 || (pathfinding !== "auto" && pathfinding !== "game" && pathfinding !== "server")) {
        usage();
        return;
    }
    const escort = escorts.get(player.id);
    const npc = resolve(escort?.id);
    if (!escort || !npc) {
        reply.line("You don't have an escort. Use /npcdemo escort to spawn one.");
        return;
    }
    if (!npc.alive) {
        reply.line("Your escort is dead. Use /npcdemo escort to dismiss it.");
        return;
    }
    if (escort.state !== "standing") {
        npc.hold();
        escort.state = "standing";
        console.log(`escort ${npc.id}: stand at (${npc.position.x.toFixed(2)}, ${npc.position.y.toFixed(2)}, ${npc.position.z.toFixed(2)})`);
        reply.line("Your escort is standing. Use /npcdemo stand [auto|game|server] to call it back.");
        return;
    }
    if (!hasPose(player)) {
        reply.line(NO_POSE_YET);
        return;
    }
    const goal = player.position;
    if (!npc.moveTo(goal, { speed: "jog", radius: 3, pathfinding })) {
        reply.line("Your escort could not start moving. It is still standing.");
        return;
    }
    escort.state = "catchingUp";
    console.log(`escort ${npc.id}: resume ${pathfinding}, simulator ${npc.simulator?.id ?? "none"}, goal (${goal.x.toFixed(2)}, ${goal.y.toFixed(2)}, ${goal.z.toFixed(2)})`);
    reply.line(`Asked your escort to return using ${pathfinding} pathfinding. It will follow you after arriving.`);
}

/**
 * A ring of townsfolk, which is the demo for the budget.
 *
 * Every body a client is elected to simulate costs it an AI LOD slot, and the
 * server caps how many one client may take -- so past that cap the overflow
 * goes to the next nearest player, or stays dormant until somebody is near it.
 * `/npcdemo stats` is how that is read back.
 */
function crowd({ player, args, reply }: CommandContext): void {
    const count = Math.min(Math.max(Number(args[1] ?? 8) || 8, 1), 32);
    const roles = Npc.roles();
    const centre = player.position;

    for (let i = 0; i < count; i += 1) {
        const angle = (i / count) * Math.PI * 2;
        const position = onRing(centre, 8, angle);
        // Vary the roles while keeping the default native movement.
        const role = roles[i % roles.length] ?? "townsman";
        const npc = track(
            spawnNpc({
                soul: role,
                position,
                // Facing inwards, towards the caller.
                rotation: yawTowards(centre.x - position.x, centre.y - position.y),
                name: `${role} ${i + 1}`,
                interactable: true,
                nametag: true,
            }),
        );
        // Half of them mill about; the rest stand and watch.
        if (i % 3 === 0) {
            npc.moveTo(onRing(centre, 14, angle + 0.6), { speed: "walk", radius: 2 });
        }
        else {
            npc.lookAt(player);
        }
    }

    reply.line(`Spawned ${count} townsfolk in a ring. /npcdemo stats says who is running them.`);
}

/**
 * A pinned two-actor scene: one NPC walks over to another and they talk.
 *
 * This is what `pin` is for. A scripted scene has to be run by the machine it
 * is being played to, or the actors migrate to whoever happens to be nearer and
 * the timing of it belongs to a player who is not watching. Both actors are
 * pinned to the caller for the duration and unpinned when it ends.
 *
 * The steps are chosen in `npcIntentDone` below, not here: the server does not
 * know how long a walk takes, and the client running the body is the only peer
 * that does.
 */
function startScene({ player, reply }: CommandContext): void {
    if (scene) {
        reply.line("A scene is already running. /npcdemo reset clears it.");
        return;
    }

    const { position: stage } = inFrontOf(player, 7);
    const actor = track(
        spawnNpc({
            soul: "townsman",
            position: new Vector3(stage.x - 4, stage.y, stage.z),
            rotation: yawTowards(1, 0),
            name: "Vendel",
            nametag: true,
        }),
    );
    const partner = track(
        spawnNpc({
            soul: "townswoman",
            position: new Vector3(stage.x + 4, stage.y, stage.z),
            rotation: yawTowards(-1, 0),
            name: "Marketa",
            nametag: true,
        }),
    );

    actor.pin(player);
    partner.pin(player);

    scene = { actor: actor.id, partner: partner.id, home: actor.position, step: 0 };
    partner.lookAt(actor);
    actor.moveTo(partner.position, { speed: "walk", radius: 2.5 });
    reply.line(`Scene running: ${actor.name} is walking over to ${partner.name}. Both are pinned to your client.`);
}

/** One step of the scene, chosen by what the simulator just reported. */
function advanceScene(status: string): void {
    if (!scene) {
        return;
    }
    const actor = resolve(scene.actor);
    const partner = resolve(scene.partner);
    if (!actor || !partner) {
        // One of them was removed under us; the scene is over.
        scene = null;
        return;
    }
    if (status === "failed") {
        console.log(`scene: ${actor.name} could not carry out step ${scene.step}; ending it`);
        endScene();
        return;
    }

    // One intent per step, and never two: an intent is held state, so a second
    // order in the same step simply replaces the first and the step reports on
    // something nobody asked for. Speech is not an intent -- it is a one-shot --
    // so a line can go out alongside whichever order the step does issue.
    switch (scene.step) {
        case 0:
            // Arrived beside her. Both speak, and the actor holds a look, which
            // is what brings us back here for the next step.
            scene.step = 1;
            actor.say("Marketa. They are saying the road past the mill is not safe after dark.");
            partner.say("Then do not walk it after dark, Vendel.");
            actor.lookAt(partner);
            return;

        case 1:
            scene.step = 2;
            actor.say("As you say. Good night to you.");
            actor.moveTo(scene.home, { speed: "walk", radius: 2 });
            return;

        case 2:
            // Home again: the scene is over and both actors go back to the
            // election.
            partner.lookAt(actor);
            endScene();
            return;

        default:
            return;
    }
}

function endScene(): void {
    if (!scene) {
        return;
    }
    // Unpinned rather than left pinned: the scene is over, and from here the
    // election should be free to give them to whoever is nearest.
    resolve(scene.actor)?.pin(null);
    resolve(scene.partner)?.pin(null);
    scene = null;
}

/** Who is simulating what, which is the one thing about an NPC nothing else shows. */
function stats({ reply }: CommandContext): void {
    const all = Npc.all();
    if (all.length === 0) {
        reply.line("No NPCs.");
        return;
    }

    const byClient = new Map<string, number>();
    let dormant = 0;
    for (const npc of all) {
        const simulator = npc.simulator;
        if (!simulator) {
            dormant += 1;
            continue;
        }
        const key = `${simulator.nickname} (${simulator.id})`;
        byClient.set(key, (byClient.get(key) ?? 0) + 1);
    }

    reply.line(`${all.length} NPC(s): ${all.length - dormant} simulated, ${dormant} dormant.`);
    for (const [client, count] of byClient) {
        reply.line(`  ${client} is running ${count}.`);
    }
    if (dormant > 0) {
        reply.line("  Dormant is not broken: nobody is near enough for it to matter, and authored routes keep advancing.");
    }
}

/** Clears what this demo made, and nothing else. */
function reset({ reply }: CommandContext): void {
    endScene();
    let removed = 0;
    for (const id of [...spawned]) {
        const npc = Npc.getById(id);
        if (npc) {
            npc.remove();
            removed += 1;
        }
        spawned.delete(id);
    }
    post = null;
    escorts.clear();
    hunters.clear();
    reply.line(`Removed ${removed} NPC(s) this demo had spawned. /npc clear removes every NPC on the server.`);
}

// --- The handlers the demos run on -------------------------------------------

export function installNpcDemoHandlers(): void {
    const hunterTimer = setInterval(updateHunters, 500);
    Events.on("resourceStop", (name) => {
        if (name !== "kcdc-gamemode") return;
        clearInterval(hunterTimer);
        for (const hunter of hunters.values()) Npc.getById(hunter.id)?.remove();
        hunters.clear();
    });
    Events.on("npcIntentDone", (npc, status) => {
        if (!spawned.has(npc.id)) {
            return; // Another resource's NPC. Leaving it alone is the whole of sharing the API.
        }
        console.log(`npcIntentDone: ${npc.name || npc.id} (${npc.id}) finished '${npc.intent}' -- ${status} at (${npc.position.x.toFixed(2)}, ${npc.position.y.toFixed(2)}, ${npc.position.z.toFixed(2)})`);

        if (scene && npc.id === scene.actor) {
            advanceScene(status);
            return;
        }

        const owner = Player.all().find((player) => escorts.get(player.id)?.id === npc.id);
        const escort = owner ? escorts.get(owner.id) : undefined;
        if (!owner || !escort) {
            return;
        }
        if (escort.state === "catchingUp" && npc.intent === "moveTo") {
            escort.state = status === "reached" ? "following" : "standing";
            if (status === "reached") {
                npc.follow(owner, { speed: "jog", radius: 3 });
            } else {
                npc.hold();
                Chat.sendToPlayer(owner, `Your escort's route ended with ${status}. Use /npcdemo stand [auto|game|server] to try again.`);
            }
        } else if (escort.state === "following" && (status === "blocked" || status === "failed") && npc.intent === "follow") {
            escort.state = "standing";
            npc.hold();
            Chat.sendToPlayer(owner, `Your escort couldn't reach you (${status}). Use /npcdemo stand [auto|game|server] to try again.`);
        }
    });

    Events.on("npcInteract", (npc, player) => {
        if (!spawned.has(npc.id)) {
            return;
        }
        // Turning to face whoever spoke is most of what makes an NPC feel
        // addressed; the line is the rest.
        npc.lookAt(player);
        npc.say(`Well met, ${player.nickname}.`);
    });

    Events.on("npcDamage", (npc, attacker, amount) => {
        if (!spawned.has(npc.id)) {
            return;
        }
        console.log(`npcDamage: ${npc.name || npc.id} took ${amount.toFixed(0)} from ${attacker ? ("nickname" in attacker ? attacker.nickname : attacker.name) : "nobody"} (${npc.health.toFixed(0)}/${npc.maxHealth.toFixed(0)})`);

        // The other guard at the post runs for it. The reaction is the server's
        // to decide -- nothing in the game is going to do it for a body whose
        // AI has been cut loose.
        if (!post || !attacker) {
            return;
        }
        const other = npc.id === post.left ? resolve(post.right) : npc.id === post.right ? resolve(post.left) : null;
        other?.flee(attacker.position, { speed: "run", radius: 30 });
    });

    Events.on("npcDeath", (npc, attacker) => {
        if (spawned.has(npc.id)) {
            console.log(`npcDeath: ${npc.name || npc.id} killed by ${attacker ? ("nickname" in attacker ? attacker.nickname : attacker.name) : "nobody"}. The corpse stays: it is still an entity.`);
        }
    });

    Events.on("npcSimulatorChange", (npc, player) => {
        if (spawned.has(npc.id)) {
            console.log(`npcSimulatorChange: ${npc.name || npc.id} is now run by ${player ? player.nickname : "nobody -- dormant"}`);
        }
    });

    Events.on("npcDestroy", (npc) => {
        spawned.delete(npc.id);
        for (const [playerId, escort] of escorts) {
            if (escort.id === npc.id) {
                escorts.delete(playerId);
            }
        }
        if (scene && (npc.id === scene.actor || npc.id === scene.partner)) {
            scene = null;
        }
    });

    Events.on("playerDisconnect", (player) => {
        dismissEscort(player.id);
        const hunter = hunters.get(player.id);
        if (hunter) Npc.getById(hunter.id)?.remove();
        hunters.delete(player.id);
    });
}

export const npcDemoCommand: Command = {
    name: "npcdemo",
    summary: "Worked NPC arrangements: an experimental hunter, a guard post, a patrol, an escort, a crowd, a scripted scene and a native-locomotion trial.",
    usage: [
        "/npcdemo hunter [bow]",
        "/npcdemo guardpost",
        "/npcdemo square [size]",
        "/npcdemo escort",
        "/npcdemo stand [auto|game|server]",
        "/npcdemo crowd [count]",
        "/npcdemo scene",
        "/npcdemo native",
        "/npcdemo stats",
        "/npcdemo reset",
    ],

    run(context: CommandContext): void {
        const { args, reply, usage } = context;
        const subcommand = args[0];

        // These demos place NPCs immediately. The escort checks its pose only
        // when spawning, so dismissing one also works before the player is ready.
        const places = subcommand === "hunter" || subcommand === "guardpost" || subcommand === "square" || subcommand === "crowd" || subcommand === "scene" || subcommand === "native";
        if (places && !hasPose(context.player)) {
            reply.line(NO_POSE_YET);
            return;
        }

        switch (subcommand) {
            case "hunter":
                spawnHunter(context);
                return;
            case "guardpost":
                guardPost(context);
                return;
            case "square":
                patrolSquare(context);
                return;
            case "escort":
                escort(context);
                return;
            case "stand":
                stand(context);
                return;
            case "crowd":
                crowd(context);
                return;
            case "scene":
                startScene(context);
                return;
            case "native":
                nativeWalk(context);
                return;
            case "stats":
                stats(context);
                return;
            case "reset":
                reset(context);
                return;
            default:
                usage();
                return;
        }
    },
};
