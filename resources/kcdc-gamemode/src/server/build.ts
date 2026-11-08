/**
 * The server half of `/build`: it asks a client to enter placement mode, and
 * answers what that client confirms.
 *
 * The client draws and colours the preview because it has the camera and the
 * world; the server decides what gets built because it has authority. A
 * client that saw green is making a request, not a decision.
 */

import { distanceBetween } from "./place.js";

const RESOURCE = "kcdc-gamemode";

/** Told to the client when a session should start. */
export const BEGIN_EVENT = `${RESOURCE}:build.begin`;

/** Told to the client when it should leave the mode. */
export const STOP_EVENT = `${RESOURCE}:build.stop`;

/** What a client sends back on a confirm. */
export const PLACE_EVENT = `${RESOURCE}:build.place`;

/**
 * How far from the player a confirmed spot may land. Not the client's
 * `maxDistance` restated -- the same question asked where the answer counts.
 */
const MAX_REACH = 60;

/** What the caller is currently placing, if anything. Keyed by player id. */
interface Session {
    model: string;
    scale: number;
    physics: string;
    placed: number;
    /** Set when the session places a siege ladder of this kind rather than a prop. */
    ladder?: string;
}

const sessions = new Map<number, Session>();

function readField(payload: unknown, key: string): unknown {
    return typeof payload === "object" && payload !== null ? (payload as Record<string, unknown>)[key] : undefined;
}

function readVector(payload: unknown, key: string): Vector3 | null {
    const value = readField(payload, key);
    if (typeof value !== "object" || value === null) {
        return null;
    }
    const source = value as Record<string, unknown>;
    const x = source.x;
    const y = source.y;
    const z = source.z;
    if (typeof x !== "number" || typeof y !== "number" || typeof z !== "number" || !Number.isFinite(x) || !Number.isFinite(y) || !Number.isFinite(z)) {
        return null;
    }
    return new Vector3(x, y, z);
}

function readQuaternion(payload: unknown, key: string): Quaternion | null {
    const value = readField(payload, key);
    if (typeof value !== "object" || value === null) {
        return null;
    }
    const source = value as Record<string, unknown>;
    const w = source.w;
    const x = source.x;
    const y = source.y;
    const z = source.z;
    if (typeof w !== "number" || typeof x !== "number" || typeof y !== "number" || typeof z !== "number") {
        return null;
    }
    if (!Number.isFinite(w) || !Number.isFinite(x) || !Number.isFinite(y) || !Number.isFinite(z)) {
        return null;
    }
    return new Quaternion(w, x, y, z);
}

/** Starts a session for this player and tells their client to open the preview. */
export function beginBuild(player: Player, model: string, scale: number, physics: string): void {
    sessions.set(player.id, { model, scale, physics, placed: 0 });
    player.emit(BEGIN_EVENT, JSON.stringify({ model, scale }));
}

/**
 * Starts a session that stands one siege ladder: the preview is the ladder's
 * own mesh leaning as it will lean, facing away from the player, which is the
 * way to the wall in front of them.
 */
export function beginLadder(player: Player, kind: string): boolean {
    const description = SiegeLadder.describe(kind);
    if (!description) {
        return false;
    }
    sessions.set(player.id, { model: description.model, scale: 1, physics: "static", placed: 0, ladder: kind });
    // The ghost's forward is the way to the wall; the ladder's own mesh is
    // turned half about and leans towards that forward, as SiegeLadder stands it.
    const half = Math.PI / 2;
    const lean = (description.lean * Math.PI) / 360;
    const turn = new Quaternion(Math.cos(half), 0, 0, Math.sin(half));
    const tilt = new Quaternion(Math.cos(lean), Math.sin(lean), 0, 0);
    const rotation = turn.mul(tilt);
    player.emit(BEGIN_EVENT, JSON.stringify({ model: description.model, scale: 1, rotations: [rotation], keepOpen: false, minDistance: 0.5, clearance: 0 }));
    return true;
}

/** Ends it, whether the player asked or something else did. Returns how many they placed. */
export function endBuild(player: Player, tellClient: boolean): number {
    const session = sessions.get(player.id);
    sessions.delete(player.id);
    if (tellClient) {
        player.emit(STOP_EVENT, "");
    }
    return session ? session.placed : 0;
}

export function isBuilding(player: Player): boolean {
    return sessions.has(player.id);
}

export function installBuildHandlers(): void {
    Events.onClient(PLACE_EVENT, (sender, payload) => {
        const player = sender as Player;
        const session = sessions.get(player.id);

        // A client that kept going after being told to stop, or never asked.
        if (!session) {
            return;
        }

        const parsed = typeof payload === "string" ? (JSON.parse(payload) as unknown) : payload;
        const position = readVector(parsed, "position");
        const rotation = readQuaternion(parsed, "rotation");
        if (!position || !rotation) {
            Chat.sendToPlayer(player, "That placement did not carry a usable pose.");
            return;
        }

        // Measured against where the server thinks the player is.
        if (!player.ready || distanceBetween(player.position, position) > MAX_REACH) {
            Chat.sendToPlayer(player, "That spot is too far from you to build on.");
            return;
        }

        if (session.ladder !== undefined) {
            const ladder = SiegeLadder.spawn(session.ladder, position, rotation, player.virtualWorld);
            session.placed += 1;
            endBuild(player, true);
            Chat.sendToPlayer(player, `Stood ${ladder.kind} ladder ${ladder.id} (${ladder.height.toFixed(2)} m). Climb it; from its top, hold use to push it off; at its foot, use raises it.`);
            return;
        }

        const prop = Prop.spawn(session.model, position, rotation, session.scale, session.physics, player.virtualWorld);
        session.placed += 1;
        Chat.sendToPlayer(player, `Built ${prop.modelName} (#${prop.id}). Right-click when you are done.`);
        console.log(`${player.nickname} built ${prop.modelName} as prop ${prop.id}`);
    });

    // A client that leaves the mode on its own says so.
    Events.onClient(`${RESOURCE}:build.ended`, (sender) => {
        const player = sender as Player;
        const placed = endBuild(player, false);
        if (placed > 0) {
            Chat.sendToPlayer(player, `Done building. You placed ${placed}.`);
        }
    });

    Events.on("playerDisconnect", (player) => {
        sessions.delete(player.id);
    });
}
