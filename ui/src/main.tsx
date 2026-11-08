import { render, type ComponentChildren, type RefObject } from "preact";
import { useEffect, useLayoutEffect, useMemo, useRef, useState } from "preact/hooks";

import { emit, playUISound } from "./bridge";
import { useMasterlist, type MasterlistState } from "./masterlist";
import { useInterfaceScale } from "./scale";
import type { Bootstrap, ConnectPayload, DownloadProgress, FrontendPresentation, ServerEntry, WorldBuilderPayload } from "./types";
import "./tokens.css";
import "./menu.css";

// What the host pushes over PushBootstrap; these stand in until it does, and keep `npm run dev`
// pointed at the real directory in a plain browser.
const initialBootstrap: Bootstrap = {
    brand: "Kingdoms Connected",
    version: "",
    defaultHost: "127.0.0.1",
    defaultPort: 27015,
    world: "Kuttenberg",
    masterlistUrl: "https://masterlist.mafiahub.dev",
    gameMode: "kcdc",
    lastProject: "",
};

const initialPresentation: FrontendPresentation = {
    visible: false,
    message: "",
    admission: "",
    download: null,
};

const storageKeys = {
    nickname: "kcdc.nickname",
    quickConnect: "kcdc.quick-connect",
} as const;

interface QuickConnectValues {
    host: string;
    port: string;
    password: string;
}

function readLocalStorage(key: string): string | null {
    try {
        return window.localStorage.getItem(key);
    }
    catch {
        return null;
    }
}

function writeLocalStorage(key: string, value: string): void {
    try {
        window.localStorage.setItem(key, value);
    }
    catch {
        // The menu remains usable when storage is disabled by the host.
    }
}

function readQuickConnect(bootstrap: Bootstrap): QuickConnectValues {
    const fallback = { host: bootstrap.defaultHost, port: String(bootstrap.defaultPort), password: "" };
    const stored = readLocalStorage(storageKeys.quickConnect);
    if (!stored) {
        return fallback;
    }

    try {
        const parsed = JSON.parse(stored) as Partial<QuickConnectValues>;
        return {
            host: typeof parsed.host === "string" ? parsed.host.slice(0, 127) : fallback.host,
            port: typeof parsed.port === "string" ? parsed.port.slice(0, 5) : fallback.port,
            password: typeof parsed.password === "string" ? parsed.password.slice(0, 63) : fallback.password,
        };
    }
    catch {
        return fallback;
    }
}

function Header({ bootstrap, onQuit, onBuilder }: { bootstrap: Bootstrap; onQuit: () => void; onBuilder: () => void }) {
    return (
        <header class="topbar">
            <div class="wordmark">
                <span class="wordmark-kicker">Kingdom Come: Deliverance II</span>
                <strong>{bootstrap.brand}</strong>
            </div>
            <div class="world-mark">
                <span class="diamond" />
                <span>{bootstrap.world}</span>
                <small>Anno Domini 1403</small>
            </div>
            <nav class="menu-actions" aria-label="Tools and exit">
                <button class="secondary-button" type="button" onClick={onBuilder}><span>World Builder</span><ButtonChevron /></button>
                <button class="primary-button" type="button" onClick={() => { playUISound("cancel"); onQuit(); }}><span>Exit game</span><ButtonChevron /></button>
            </nav>
        </header>
    );
}

