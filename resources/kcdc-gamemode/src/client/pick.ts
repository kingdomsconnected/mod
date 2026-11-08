/** The client half of `/pick`: the trace a hunter's swing would be answered with. */

const PICK_EVENT = "kcdc-gamemode:prophunt.pick";

/** How far `/pick` reaches, in metres: a hunter's swing is nowhere near it, a check is. */
const PICK_RANGE = 50;

/** What a hunter's swing would be answered with: the player under the crosshair, disguised or not. */
function pick(): string {
    const ray = Camera.screenRay({ range: PICK_RANGE });
    if (!ray) {
        return "No view to trace from.";
    }

    const hit = World.raycast(ray.origin, ray.target, { mode: "anything" });
    if (!hit) {
        return `Nothing within ${PICK_RANGE} m.`;
    }

    const where = `${hit.distance.toFixed(1)} m`;
    if (hit.player) {
        const disguise = hit.player.disguise;
        return `${hit.player.nickname} at ${where}${disguise ? `, disguised as ${disguise.slice(disguise.lastIndexOf("/") + 1)}` : ""}`;
    }
    return `${hit.entityClass ?? (hit.terrain ? "terrain" : "static geometry")} at ${where} (${hit.surface})`;
}

export function installPick(): void {
    Events.on(PICK_EVENT, () => {
        const text = pick();
        console.log(`/pick: ${text}`);
        Hud.showNotification(text);
    });
}
