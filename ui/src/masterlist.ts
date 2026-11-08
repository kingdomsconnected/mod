import { useCallback, useEffect, useRef, useState } from "preact/hooks";

import type { ServerEntry } from "./types";

/**
 * Client for MafiaHub's masterlist (apps/masterlist in mafiahub/services).
 *
 * The page owns this rather than the host: fw:// is registered CORS- and
 * fetch-enabled, so reading it here is one fetch, while reading it natively
 * would cost a worker thread and a per-frame poll.
 *
 * GET /servers is public, CORS `origin: *` and rate limited to 100 req/min per
 * IP. The service drops servers that have not pinged in two minutes, so what
 * comes back is already an "online now" list.
 */

/**
 * One `metadata` pair of a row: the free-form operator fields the dashboard collects and the
 * directory passes through untouched. `discord_invite_url` and `website_url` are the ones drawn.
 */
interface MasterlistMetadata {
    key: string;
    value: string;
}

/** Raw wire shape of one row of GET /servers. Mirrors apps/masterlist/src/types.ts. */
interface MasterlistServer {
    id: string;
    name: string;
    localization: string;
    gamemode: string;
    current_players: number;
    max_players: number;
    version: string;
    ip: string;
    port: number;
    last_ping: string;
    /** Absent on rows from a masterlist or a server that predates the flag; reads as false. */
    passworded?: boolean;
    /** The operator's logo, or null when they set none. Absent on rows from a masterlist that predates it. */
    logo_url?: string | null;
    /** Empty when the operator filled nothing in. Absent on rows from a masterlist that predates the field. */
    metadata?: MasterlistMetadata[] | null;
}

const REQUEST_TIMEOUT_MS = 10_000;

// Rows go stale server-side after two minutes; four requests a minute is well under the limit.
const POLL_INTERVAL_MS = 15_000;

function isPort(value: unknown): value is number {
    return typeof value === "number" && Number.isInteger(value) && value >= 1 && value <= 65535;
}

function isCount(value: unknown): value is number {
    return typeof value === "number" && Number.isInteger(value) && value >= 0;
}

// One malformed row must not empty the browser, so every row is checked rather than the array cast.
function isMasterlistServer(value: unknown): value is MasterlistServer {
    if (value === null || typeof value !== "object") {
        return false;
    }
    const row = value as Record<string, unknown>;
    return typeof row.id === "string" && typeof row.gamemode === "string" && typeof row.ip === "string" && row.ip.length > 0 && isPort(row.port) && isCount(row.current_players) && isCount(row.max_players);
}

function text(value: unknown, fallback: string): string {
    return typeof value === "string" && value.trim().length > 0 ? value.trim() : fallback;
}

// Every URL on a row is operator-supplied, so only a plain web address is let through to the page.
function webUrl(value: unknown): string {
    if (typeof value !== "string") {
        return "";
    }
    try {
        const url = new URL(value.trim());
        return url.protocol === "https:" || url.protocol === "http:" ? url.href : "";
    }
    catch {
        return "";
    }
}

/**
 * The row check does not look inside `metadata`, so the pairs are read as the unvalidated operator
 * input they are: a malformed or repeated entry is skipped and the first usable URL wins.
 */
function metadataUrl(metadata: MasterlistServer["metadata"], key: string): string {
    if (!Array.isArray(metadata)) {
        return "";
    }
    for (const entry of metadata) {
        if (entry === null || typeof entry !== "object" || entry.key !== key) {
            continue;
        }
        const url = webUrl(entry.value);
        if (url) {
            return url;
        }
    }
    return "";
}

