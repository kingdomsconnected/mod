/**
 * The client half of the default gamemode: a CEF debug panel on F4.
 *
 * Three directions: the page calls `callEvent`, `Web.on` receives it; `Web.emit`
 * sends a whole snapshot back per channel; `Events.emitServer` and `player.emit`
 * carry what only the server knows. Nothing here is privileged -- every button
 * that changes the world sends the `/` line a player could have typed.
 */

import { installBook } from "./book.js";
import { installBuild } from "./build.js";
import { installCamera } from "./camera.js";
import { installCreator } from "./creator.js";
import { installDice } from "./dice.js";
import { installFollow } from "./follow.js";
import { installCombat } from "./combat.js";
import { installHud } from "./hud.js";
import { installPick } from "./pick.js";
import { installScreen } from "./screen.js";
import * as panel from "./panel.js";
import { readSelf } from "./snapshot.js";
import { describeAll, flip } from "./switches.js";

const RESOURCE = "kcdc-gamemode";

/** F5, F6 and F9 already belong to the client itself. */
const OPEN_KEY = "f4";

const SAMPLE_MS = 250;

/** Samples between server polls. */
const POLL_EVERY = 8;

const LOG_LINES = 40;

const lines: string[] = [];
let ticks = 0;
let timer = -1;

function remember(text: string): void {
    lines.push(text);
    if (lines.length > LOG_LINES) {
        lines.splice(0, lines.length - LOG_LINES);
    }
    panel.push("panel:log", lines);
}

function note(text: string): void {
    console.log(text);
    remember(text);
}

function readField(payload: unknown, key: string): unknown {
    return typeof payload === "object" && payload !== null ? (payload as Record<string, unknown>)[key] : undefined;
}

function askServer(): void {
    Events.emitServer(`${RESOURCE}:panel.poll`);
}

function pushSelf(): void {
    panel.push("panel:self", readSelf());
}

function pushSwitches(): void {
    panel.push("panel:switches", describeAll());
}

const handlers: panel.PageHandlers = {
    "panel:ready": () => {
        panel.markReady();
        panel.push("panel:init", { ...panel.describe(), key: OPEN_KEY.toUpperCase(), screen: Web.getScreenSize() });
        panel.push("panel:log", lines);
        pushSwitches();
        pushSelf();
        askServer();
    },

    "panel:close": () => {
        panel.hide();
    },

    "panel:blur": () => {
        panel.blur();
        note(`input handed back to the game; ${OPEN_KEY.toUpperCase()} works again`);
    },

    "panel:refresh": () => {
        pushSelf();
        pushSwitches();
        askServer();
    },

    "panel:command": (payload) => {
        const line = readField(payload, "line");
        if (typeof line !== "string" || line.trim().length === 0) {
            return;
        }

        const text = line.trim().startsWith("/") ? line.trim() : `/${line.trim()}`;
        Chat.send(text);
        note(`sent ${text}`);
    },

    "panel:switch": (payload) => {
        const name = readField(payload, "name");
        if (typeof name !== "string") {
            return;
        }

        const landed = flip(name);
        if (landed === null) {
            note(`no switch called '${name}'`);
            return;
        }

        pushSwitches();
        note(`${name} is now ${landed ? "on" : "off"}`);
    },
};

Key.bind(OPEN_KEY, "down", () => {
    if (panel.toggle(handlers)) {
        askServer();
    }
});

Events.on(`${RESOURCE}:panel.open`, () => {
    panel.open(handlers);
    askServer();
});

Events.on(`${RESOURCE}:panel.realm`, (realm) => {
    panel.push("panel:realm", realm);
});

// Commands answer in chat, which a panel covering the screen would swallow.
// `chatMessage` is a reserved event, so it has no entry in the generated EventMap.
Events.on("chatMessage", (message) => {
    const text = readField(message, "text");
    if (typeof text !== "string" || text.length === 0) {
        return;
    }

    const author = readField(message, "author");
    remember(typeof author === "string" && author.length > 0 ? `${author}: ${text}` : text);
});

// The one failure a panel cannot report about itself. The view goes, so the
// next F4 or /ui retries the load instead of showing a dead page.
Events.on("browserLoadingFailed", (event) => {
    if (event.isMainFrame) {
        note(`page failed to load: ${event.description} (${event.errorCode}) at ${event.url}`);
        if (panel.owns(event.viewId)) {
            panel.destroy();
        }
    }
});

timer = setInterval(() => {
    if (!panel.isVisible()) {
        return;
    }

    pushSelf();
    ticks += 1;
    if (ticks % POLL_EVERY === 0) {
        askServer();
    }
}, SAMPLE_MS);

// The `/build` preview; it carries no tick of its own.
installBuild();

// The `/camera` switch.
installCamera();

// The `/book` demo books.
installBook();

// The `/creator` stage, which the server asks for.
installCreator();

// The `/pick` trace, for the prop-hunt test commands.
installPick();

// The `/screenfx` presets: intoxication and injury.
installScreen();

// The `/hud` commands and the server's optional playerSpawned Stats test.
installHud();
installDice();
installCombat();
installFollow();

Events.on("resourceStop", (resourceName) => {
    if (resourceName === RESOURCE) {
        clearInterval(timer);
        panel.destroy();
    }
});

console.log(`debug panel armed; press ${OPEN_KEY.toUpperCase()} or type /ui`);