/** Shared modal lifetime: the browser owns focus containment, Escape and focus restoration. */
function ModalCard({ className, title, titleId, descriptionId, eyebrow, onCancel, children }: {
    className: string; title: string; titleId: string; descriptionId?: string; eyebrow?: string; onCancel: () => void; children: ComponentChildren;
}) {
    const dialog = useRef<HTMLDialogElement>(null);
    useLayoutEffect(() => {
        const element = dialog.current!;
        element.showModal();
        return () => element.close();
    }, []);

    return (
        <dialog class={`dialog-card menu-dialog ${className}`} ref={dialog} aria-labelledby={titleId} aria-describedby={descriptionId} onCancel={(event) => { event.preventDefault(); playUISound("cancel"); onCancel(); }}>
            <div class="parchment-corner top-left" />
            <div class="parchment-corner top-right" />
            <div class="parchment-corner bottom-left" />
            <div class="parchment-corner bottom-right" />
            {eyebrow && <p class="eyebrow dark">{eyebrow}</p>}
            <h2 class="dialog-title" id={titleId}>{title}</h2>
            {eyebrow && <div class="rule dark"><span /><b>&#9670;</b><span /></div>}
            {children}
        </dialog>
    );
}

function ConfirmDialog({ className, title, titleId, descriptionId, eyebrow, cancelLabel, confirmLabel, onCancel, onConfirm, children }: {
    className: string; title: string; titleId: string; descriptionId: string; eyebrow: string; cancelLabel: string; confirmLabel: string; onCancel: () => void; onConfirm: () => void; children: ComponentChildren;
}) {
    return (
        <ModalCard className={className} title={title} titleId={titleId} descriptionId={descriptionId} eyebrow={eyebrow} onCancel={onCancel}>
            {children}
            <div class="dialog-actions">
                <button class="secondary-button" type="button" autoFocus onClick={() => { playUISound("cancel"); onCancel(); }}><span>{cancelLabel}</span><ButtonChevron /></button>
                <button class="primary-button" type="button" onClick={onConfirm}><span>{confirmLabel}</span><ButtonChevron /></button>
            </div>
        </ModalCard>
    );
}

/**
 * The offline World Builder: a level for a new project, or a saved project. The client answers
 * from its tick, so this only asks and closes; a refusal comes back as the menu's error banner.
 */
function WorldBuilderMenu({ lastProject, onCancel }: { lastProject: string; onCancel: () => void }) {
    const [level, setLevel] = useState("kutnohorsko");
    const open = (request: WorldBuilderPayload) => { emit("menu.worldBuilder", request); onCancel(); };
    return (
        <ModalCard className="builder-card" title="World Builder" titleId="builder-title" onCancel={onCancel}>
            <p>Create a project on a game level, or open a saved project. No server connection is needed.</p>
            <div class="builder-resume">
                <button class="secondary-button" type="button" disabled={!lastProject} onClick={() => open({ lastProject: true })}>Load last project</button>
                <span title={lastProject}>{lastProject ? lastProject.split(/[\\/]/).pop() : "No saved project yet"}</span>
            </div>
            <label>Level <select autoFocus value={level} onChange={event => setLevel(event.currentTarget.value)}>
                <option value="kutnohorsko">Kuttenberg</option>
                <option value="trosecko">Trosky</option>
                <option value="klaster">Monastery (requires the DLC)</option>
            </select></label>
            <div class="builder-actions">
                <button class="secondary-button" type="button" onClick={() => { playUISound("cancel"); onCancel(); }}>Back</button>
                <button class="secondary-button" type="button" onClick={() => open({ openProject: true })}>Open project...</button>
                <button class="primary-button" type="button" onClick={() => open({ level })}>New project</button>
            </div>
        </ModalCard>
    );
}

/** Asked before the game goes away, because returning takes a full game launch. */
function QuitConfirm({ onCancel }: { onCancel: () => void }) {
    return (
        <ConfirmDialog className="quit-card" title="Close the game?" titleId="quit-title" descriptionId="quit-description" eyebrow="Before you go" cancelLabel="Stay" confirmLabel="Exit game" onCancel={onCancel} onConfirm={() => emit("menu.quit")}>
            <p class="dialog-copy" id="quit-description">Bohemia keeps its counsel until you ride back, and any realm you were riding with carries on without you.</p>
        </ConfirmDialog>
    );
}

/** What the card says while the server has no download slot free for this join. */
function describeQueue(position: number): string {
    if (position > 0) {
        return `The server is busy sending its wares to others; ${position} ahead of you.`;
    }
    return position === 0 ? "The server is busy sending its wares to others; you are next." : "The server is busy sending its wares to others; waiting for a slot.";
}

function formatBytes(bytes: number): string {
    const units = ["B", "KB", "MB", "GB"];
    let value = bytes;
    let unit = 0;
    while (value >= 1024 && unit < units.length - 1) {
        value /= 1024;
        unit += 1;
    }
    return `${value.toFixed(unit === 0 || value >= 100 ? 0 : 1)} ${units[unit]}`;
}

/**
 * Up while the server's admission gate holds the connection and its script chose to say
 * something -- a plain whitelist check answers too fast to need it -- and while the server's
 * streamed assets download. A download larger than the player's threshold waits here for them to
 * accept its size. Leaving is always a way out.
 */
function AdmissionCard({ bootstrap, status, download }: { bootstrap: Bootstrap; status: string; download: DownloadProgress | null }) {
    const leave = useRef<HTMLButtonElement>(null);
    const accept = useRef<HTMLButtonElement>(null);
    const asking = download?.awaitingConsent ?? false;

    useEffect(() => {
        (asking ? accept : leave).current?.focus();
    }, [asking]);

    const fraction = download && download.total > 0 ? Math.min(1, download.received / download.total) : 0;
    return (
        <main class="screen admission-screen">
            <section class="dialog-card admission-card" role="status" aria-live="polite">
                <div class="parchment-corner top-left" />
                <div class="parchment-corner top-right" />
                <div class="parchment-corner bottom-left" />
                <div class="parchment-corner bottom-right" />
                <p class="eyebrow dark">{bootstrap.brand}</p>
                <h2 class="dialog-title">{download ? "Fetching the server's wares" : "At the gates"}</h2>
                <div class="rule dark"><span /><b>&#9670;</b><span /></div>
                {status && <p class="admission-status">{status}</p>}
                {download && (asking
                    ? <p class="admission-status">This server streams {formatBytes(download.total)} of models, sounds and textures. They are kept for your next visit.</p>
                    : (
                        <div class="download-progress">
                            <div class="download-bar" role="progressbar" aria-valuemin={0} aria-valuemax={download.total} aria-valuenow={download.received}><span style={{ width: `${(fraction * 100).toFixed(1)}%` }} /></div>
                            <p class="admission-status">{formatBytes(download.received)} of {formatBytes(download.total)}{download.bytesPerSecond > 0 && !download.waitingForSlot ? ` at ${formatBytes(download.bytesPerSecond)}/s` : ""}</p>
                            {download.waitingForSlot && <p class="admission-status">{describeQueue(download.queuePosition)}</p>}
                        </div>
                    ))}
                <div class="admission-actions">
                    {asking && <button class="primary-button" type="button" ref={accept} onClick={() => emit("menu.acceptDownload")}><span>Download</span><ButtonChevron /></button>}
                    <button class="secondary-button" type="button" ref={leave} onClick={() => emit("menu.leave")}><span>Leave</span><ButtonChevron /></button>
                </div>
            </section>
        </main>
    );
}

function Hero() {
    return (
        <aside class="hero-copy">
            <p class="eyebrow">A multiplayer chronicle</p>
            <h1>Find your place<br />in <em>Bohemia.</em></h1>
            <div class="rule"><span /><b>◆</b><span /></div>
            <p class="hero-description">Gather your company, cross the city gates and write a story no minstrel has sung before.</p>
            <blockquote>“Fortune favours those who ride together.”</blockquote>
        </aside>
    );
}

function MenuMessage({ message, onDismiss }: { message: string; onDismiss: () => void }) {
    if (!message) {
        return null;
    }
    return (
        <div class="menu-message" role="status">
            <span class="message-seal">!</span>
            <span>{message}</span>
            <button type="button" aria-label="Dismiss" onClick={() => { playUISound("cancel"); onDismiss(); }}>×</button>
        </div>
    );
}

function ButtonChevron() {
    return <svg class="button-chevron" viewBox="0 0 8 12" aria-hidden="true" focusable="false"><path d="M1 1L6 6L1 11" /></svg>;
}

/** Marks a realm that asks a password before it lets anyone in. */
function LockIcon() {
    return (
        <svg class="lock-icon" viewBox="0 0 12 14" role="img" aria-label="Password required" focusable="false">
            <title>Password required</title>
            <path class="lock-shackle" d="M3.5 6V4a2.5 2.5 0 0 1 5 0v2" />
            <rect class="lock-body" x="1.5" y="6" width="9" height="7" />
            <path class="lock-keyhole" d="M6 8.6v2" />
        </svg>
    );
}

/** The operator's logo, or the realm's initial on a blank crest when there is none or it fails to load. */
function ServerLogo({ server, large }: { server: ServerEntry; large?: boolean }) {
    const [failed, setFailed] = useState(false);
    useEffect(() => setFailed(false), [server.logoUrl]);
    return (
        <div class={`server-logo${large ? " large" : ""}`} aria-hidden="true">
            {server.logoUrl && !failed ? <img src={server.logoUrl} alt="" loading="lazy" decoding="async" referrerPolicy="no-referrer" draggable={false} onError={() => setFailed(true)} /> : <span>{server.name.trim().charAt(0).toLocaleUpperCase()}</span>}
        </div>
    );
}

function ServerRow({ server, selected, onSelect, onJoin }: { server: ServerEntry; selected: boolean; onSelect: () => void; onJoin: () => void }) {
    return (
        <div
            class={`server-row${selected ? " selected" : ""}`}
            role="option"
            aria-selected={selected}
            tabIndex={0}
            onClick={onSelect}
            onDblClick={onJoin}
            onKeyDown={(event) => {
                if (event.key === "Enter") {
                    event.preventDefault();
                    selected ? onJoin() : onSelect();
                }
                else if (event.key === " ") {
                    event.preventDefault();
                    onSelect();
                }
            }}
        >
            <ServerLogo server={server} />
            <div class="server-main">
                <div class="server-title-line">
                    <strong class="server-title">{server.name}</strong>
                    {server.full && <span class="server-badge">Full</span>}
                    {server.passworded && <LockIcon />}
                </div>
                <div class="server-meta"><span>{server.region}</span><i /><span>{server.address}</span></div>
            </div>
            <div class="server-stat"><strong>{server.players}</strong><small> / {server.maxPlayers}</small></div>
            <div class="server-stat"><strong class="server-version">{server.version || "—"}</strong></div>
        </div>
    );
}

/** Discord's own mark, so the row reads as the invite it is. */
function DiscordIcon() {
    return <svg class="link-icon brand" viewBox="0 0 24 24" aria-hidden="true" focusable="false"><path d="M20.317 4.3698a19.7913 19.7913 0 00-4.8851-1.5152.0741.0741 0 00-.0785.0371c-.211.3753-.4447.8648-.6083 1.2495-1.8447-.2762-3.68-.2762-5.4868 0-.1636-.3933-.4058-.8742-.6177-1.2495a.077.077 0 00-.0785-.037 19.7363 19.7363 0 00-4.8852 1.515.0699.0699 0 00-.0321.0277C.5334 9.0458-.319 13.5799.0992 18.0578a.0824.0824 0 00.0312.0561c2.0528 1.5076 4.0413 2.4228 5.9929 3.0294a.0777.0777 0 00.0842-.0276c.4616-.6304.8731-1.2952 1.226-1.9942a.076.076 0 00-.0416-.1057c-.6528-.2476-1.2743-.5495-1.8722-.8923a.077.077 0 01-.0076-.1277c.1258-.0943.2517-.1923.3718-.2914a.0743.0743 0 01.0776-.0105c3.9278 1.7933 8.18 1.7933 12.0614 0a.0739.0739 0 01.0785.0095c.1202.099.246.1981.3728.2924a.077.077 0 01-.0066.1276 12.2986 12.2986 0 01-1.873.8914.0766.0766 0 00-.0407.1067c.3604.698.7719 1.3628 1.225 1.9932a.076.076 0 00.0842.0286c1.961-.6067 3.9495-1.5219 6.0023-3.0294a.077.077 0 00.0313-.0552c.5004-5.177-.8382-9.6739-3.5485-13.6604a.061.061 0 00-.0312-.0286zM8.02 15.3312c-1.1825 0-2.1569-1.0857-2.1569-2.419 0-1.3332.9555-2.4189 2.157-2.4189 1.2108 0 2.1757 1.0952 2.1568 2.419 0 1.3332-.9555 2.4189-2.1569 2.4189zm7.9748 0c-1.1825 0-2.1569-1.0857-2.1569-2.419 0-1.3332.9554-2.4189 2.1569-2.4189 1.2108 0 2.1757 1.0952 2.1568 2.419 0 1.3332-.946 2.4189-2.1568 2.4189Z" /></svg>;
}

/** The host's own website. */
function GlobeIcon() {
    return <svg class="link-icon" viewBox="0 0 16 16" aria-hidden="true" focusable="false"><circle cx="8" cy="8" r="6" /><path d="M2 8h12" /><path d="M8 2c2.4 1.7 2.4 10.3 0 12M8 2C5.6 3.7 5.6 12.3 8 14" /></svg>;
}

/** `https://discord.gg/abcdef/` reads as `discord.gg/abcdef`; the scheme is noise on a two-line chip. */
function readableUrl(url: string): string {
    return url.replace(/^https?:\/\//i, "").replace(/\/+$/, "");
}

/**
 * One of the operator's links. The page never navigates there itself -- it is the menu, and a CEF
 * view that leaves it has no way back -- so the host opens the address in the player's own browser.
 */
function ExternalLink({ url, label, children }: { url: string; label: string; children: ComponentChildren }) {
    return (
        <button class="details-link" type="button" title={url} onClick={() => emit("menu.openUrl", { url })}>
            {children}
            <span>{label}</span>
            <small>{readableUrl(url)}</small>
        </button>
    );
}

interface DetailsProps {
    server: ServerEntry;
    password: string;
    onPassword: (password: string) => void;
    onJoin: () => void;
    passwordInput: RefObject<HTMLInputElement>;
}

/**
 * The selected realm, beside the list rather than under it: its crest and the operator's links need
 * the height, and the way in belongs where the realm it leads to is being read.
 */
function ServerDetails({ server, password, onPassword, onJoin, passwordInput }: DetailsProps) {
    const needsPassword = server.passworded && password.length === 0;
    return (
        <aside class="server-details" aria-label="Realm details">
            {/* Only what the realm is scrolls; the way in stays on screen however much it carries. */}
            <div class="details-body">
                <ServerLogo server={server} large />
                <h3 class="details-name">{server.passworded && <LockIcon />}{server.name}</h3>
                <p class="details-meta">{server.region} · {server.address}</p>
                {server.full && <span class="server-badge details-badge">Full</span>}
                <dl class="details-facts">
                    <div><dt>Players</dt><dd>{server.players} / {server.maxPlayers}</dd></div>
                    <div><dt>Version</dt><dd>{server.version || "\u2014"}</dd></div>
                </dl>
                {(server.discordUrl || server.websiteUrl) && (
                    <div class="details-links">
                        <p class="eyebrow dark">Find the host</p>
                        {server.discordUrl && <ExternalLink url={server.discordUrl} label="Discord"><DiscordIcon /></ExternalLink>}
                        {server.websiteUrl && <ExternalLink url={server.websiteUrl} label="Website"><GlobeIcon /></ExternalLink>}
                    </div>
                )}
            </div>
            <div class="details-join">
                {server.passworded && (
                    <label class="compact-password">
                        <span>Passphrase</span>
                        <input type="password" ref={passwordInput} maxLength={63} value={password} onInput={(event) => onPassword(event.currentTarget.value)} onKeyDown={(event) => { if (event.key === "Enter") { event.preventDefault(); onJoin(); } }} autoComplete="off" />
                    </label>
                )}
                <button class="primary-button" type="button" disabled={server.full || needsPassword} onClick={onJoin}><span>{server.full ? "Realm is full" : needsPassword ? "Enter the passphrase" : "Join realm"}</span><ButtonChevron /></button>
            </div>
        </aside>
    );
}

/** Ignore display-only prefixes, but keep prerelease and build identifiers distinct. */
function normalizeVersion(version: string): string {
    return version.trim().replace(/^v(?=\d)/i, "");
}

interface PendingConnection {
    payload: ConnectPayload;
    clientVersion: string;
    serverVersion: string;
}

function VersionConfirm({ connection, onCancel, onJoin }: { connection: PendingConnection; onCancel: () => void; onJoin: () => void }) {
    return (
        <ConfirmDialog className="version-dialog" title="Different versions" titleId="version-title" descriptionId="version-warning" eyebrow="Before you join" cancelLabel="Cancel" confirmLabel="Join anyway" onCancel={onCancel} onConfirm={onJoin}>
            <p class="version-address">{connection.payload.host}:{connection.payload.port}</p>
            <dl class="version-details">
                <div><dt>Your version</dt><dd>{connection.clientVersion}</dd></div>
                <div><dt>Server version</dt><dd>{connection.serverVersion}</dd></div>
            </dl>
            <p class="dialog-copy" id="version-warning">You and this server are running different versions of Kingdoms Connected. Joining may cause compatibility issues, including errors or disconnections. Use the same version for the best experience.</p>
        </ConfirmDialog>
    );
}

/** Conflicting or unavailable directory data is not evidence of a version mismatch. */
function advertisedVersion(browser: MasterlistState, host: string, port: number): string {
    if (browser.error || browser.loading) {
        return "";
    }
    const versions = new Set(browser.servers.filter(server => server.host.toLowerCase() === host.toLowerCase() && server.port === port).map(server => normalizeVersion(server.version)));
    return versions.size === 1 ? versions.values().next().value! : "";
}

interface BrowserProps {
    browser: MasterlistState;
    onConnect: (host: string, port: number, password: string) => void;
    onQuickConnect: () => void;
}

/** Copy for the panel that stands in for the list: waiting, empty, or filtered to nothing. */
function browserNotice(browser: MasterlistState): { title: string; body: string } {
    if (browser.loading) {
        return { title: "Asking who rides today.", body: "Reading the realm directory." };
    }
    if (browser.servers.length === 0) {
        return { title: "No realm stands open.", body: "No server is announcing itself. Use quick connect if a friend gave you an address." };
    }
    return { title: "No realm answers that name.", body: "Try another search or use quick connect." };
}

function ServerBrowser({ browser, onConnect, onQuickConnect }: BrowserProps) {
    const { servers, loading, refresh } = browser;
    const [filter, setFilter] = useState("");
    const [selectedId, setSelectedId] = useState<string>();
    const [password, setPassword] = useState("");
    const passwordInput = useRef<HTMLInputElement>(null);
    const selected = servers.find((server) => server.id === selectedId);
    const visibleServers = useMemo(() => {
        const needle = filter.trim().toLocaleLowerCase();
        if (!needle) {
            return servers;
        }
        return servers.filter((server) => [server.name, server.region, server.address, server.version].some((value) => value.toLocaleLowerCase().includes(needle)));
    }, [filter, servers]);
    const notice = browserNotice(browser);

    // Choosing a locked realm is asking to type its password, so put the caret there.
    useEffect(() => {
        if (selected?.passworded) {
            passwordInput.current?.focus();
        }
    }, [selected?.id, selected?.passworded]);

    useEffect(() => {
        if (selectedId && !servers.some((server) => server.id === selectedId)) {
            setSelectedId(undefined);
            setPassword("");
        }
    }, [selectedId, servers]);

    const select = (server: ServerEntry) => {
        setSelectedId(server.id);
        if (!server.passworded) {
            setPassword("");
        }
    };
    const join = (server: ServerEntry) => {
        if (server.full || (server.passworded && password.length === 0)) {
            return;
        }
        onConnect(server.host, server.port, server.passworded ? password : "");
    };

    return (
        <section class="tab-page active">
            <div class="browser-toolbar">
                <label class="search-box">
                    <span aria-hidden="true" />
                    <input type="search" placeholder="Search realms, regions or addresses" value={filter} onInput={(event) => setFilter(event.currentTarget.value)} autoComplete="off" />
                </label>
                <button class={`text-button${loading ? " refreshing" : ""}`} type="button" onClick={() => { playUISound("confirm"); refresh(); }}><i>↻</i> Refresh</button>
            </div>
            <div class={`browser-body${selected ? " with-details" : ""}`}>
                <div class="browser-results">
                    {browser.error && (
                        <section class="masterlist-error" role="alert" aria-labelledby="masterlist-error-title">
                            <h3 id="masterlist-error-title">Could not load the server list</h3>
                            <p>The master server provides the public server list. It may be temporarily unavailable, or access may be blocked by your firewall, internet provider or country-level restrictions.</p>
                            <p>Check your internet connection and firewall settings, then select <strong>Refresh</strong>. If the list still does not load, try another connection or a VPN.</p>
                            <p>If you know a server's address and game port, use <strong>Quick connect</strong> to connect directly. This bypasses the list, but the server itself must still be reachable.</p>
                            {servers.length > 0 && <p class="masterlist-stale">Showing the last loaded servers. Their status may be out of date.</p>}
                            <button class="text-button" type="button" onClick={() => { playUISound("confirm"); onQuickConnect(); }}>Quick connect</button>
                        </section>
                    )}
                    <div class="browser-labels" aria-hidden="true" hidden={visibleServers.length === 0 && !!browser.error}><span /><span>Realm</span><span>Players</span><span>Version</span></div>
                    <div class="server-list" role="listbox" aria-label="Available servers" hidden={visibleServers.length === 0}>
                        {visibleServers.map((server) => <ServerRow key={server.id} server={server} selected={selectedId === server.id} onSelect={() => select(server)} onJoin={() => { select(server); if (!server.passworded) join(server); }} />)}
                    </div>
                    <div class="server-empty" hidden={visibleServers.length !== 0 || !!browser.error} role="status">
                        <div class="empty-ornament">⌁</div>
                        <strong>{notice.title}</strong>
                        <span>{notice.body}</span>
                    </div>
                </div>
                {selected && <ServerDetails server={selected} password={password} onPassword={setPassword} onJoin={() => join(selected)} passwordInput={passwordInput} />}
            </div>
        </section>
    );
}

function QuickConnect({ bootstrap, onConnect }: { bootstrap: Bootstrap; onConnect: BrowserProps["onConnect"] }) {
    const [initialValues] = useState(() => readQuickConnect(bootstrap));
    const [host, setHost] = useState(initialValues.host);
    const [port, setPort] = useState(initialValues.port);
    const [password, setPassword] = useState(initialValues.password);

    useEffect(() => {
        writeLocalStorage(storageKeys.quickConnect, JSON.stringify({ host, port, password } satisfies QuickConnectValues));
    }, [host, port, password]);

    return (
        <section class="tab-page active">
            <article class="quick-card">
                <div class="quick-card-copy">
                    <p class="eyebrow dark">Recommended</p>
                    <h3>Ride to your local realm</h3>
                    <p>Connect to the standard Kingdoms Connected server running on this machine.</p>
                    <span class="endpoint">{bootstrap.defaultHost}:{bootstrap.defaultPort}</span>
                </div>
                <button class="primary-button broad" type="button" onClick={() => onConnect(bootstrap.defaultHost, bootstrap.defaultPort, "")}><span>Quick connect</span><ButtonChevron /></button>
            </article>
            <div class="or-divider"><span /><b>or enter a destination</b><span /></div>
            <form class="direct-form" onSubmit={(event) => { event.preventDefault(); onConnect(host, Number(port), password); }}>
                <label class="field address-field">
                    <span>Server address</span>
                    <input maxLength={127} value={host} onInput={(event) => setHost(event.currentTarget.value)} autoComplete="off" spellcheck={false} />
                </label>
                <label class="field port-field">
                    <span>Port</span>
                    <input type="number" min={1} max={65535} value={port} onInput={(event) => setPort(event.currentTarget.value)} />
                </label>
                <label class="field password-field">
                    <span>Password <small>optional</small></span>
                    <input type="password" maxLength={63} value={password} onInput={(event) => setPassword(event.currentTarget.value)} autoComplete="off" />
                </label>
                <button class="secondary-button" type="submit"><span>Connect directly</span><ButtonChevron /></button>
            </form>
        </section>
    );
}

interface MenuProps {
    bootstrap: Bootstrap;
    presentation: FrontendPresentation;
    onLocalMessage: (message: string) => void;
}

function Menu({ bootstrap, presentation, onLocalMessage }: MenuProps) {
    const [tab, setTab] = useState<"browser" | "quick">("browser");
    const [quitting, setQuitting] = useState(false);
    const [pendingConnection, setPendingConnection] = useState<PendingConnection>();
    const [building, setBuilding] = useState(false);
    // Polled from here rather than from the browser tab, so the list is already there when the
    // player opens it, and stops the moment the menu leaves the screen with this component.
    const browser = useMasterlist(bootstrap.masterlistUrl, bootstrap.gameMode);
    const [nickname, setNickname] = useState(() => readLocalStorage(storageKeys.nickname)?.slice(0, 63) || "Player");
    const [messageDismissed, setMessageDismissed] = useState(false);

    useEffect(() => setMessageDismissed(false), [presentation.message]);
    useEffect(() => writeLocalStorage(storageKeys.nickname, nickname), [nickname]);

    const connect: BrowserProps["onConnect"] = (host, port, password) => {
        const destination = host.trim();
        if (!destination || !Number.isInteger(port) || port < 1 || port > 65535) {
            onLocalMessage("Enter a valid address and a port between 1 and 65535.");
            return;
        }
        onLocalMessage("");
        const payload = { nickname: nickname.trim() || "Player", host: destination, port, password };
        const clientVersion = normalizeVersion(bootstrap.version);
        // No DNS lookup: only the literal endpoint advertised by the directory can be compared.
        const serverVersion = advertisedVersion(browser, destination, port);
        if (clientVersion && serverVersion && clientVersion !== serverVersion) {
            playUISound("cancel");
            setPendingConnection({ payload, clientVersion, serverVersion });
            return;
        }
        emit("menu.connect", payload);
    };

    return (
        <main class="screen menu-screen">
            <Header bootstrap={bootstrap} onQuit={() => setQuitting(true)} onBuilder={() => setBuilding(true)} />
            <section class="layout">
                <Hero />
                <section class={`parchment${tab === "quick" ? " quick-mode" : ""}`} aria-label="Multiplayer menu">
                    <div class="parchment-corner top-left" />
                    <div class="parchment-corner top-right" />
                    <div class="parchment-corner bottom-left" />
                    <div class="parchment-corner bottom-right" />
                    <div class="panel-heading">
                        <div><p class="eyebrow dark">Multiplayer</p><h2>Choose your destination</h2></div>
                        <label class="identity-field">
                            <span>Your name</span>
                            <input maxLength={63} value={nickname} onInput={(event) => setNickname(event.currentTarget.value)} autoComplete="off" spellcheck={false} />
                        </label>
                    </div>
                    <MenuMessage message={messageDismissed ? "" : presentation.message} onDismiss={() => setMessageDismissed(true)} />
                    <nav class="tabs" aria-label="Connection method">
                        <button class={`tab${tab === "browser" ? " active" : ""}`} type="button" onClick={() => { playUISound("confirm"); setTab("browser"); }}><span>01</span> Server browser</button>
                        <button class={`tab${tab === "quick" ? " active" : ""}`} type="button" onClick={() => { playUISound("confirm"); setTab("quick"); }}><span>02</span> Quick connect</button>
                    </nav>
                    {tab === "browser" ? <ServerBrowser browser={browser} onConnect={connect} onQuickConnect={() => setTab("quick")} /> : <QuickConnect bootstrap={bootstrap} onConnect={connect} />}
                </section>
            </section>
            {building && <WorldBuilderMenu lastProject={bootstrap.lastProject} onCancel={() => setBuilding(false)} />}
            {quitting && <QuitConfirm onCancel={() => setQuitting(false)} />}
            {pendingConnection && <VersionConfirm connection={pendingConnection} onCancel={() => setPendingConnection(undefined)} onJoin={() => { setPendingConnection(undefined); emit("menu.connect", pendingConnection.payload); }} />}
        </main>
    );
}

function App() {
    const [bootstrap, setBootstrap] = useState(initialBootstrap);
    const [presentation, setPresentation] = useState(initialPresentation);
    const [localMessage, setLocalMessage] = useState("");
    const presentationRef = useRef(initialPresentation);

    useInterfaceScale();

    const effectivePresentation = localMessage ? { ...presentation, message: localMessage } : presentation;
    const showLocalMessage = (message: string) => {
        if (message) {
            playUISound("cancel");
        }
        setLocalMessage(message);
    };

    useLayoutEffect(() => {
        window.KCDCMenu = {
            setBootstrap: (next) => setBootstrap((current) => ({ ...current, ...next })),
            setState: (next) => {
                const previous = presentationRef.current;
                if (next.message && next.message !== previous.message) {
                    playUISound("cancel");
                }
                presentationRef.current = next;
                setPresentation(next);
                setLocalMessage("");
            },
        };
        emit("menu.ready");
        return () => {
            delete window.KCDCMenu;
        };
    }, []);

    return (
        <>
            {presentation.visible && <div class="veil" aria-hidden="true" />}
            {presentation.visible && (presentation.admission || presentation.download
                ? <AdmissionCard bootstrap={bootstrap} status={presentation.admission} download={presentation.download} />
                : <Menu bootstrap={bootstrap} presentation={effectivePresentation} onLocalMessage={showLocalMessage} />)}
        </>
    );
}

render(<App />, document.getElementById("app")!);
