const RESOURCE = "kcdc-playground";
const PAGE = `fw://resources/${RESOURCE}/ui/index.html`;
let view = -1;
let ready = false;
let visible = false;
let cursor: Vector3 | null = null;
let creatorOwned = false;
let nextId = 1;
let pending = 0;
let pendingSince = 0;
let loadStarted = 0;

function decode(payload: unknown): Record<string, unknown> {
    if (typeof payload === "string") payload = JSON.parse(payload) as unknown;
    return payload && typeof payload === "object" ? payload as Record<string, unknown> : {};
}

function emit(channel: string, payload: unknown): void {
    if (view >= 0 && ready) Web.emit(view, channel, payload);
}

function hide(): void {
    visible = false;
    if (view >= 0) { Web.focusView(view, false); Web.hideView(view); }
}

function destroy(): void {
    hide();
    if (view >= 0) Web.destroyView(view);
    view = -1;
    ready = false;
}

function request(payload: Record<string, unknown>): void {
    if (pending) return;
    pending = nextId++;
    pendingSince = Date.now();
    const body: Record<string, unknown> = { ...payload, id: pending };
    if (body.action === "item" && body.destination === "cursor") body.position = cursor;
    Events.emitServer(`${RESOURCE}:request`, JSON.stringify(body));
}

function open(): void {
    if (!LocalPlayer?.ready || !LocalPlayer.alive || CharacterCreator.isOpen() || pending) return;
    const ray = Camera.screenRay({ range: 15 });
    cursor = ray ? World.raycast(ray.origin, ray.target, { mode: "ground" })?.position ?? null : null;
    if (view < 0) {
        const screen = Web.getScreenSize();
        const width = Math.min(1120, screen.width - 40);
        const height = Math.min(760, screen.height - 40);
        view = Web.createView(PAGE, { width, height, x: Math.round((screen.width - width) / 2), y: Math.round((screen.height - height) / 2), zIndex: 110, visible: true, focus: false });
        loadStarted = Date.now();
        Web.on(view, "playground:ready", () => {
            ready = true;
            if (visible) Web.focusView(view, true);
            emit("playground:open", { cursor: cursor !== null });
        });
        Web.on(view, "playground:close", hide);
        Web.on(view, "playground:request", (payload) => {
            if (!visible) return;
            try { request(decode(payload)); }
            catch { emit("playground:response", { ok: false, message: "Invalid panel request." }); }
        });
    }
    visible = true;
    Web.showView(view);
    if (ready) { Web.focusView(view, true); emit("playground:open", { cursor: cursor !== null }); }
}

Events.on(`${RESOURCE}:response`, (payload) => {
    const result = decode(payload);
    if (result.id !== pending) return;
    pending = 0;
    if (result.creator === true && result.ok === true) {
        hide();
        creatorOwned = CharacterCreator.open({ appearance: LocalPlayer?.appearance, genders: "both", title: "Playground character" });
        if (!creatorOwned) request({ action: "appearance", appearance: null });
    }
    emit("playground:response", result);
    if (!visible && typeof result.message === "string") Hud.showInfoText(result.message);
    if (result.close === true) hide();
});

Events.on("characterCreatorClosed", (appearance) => {
    if (!creatorOwned) return;
    creatorOwned = false;
    request({ action: "appearance", appearance });
});

const hint = ActionHint.create({ key: "kcdc_playground", text: "Playground", enabled: false }, open);
if (!hint) throw new Error("This client does not support the playground action hint. Update the client and server together.");

const timer = setInterval(() => {
    const enabled = !!LocalPlayer?.ready && LocalPlayer.alive && !visible && !CharacterCreator.isOpen() && !pending;
    ActionHint.setEnabled(hint, enabled);
    if (pending && Date.now() - pendingSince > 10000) {
        pending = 0;
        emit("playground:response", { ok: false, message: "The request timed out. Check your inventory or world before retrying a grant." });
    }
    if (view >= 0 && !ready && Date.now() - loadStarted > 10000) { destroy(); Hud.showInfoText("Playground page did not load. Try opening it again."); }
}, 200);

Events.on("browserLoadingFailed", (event) => {
    if (event.viewId === view && event.isMainFrame) { destroy(); Hud.showInfoText(`Playground page failed: ${event.description}`); }
});
Events.on("resourceStop", (name) => {
    if (name !== RESOURCE) return;
    clearInterval(timer);
    ActionHint.remove(hint);
    destroy();
    if (creatorOwned) { creatorOwned = false; CharacterCreator.close(); }
});
