/**
 * The client half of `/book`: opens the demo books through the `Book` API,
 * which is client-only -- the server carries the ask, the book is this
 * machine's own.
 */

import { chroniclePages, letterPages } from "./chronicle.js";

const BOOK_EVENT = "kcdc-gamemode:book";

/** How long each step of `/book fade` waits before the text sharpens by a tenth. */
const FADE_STEP_MS = 1200;

/** The book movie works in tenths; a finer step changes nothing on the page. */
const FADE_STEP = 0.1;

type BookStyle = "book" | "plainBook" | "letter";

// Module-local until the client contract is regenerated from a debug client
// (see scripting-api/README.md). Shadowing the global this way keeps
// compiling once the generated declaration exists too; delete it then.
interface BookPageImage {
    page: number;
    image: string;
}
interface BookOptions {
    pages: string[];
    style?: BookStyle;
    type?: number;
    visual?: number;
    legibility?: number;
    texture?: string;
    images?: BookPageImage[];
}
declare const Book: {
    open(options: BookOptions): number | null;
    close(): boolean;
    setLegibility(value: number): boolean;
    getOpenBook(): number | null;
    getLastError(): string;
    image(path: string): string;
};

interface BookRequest {
    action: string;
    style?: BookStyle;
    value?: number;
    pages?: string[];
}

let fadeBook: number | null = null;
let fadeTimer = -1;
let fadeLevel = 0;

/** The framework parses a JSON payload before handing it over, so this is usually an object already. */
function readRequest(payload: unknown): BookRequest | undefined {
    let parsed: unknown = payload;
    if (typeof payload === "string") {
        try {
            parsed = JSON.parse(payload) as unknown;
        } catch {
            return undefined;
        }
    }
    if (typeof parsed !== "object" || parsed === null) {
        return undefined;
    }
    const { action, style, value, pages } = parsed as { action?: unknown; style?: unknown; value?: unknown; pages?: unknown };
    if (typeof action !== "string") {
        return undefined;
    }
    const known = style === "book" || style === "plainBook" || style === "letter";
    const validPages = Array.isArray(pages) && pages.length > 0 && pages.length <= 8 && pages.every((page) => typeof page === "string" && page.length <= 8192);
    return { action, style: known ? style : undefined, value: typeof value === "number" ? value : undefined, pages: validPages ? pages as string[] : undefined };
}

/** A picture this resource ships, or null with the reason shown when it could not be packaged. */
function picture(path: string): string | null {
    try {
        return Book.image(path);
    } catch (error) {
        Hud.showNotification(`Book picture unavailable: ${String(error)}`);
        return null;
    }
}

function stopFade(): void {
    if (fadeTimer !== -1) {
        clearInterval(fadeTimer);
        fadeTimer = -1;
    }
    fadeBook = null;
}

function open(options: BookOptions): number | null {
    const id = Book.open(options);
    if (id === null) {
        Hud.showNotification(`The book will not open: ${Book.getLastError()}`);
    }
    return id;
}

function openChronicle(style: BookStyle, extra: Partial<BookOptions> = {}): number | null {
    return open({ pages: chroniclePages(picture("assets/book/seal.dds")), style, visual: 7, ...extra });
}

export function installBook(): void {
    Events.on(BOOK_EVENT, (payload) => {
        const request = readRequest(payload);
        if (!request) {
            return;
        }

        switch (request.action) {
            case "customLetter":
                if (request.pages) {
                    stopFade();
                    open({ pages: request.pages, style: "letter", visual: 3 });
                }
                break;
            case "chronicle":
                stopFade();
                openChronicle(request.style ?? "book");
                break;

            case "letter":
                stopFade();
                open({ pages: letterPages(), style: "letter", visual: 3 });
                break;

            case "parchment": {
                stopFade();
                // Plain binding: the red one's diffuse colour would darken
                // the parchment to 78%.
                const texture = picture("assets/book/parchment_diff.dds");
                openChronicle("plainBook", texture !== null ? { texture } : {});
                break;
            }

            case "fade":
                stopFade();
                fadeLevel = 0;
                fadeBook = openChronicle(request.style ?? "book", { legibility: 0 });
                break;

            case "legibility": {
                const value = request.value ?? 1;
                Hud.showNotification(Book.setLegibility(value) ? `Legibility ${value.toFixed(2)}` : "No book is in your hands yet.");
                break;
            }

            case "close":
                Hud.showNotification(Book.close() ? "Closing the book." : "No book is open.");
                break;

            case "status": {
                const id = Book.getOpenBook();
                Hud.showNotification(id !== null ? `Book ${id} is open.` : `No book is open. ${Book.getLastError()}`);
                break;
            }
        }
    });

    Events.on("bookOpened", (bookId) => {
        if (bookId !== fadeBook || fadeTimer !== -1) {
            return;
        }
        // The ink comes clear a tenth at a time, the movie's own resolution.
        fadeTimer = setInterval(() => {
            fadeLevel = Math.min(1, fadeLevel + FADE_STEP);
            if (!Book.setLegibility(fadeLevel) || fadeLevel >= 1) {
                stopFade();
            }
        }, FADE_STEP_MS);
    });

    Events.on("bookClosed", (bookId, reason) => {
        if (bookId === fadeBook) {
            stopFade();
        }
        Hud.showNotification(`The book is closed (${String(reason)}).`);
    });
}
