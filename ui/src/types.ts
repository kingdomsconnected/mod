export interface Bootstrap {
    brand: string;
    version: string;
    defaultHost: string;
    defaultPort: number;
    world: string;
    /** Base address of MafiaHub's masterlist; the browser reads `${masterlistUrl}/servers`. */
    masterlistUrl: string;
    /** The gamemode KCDC servers announce under, matched case-insensitively. */
    gameMode: string;
    /** Last successfully opened or saved project, from the client's recent-project history. */
    lastProject: string;
}

/** A masterlist row as the browser draws it. See `masterlist.ts` for the wire shape. */
export interface ServerEntry {
    id: string;
    name: string;
    host: string;
    port: number;
    address: string;
    region: string;
    version: string;
    players: number;
    maxPlayers: number;
    passworded: boolean;
    /** The operator's logo as an http(s) URL, or empty when there is none to draw. */
    logoUrl: string;
    /** The operator's Discord invite as an http(s) URL, or empty when they published none. */
    discordUrl: string;
    /** The operator's website as an http(s) URL, or empty when they published none. */
    websiteUrl: string;
    full: boolean;
}

export interface FrontendPresentation {
    visible: boolean;
    message: string;
    /**
     * The server's line while its admission gate holds the connection ("checking the whitelist", a
     * queue position). Non-empty replaces the menu with the admission card; the game's own loading
     * screen does not exist yet, since no level is asked for before the server lets the player in.
     */
    admission: string;
    /**
     * The server's streamed assets while they download, before the level is asked for. Null when
     * there is nothing to fetch. `awaitingConsent` holds the download for the player to accept its
     * size; the card then asks rather than shows progress.
     */
    download: DownloadProgress | null;
}

export interface DownloadProgress {
    received: number;
    total: number;
    awaitingConsent: boolean;
    /** Recent throughput in bytes per second; 0 until bytes move. */
    bytesPerSecond: number;
    /** The server has every download slot busy and this join waits for one. */
    waitingForSlot: boolean;
    /** How many joins are ahead when waiting (0 is next); -1 when the server did not say. */
    queuePosition: number;
}

/** `menu.worldBuilder`: a new project on a level, or a saved project the client asks the player to pick. */
export type WorldBuilderPayload = { level: string } | { openProject: true } | { lastProject: true };

/** `menu.openUrl`: an operator's link, which the host opens in the player's own browser. */
export interface OpenUrlPayload {
    url: string;
}

export interface ConnectPayload {
    nickname: string;
    host: string;
    port: number;
    password: string;
}

export interface KCDCMenuBridge {
    setBootstrap(bootstrap: Partial<Bootstrap>): void;
    setState(state: FrontendPresentation): void;
}

declare global {
    interface Window {
        callEvent?: (event: string, payload: string | null) => void;
        KCDCMenu?: KCDCMenuBridge;
    }
}
