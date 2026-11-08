/**
 * The panel's CEF view. A focused view owns mouse and keyboard, which is also
 * what stops `Key.bind` from firing, so while it is focused only the page can
 * close it. Focus therefore waits for the page to announce itself: a page that
 * never loads -- missing from the server's resource, or failing outright --
 * must not take input it has no way to give back.
 */


const PAGE = "fw://resources/kcdc-gamemode/ui/index.html";

/** Sizes at 1080p. Larger screens multiply them, and the page zooms by its size over PREFERRED, so the two must agree with ui/index.html. */
const REFERENCE = { width: 1920, height: 1080 };
const PREFERRED = { width: 1040, height: 680 };
const MINIMUM = { width: 720, height: 460 };
const MARGIN = 120;

/** One handler per page event name, registered when the view is created. */
export type PageHandlers = Readonly<Record<string, (payload: unknown) => void>>;

let view = -1;
let pageReady = false;

/** Focus was asked for before the page could take it; granted by markReady. */
let focusPending = false;

/** The newest value per channel, held for a page that has not finished loading. */
const latest = new Map<string, unknown>();

export function isVisible(): boolean {
    return view >= 0 && Web.isViewVisible(view);
}

export function open(handlers: PageHandlers, focus = true): boolean {
    if (view < 0 && !create(handlers)) {
        return false;
    }

    Web.showView(view);
    if (focus) {
        if (pageReady) {
            Web.focusView(view, true);
        } else {
            focusPending = true;
        }
    }
    return true;
}

export function hide(): void {
    focusPending = false;
    if (view >= 0) {
        Web.focusView(view, false);
        Web.hideView(view);
    }
}

/** Leaves the panel up but gives input back to the game. */
export function blur(): void {
    focusPending = false;
    if (view >= 0) {
        Web.focusView(view, false);
    }
}

export function toggle(handlers: PageHandlers): boolean {
    if (isVisible()) {
        hide();
        return false;
    }

    open(handlers);
    return true;
}

/** The page announced itself; anything held while it loaded goes out now. */
export function markReady(): void {
    pageReady = true;
    if (focusPending) {
        focusPending = false;
        Web.focusView(view, true);
    }
    for (const [channel, payload] of latest) {
        Web.emit(view, channel, payload);
    }
}

export function push(channel: string, payload: unknown): void {
    latest.set(channel, payload);
    if (view >= 0 && pageReady) {
        Web.emit(view, channel, payload);
    }
}

export function owns(viewId: number): boolean {
    return view >= 0 && viewId === view;
}

export function describe(): { resource: string; view: number; page: string } {
    return { resource: "kcdc-gamemode", view, page: PAGE };
}

export function destroy(): void {
    if (view < 0) {
        return;
    }

    Web.destroyView(view);
    view = -1;
    pageReady = false;
    focusPending = false;
    latest.clear();
}

function create(handlers: PageHandlers): boolean {
    const screen = Web.getScreenSize();
    const scale = Math.max(1, Math.min(screen.width / REFERENCE.width, screen.height / REFERENCE.height));
    const width = Math.round(Math.max(MINIMUM.width * scale, Math.min(PREFERRED.width * scale, screen.width - MARGIN * scale)));
    const height = Math.round(Math.max(MINIMUM.height * scale, Math.min(PREFERRED.height * scale, screen.height - MARGIN * scale)));

    try {
        view = Web.createView(PAGE, {
            width,
            height,
            x: Math.round((screen.width - width) / 2),
            y: Math.round((screen.height - height) / 2),
            zIndex: 100,
            visible: true,
            focus: false,
        });
    } catch (thrown: unknown) {
        console.error(`could not create the panel view: ${thrown instanceof Error ? thrown.message : String(thrown)}`);
        view = -1;
        return false;
    }

    pageReady = false;
    focusPending = false;
    for (const [name, handler] of Object.entries(handlers)) {
        Web.on(view, name, handler);
    }

    console.log(`panel view ${view} created at ${width}x${height}`);
    return true;
}