function toServerEntry(server: MasterlistServer): ServerEntry {
    const address = `${server.ip}:${server.port}`;
    return {
        id: server.id,
        name: text(server.name, address),
        host: server.ip,
        port: server.port,
        address,
        region: text(server.localization, "Unknown realm"),
        version: text(server.version, ""),
        players: server.current_players,
        maxPlayers: server.max_players,
        passworded: server.passworded === true,
        logoUrl: webUrl(server.logo_url),
        discordUrl: metadataUrl(server.metadata, "discord_invite_url"),
        websiteUrl: metadataUrl(server.metadata, "website_url"),
        full: server.max_players > 0 && server.current_players >= server.max_players,
    };
}

/** Fetch the servers announcing themselves under `gameMode`. Throws on a network or non-2xx answer. */
export async function fetchServers(baseUrl: string, gameMode: string, signal?: AbortSignal): Promise<ServerEntry[]> {
    // Hand-rolled rather than AbortSignal.timeout/any, which the chrome100 build target predates.
    const controller = new AbortController();
    const timer = window.setTimeout(() => controller.abort(), REQUEST_TIMEOUT_MS);
    const abortFromCaller = () => controller.abort();
    // An already-aborted signal never fires the event, so adopt its state before listening.
    if (signal?.aborted) {
        controller.abort();
    }
    signal?.addEventListener("abort", abortFromCaller);

    let response: Response;
    try {
        response = await fetch(`${baseUrl.replace(/\/+$/, "")}/servers`, { signal: controller.signal, cache: "no-store", headers: { Accept: "application/json" } });
    }
    finally {
        window.clearTimeout(timer);
        signal?.removeEventListener("abort", abortFromCaller);
    }

    if (!response.ok) {
        throw new Error(`The directory answered ${response.status}.`);
    }

    const payload: unknown = await response.json();
    if (!Array.isArray(payload)) {
        throw new Error("The directory returned something that is not a server list.");
    }

    // One masterlist serves every MafiaHub game, and `gamemode` is whatever the server announced.
    const wanted = gameMode.trim().toLocaleLowerCase();
    return payload
        .filter(isMasterlistServer)
        .filter((server) => !wanted || server.gamemode.trim().toLocaleLowerCase() === wanted)
        .map(toServerEntry)
        .sort((left, right) => right.players - left.players || left.name.localeCompare(right.name));
}

export interface MasterlistState {
    servers: ServerEntry[];
    /** A fetch the player is waiting on: the first one, or one they asked for. */
    loading: boolean;
    error: string;
    refresh: () => void;
}

/** Polls the masterlist for as long as the calling component is mounted. */
export function useMasterlist(baseUrl: string, gameMode: string): MasterlistState {
    const [servers, setServers] = useState<ServerEntry[]>([]);
    const [loading, setLoading] = useState(true);
    const [error, setError] = useState("");
    const inFlight = useRef<AbortController>();

    const load = useCallback(async (showSpinner: boolean) => {
        // Nothing to read until the host pushes the bootstrap, but the panel must not sit on a
        // spinner that can never finish.
        if (!baseUrl) {
            setLoading(false);
            setError("The server directory address has not arrived yet.");
            return;
        }

        inFlight.current?.abort();
        const controller = new AbortController();
        inFlight.current = controller;

        if (showSpinner) {
            setLoading(true);
        }
        try {
            const entries = await fetchServers(baseUrl, gameMode, controller.signal);
            if (!controller.signal.aborted) {
                setServers(entries);
                setError("");
            }
        }
        catch (reason) {
            // Superseded or unmounted is not a failure, and one failed poll must not blank the list.
            if (!controller.signal.aborted) {
                setError(reason instanceof Error ? reason.message : "The server directory could not be reached.");
            }
        }
        finally {
            if (!controller.signal.aborted) {
                setLoading(false);
            }
        }
    }, [baseUrl, gameMode]);

    useEffect(() => {
        void load(true);
        const timer = window.setInterval(() => void load(false), POLL_INTERVAL_MS);
        return () => {
            window.clearInterval(timer);
            inFlight.current?.abort();
        };
    }, [load]);

    const refresh = useCallback(() => void load(true), [load]);

    return { servers, loading, error, refresh };
}
