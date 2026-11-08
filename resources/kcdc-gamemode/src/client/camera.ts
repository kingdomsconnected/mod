/**
 * The client half of `/camera`: flips this machine's own view between the
 * game's first-person camera and `Camera.setThirdPerson`.
 */

const CAMERA_EVENT = "kcdc-gamemode:camera.thirdPerson";

/** The framework parses a JSON payload before handing it over, so this is usually an object already. */
function readRequest(payload: unknown): { mode: string; distance?: number } | undefined {
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
    const { mode, distance } = parsed as { mode?: unknown; distance?: unknown };
    if (typeof mode !== "string") {
        return undefined;
    }
    return { mode, distance: typeof distance === "number" ? distance : undefined };
}

export function installCamera(): void {
    Events.on(CAMERA_EVENT, (payload) => {
        const request = readRequest(payload);
        if (!request) {
            return;
        }

        const third = request.mode === "toggle" ? !Camera.isThirdPerson() : request.mode === "third";
        if (third) {
            Camera.setThirdPerson(request.distance !== undefined ? { distance: request.distance } : {});
        } else {
            Camera.setThirdPerson(null);
        }
        Hud.showNotification(third ? "Third-person camera" : "First-person camera");
    });
}
