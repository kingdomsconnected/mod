import { isInteractionTargetVisible } from "./interaction.js";

const STATE_EVENT = "kcdc-gamemode:dice.state";
const INTERACT_EVENT = "kcdc-gamemode:dice.interact";

interface TableOffer {
    token: number;
    tables: { id: string; position: { x: number; y: number; z: number } }[];
    joined: boolean;
    world: number;
}

export function installDice(): void {
    let hint = 0;
    let offer: TableOffer | undefined;
    let pending = false;
    const remove = (): void => {
        if (hint) ActionHint.remove(hint);
        hint = 0;
    };
    const lookingAtTable = (table: TableOffer["tables"][number]): boolean => {
        if (!isInteractionTargetVisible(table.position, { maxDistance: 3, aimRadius: 1.25, height: 0.75 })) return false;
        const q = table.position;
        // Some catalog placements belong to inactive quest layers.
        return World.entitiesInRadius(new Vector3(q.x, q.y, q.z), 3, { class: "SmartObjectHolder", max: 64 }).some((entity) => entity.guid === table.id);
    };
    let selected = "";
    const update = (): void => {
        const player = LocalPlayer;
        const table = !pending && player && offer?.tables.slice().sort((a, b) => {
            const p = player.position;
            return Math.hypot(p.x - a.position.x, p.y - a.position.y, p.z - a.position.z) - Math.hypot(p.x - b.position.x, p.y - b.position.y, p.z - b.position.z);
        }).find(lookingAtTable);
        if (!table) {
            remove();
            return;
        }
        selected = table.id;
        const text = offer!.joined ? "Leave dice lobby" : "Play dice (join lobby)";
        if (!hint) {
            hint = ActionHint.create({ key: "primary_action", text }, () => {
                const target = offer?.tables.find((table) => table.id === selected);
                if (pending || !target || !lookingAtTable(target)) return;
                pending = true;
                remove();
                Events.emitServer(INTERACT_EVENT, { token: offer!.token, table: selected });
            });
        } else ActionHint.setText(hint, text);
    };
    Events.on(STATE_EVENT, (payload) => {
        let state: unknown;
        try { state = typeof payload === "string" ? JSON.parse(payload) : payload; }
        catch { return; }
        if (!state || typeof state !== "object") return;
        const value = state as TableOffer;
        if (typeof value.token !== "number" || typeof value.joined !== "boolean" || typeof value.world !== "number") return;
        if (!Array.isArray(value.tables) || value.tables.some((table) => !table || typeof table.id !== "string" || !table.position || ![table.position.x, table.position.y, table.position.z].every(Number.isFinite))) return;
        offer = value;
        pending = false;
        update();
    });
    const timer = setInterval(update, 100);
    Events.on("resourceStop", (name) => {
        if (name !== "kcdc-gamemode") return;
        clearInterval(timer);
        remove();
    });
    Events.emitServer("kcdc-gamemode:dice.sync");
}
