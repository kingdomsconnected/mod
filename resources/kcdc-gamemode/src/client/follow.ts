import { isInteractionTargetVisible } from "./interaction.js";

/** The native Follow control, including rebinding and gamepad glyphs. */
const FOLLOW_KEY = "chat_focus_follow_init";

export function installFollow(): void {
    let selected: FollowTarget | undefined;
    let hint = 0;
    let stopping = false;
    let owns = false;
    const remove = (): void => {
        if (hint) ActionHint.remove(hint);
        hint = 0;
    };
    const visible = (target: FollowTarget): boolean => isInteractionTargetVisible(target.position, { maxDistance: 20, aimRadius: 1.2, height: 1.3 });
    const update = (): void => {
        const active = Follow.getTarget();
        const nextStopping = owns && (active !== null || Follow.isPending());
        if (!nextStopping) owns = false;
        const local = LocalPlayer;
        const next = !nextStopping && local?.ready && local.alive
            ? Follow.targets().filter(visible).sort((a, b) => {
                const p = local.position;
                return Math.hypot(a.position.x - p.x, a.position.y - p.y, a.position.z - p.z) - Math.hypot(b.position.x - p.x, b.position.y - p.y, b.position.z - p.z);
            }).find((target) => Follow.canStart(target)) : undefined;
        // A press queued for one body must not select another on the next poll.
        if (next?.id !== selected?.id || next?.kind !== selected?.kind || nextStopping !== stopping) remove();
        selected = next;
        stopping = nextStopping;
        if (!next && !stopping) { remove(); return; }
        const text = stopping ? (active ? `Stop following ${active.name.slice(0, 40)}` : "Cancel follow") : `Follow ${next!.name.slice(0, 40)}`;
        if (hint) { ActionHint.setText(hint, text); return; }
        const target = selected;
        const cancel = stopping;
        hint = ActionHint.create({ key: FOLLOW_KEY, text }, () => {
            if (cancel) { Follow.stop(); owns = false; }
            else if (target) {
                const current = Follow.targets().find((candidate) => candidate.id === target.id && candidate.kind === target.kind);
                if (current && visible(current) && Follow.canStart(current)) owns = Follow.start(current);
            }
            update();
        });
    };
    const timer = setInterval(update, 100);
    Events.on("followStarted", update);
    Events.on("followStopped", update);
    Events.on("resourceStop", (name) => {
        if (name !== "kcdc-gamemode") return;
        // The client stops this resource's follow itself.
        clearInterval(timer);
        remove();
    });
}
