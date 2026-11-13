import type { ConnectPayload, OpenUrlPayload, WorldBuilderPayload } from "./types";

export type UISound = "cancel" | "confirm";

const eventSounds: Partial<Record<string, UISound>> = {
    "menu.connect": "confirm",
    "menu.worldBuilder": "confirm",
    "menu.quit": "confirm",
    "menu.leave": "cancel",
    "menu.acceptDownload": "confirm",
    "menu.acceptScripts": "confirm",
    "menu.openUrl": "confirm",
};

export function playUISound(sound: UISound): void {
    window.callEvent?.(`ui.${sound}`, null);
}

export function emit(event: "menu.quit" | "menu.ready" | "menu.leave" | "menu.acceptDownload" | "menu.acceptScripts", payload?: never): void;
export function emit(event: "menu.connect", payload: ConnectPayload): void;
export function emit(event: "menu.worldBuilder", payload: WorldBuilderPayload): void;
export function emit(event: "menu.openUrl", payload: OpenUrlPayload): void;
export function emit(event: string, payload: ConnectPayload | OpenUrlPayload | WorldBuilderPayload | undefined): void {
    const sound = eventSounds[event];
    if (sound) {
        playUISound(sound);
    }
    window.callEvent?.(event, payload === undefined ? null : JSON.stringify(payload));
}
